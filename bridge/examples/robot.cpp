/*
 * Robot example -- streams real webcam video and microphone audio to a
 * LiveKit room using SDL3 for hardware capture.
 *
 * Usage:
 *   robot <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... robot
 *
 * The token must grant identity "robot". Generate one with:
 *   lk token create --api-key <key> --api-secret <secret> \
 *       --join --room my-room --identity robot \
 *       --valid-for 24h
 *
 * Run alongside the "human" example (which displays the robot's feed).
 */

#include "livekit/audio_frame.h"
#include "livekit/track.h"
#include "livekit/video_frame.h"
#include "livekit_bridge/livekit_bridge.h"
#include "sdl_media.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};
static void handleSignal(int) { g_running.store(false); }

int main(int argc, char *argv[]) {
  // ----- Parse args / env -----
  std::string url, token;
  if (argc >= 3) {
    url = argv[1];
    token = argv[2];
  } else {
    const char *e = std::getenv("LIVEKIT_URL");
    if (e)
      url = e;
    e = std::getenv("LIVEKIT_TOKEN");
    if (e)
      token = e;
  }
  if (url.empty() || token.empty()) {
    std::cerr << "Usage: robot <ws-url> <token>\n"
              << "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... robot\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);

  // ----- Initialize SDL3 -----
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_CAMERA)) {
    std::cerr << "[robot] SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  // ----- Connect to LiveKit -----
  livekit_bridge::LiveKitBridge bridge;
  std::cout << "[robot] Connecting to " << url << " ...\n";
  if (!bridge.connect(url, token)) {
    std::cerr << "[robot] Failed to connect.\n";
    SDL_Quit();
    return 1;
  }
  std::cout << "[robot] Connected.\n";

  // ----- Create outgoing tracks -----
  constexpr int kSampleRate = 48000;
  constexpr int kChannels = 1;
  constexpr int kWidth = 1280;
  constexpr int kHeight = 720;

  auto mic = bridge.createAudioTrack("robot-mic", kSampleRate, kChannels);
  auto cam = bridge.createVideoTrack("robot-cam", kWidth, kHeight);
  std::cout << "[robot] Publishing audio (" << kSampleRate << " Hz, "
            << kChannels << " ch) and video (" << kWidth << "x" << kHeight
            << ").\n";

  // ----- SDL Mic capture -----
  // SDLMicSource pulls 10ms frames from the default recording device and
  // invokes our callback with interleaved int16 samples.
  bool mic_using_sdl = false;
  std::unique_ptr<SDLMicSource> sdl_mic;
  std::atomic<bool> mic_running{true};
  std::thread mic_thread;

  {
    int recCount = 0;
    SDL_AudioDeviceID *recDevs = SDL_GetAudioRecordingDevices(&recCount);
    bool has_mic = recDevs && recCount > 0;
    if (recDevs)
      SDL_free(recDevs);

    if (has_mic) {
      sdl_mic = std::make_unique<SDLMicSource>(
          kSampleRate, kChannels, kSampleRate / 100, // 10ms frames
          [&mic](const int16_t *samples, int num_samples_per_channel,
                 int /*sample_rate*/, int /*num_channels*/) {
            try {
              mic->pushFrame(samples, num_samples_per_channel);
            } catch (const std::exception &e) {
              std::cerr << "[robot] Mic push error: " << e.what() << "\n";
            }
          });

      if (sdl_mic->init()) {
        mic_using_sdl = true;
        std::cout << "[robot] Using SDL microphone.\n";
        mic_thread = std::thread([&]() {
          while (mic_running.load()) {
            sdl_mic->pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        });
      } else {
        std::cerr << "[robot] SDL mic init failed.\n";
        sdl_mic.reset();
      }
    }

    if (!mic_using_sdl) {
      std::cout << "[robot] No microphone found; sending silence.\n";
      mic_thread = std::thread([&]() {
        constexpr int kSamplesPerFrame = kSampleRate / 100;
        std::vector<std::int16_t> silence(kSamplesPerFrame * kChannels, 0);
        auto next = std::chrono::steady_clock::now();
        while (mic_running.load()) {
          try {
            mic->pushFrame(silence, kSamplesPerFrame);
          } catch (...) {
          }
          next += std::chrono::milliseconds(10);
          std::this_thread::sleep_until(next);
        }
      });
    }
  }

  // ----- SDL Camera capture -----
  // SDLCamSource grabs webcam frames and invokes our callback with raw pixels.
  bool cam_using_sdl = false;
  std::unique_ptr<SDLCamSource> sdl_cam;
  std::atomic<bool> cam_running{true};
  std::thread cam_thread;

  {
    int camCount = 0;
    SDL_CameraID *cams = SDL_GetCameras(&camCount);
    bool has_cam = cams && camCount > 0;
    if (cams)
      SDL_free(cams);

    if (has_cam) {
      sdl_cam = std::make_unique<SDLCamSource>(
          kWidth, kHeight, 30, SDL_PIXELFORMAT_RGBA32,
          [&cam](const uint8_t *pixels, int pitch, int width, int height,
                 SDL_PixelFormat /*fmt*/, Uint64 timestampNS) {
            // Copy row-by-row (pitch may differ from width*4)
            const int dstPitch = width * 4;
            std::vector<std::uint8_t> buf(dstPitch * height);
            for (int y = 0; y < height; ++y) {
              std::memcpy(buf.data() + y * dstPitch, pixels + y * pitch,
                          dstPitch);
            }
            try {
              cam->pushFrame(buf.data(), buf.size(),
                             static_cast<std::int64_t>(timestampNS / 1000));
            } catch (const std::exception &e) {
              std::cerr << "[robot] Cam push error: " << e.what() << "\n";
            }
          });

      if (sdl_cam->init()) {
        cam_using_sdl = true;
        std::cout << "[robot] Using SDL camera.\n";
        cam_thread = std::thread([&]() {
          while (cam_running.load()) {
            sdl_cam->pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        });
      } else {
        std::cerr << "[robot] SDL camera init failed.\n";
        sdl_cam.reset();
      }
    }

    if (!cam_using_sdl) {
      std::cout << "[robot] No camera found; sending solid green frames.\n";
      cam_thread = std::thread([&]() {
        std::vector<std::uint8_t> green(kWidth * kHeight * 4);
        for (int i = 0; i < kWidth * kHeight; ++i) {
          green[i * 4 + 0] = 0;
          green[i * 4 + 1] = 180;
          green[i * 4 + 2] = 0;
          green[i * 4 + 3] = 255;
        }
        std::int64_t ts = 0;
        while (cam_running.load()) {
          try {
            cam->pushFrame(green, ts);
            ts += 33333;
          } catch (...) {
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
      });
    }
  }

  // ----- Main loop: keep alive + pump SDL events -----
  std::cout << "[robot] Streaming... press Ctrl-C to stop.\n";

  while (g_running.load()) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        g_running.store(false);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // ----- Cleanup -----
  std::cout << "[robot] Shutting down...\n";

  mic_running.store(false);
  cam_running.store(false);
  if (mic_thread.joinable())
    mic_thread.join();
  if (cam_thread.joinable())
    cam_thread.join();
  sdl_mic.reset();
  sdl_cam.reset();

  mic.reset();
  cam.reset();
  bridge.disconnect();

  SDL_Quit();
  std::cout << "[robot] Done.\n";
  return 0;
}
