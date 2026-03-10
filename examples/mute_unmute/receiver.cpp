/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Receiver (publisher) for the mute/unmute example.
 *
 * Publishes an audio track ("mic") and a video track ("cam"), then enables
 * remote track control so that a remote caller can mute/unmute them via RPC.
 *
 * By default, captures from the real microphone and webcam using SDL3. If
 * no hardware is available, falls back to silence (audio) and solid-color
 * frames (video).
 *
 * Usage:
 *   MuteUnmuteReceiver <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... MuteUnmuteReceiver
 *
 * The token must grant identity "receiver". Generate one with:
 *   lk token create --api-key <key> --api-secret <secret> \
 *       --join --room my-room --identity receiver --valid-for 24h
 */

#include "livekit/track.h"
#include "sdl_media.h"
#include "session_manager/session_manager.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};
static void handleSignal(int) { g_running.store(false); }

int main(int argc, char *argv[]) {
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
    std::cerr
        << "Usage: MuteUnmuteReceiver <ws-url> <token>\n"
        << "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... MuteUnmuteReceiver\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);

  // ----- Initialize SDL3 -----
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_CAMERA)) {
    std::cerr << "[receiver] SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  // ----- Connect to LiveKit -----
  session_manager::SessionManager sm;
  std::cout << "[receiver] Connecting to " << url << " ...\n";

  livekit::RoomOptions options;
  options.auto_subscribe = true;

  if (!sm.connect(url, token, options)) {
    std::cerr << "[receiver] Failed to connect.\n";
    SDL_Quit();
    return 1;
  }
  std::cout << "[receiver] Connected.\n";

  constexpr int kSampleRate = 48000;
  constexpr int kChannels = 1;
  constexpr int kWidth = 1280;
  constexpr int kHeight = 720;

  auto mic = sm.createAudioTrack("mic", kSampleRate, kChannels,
                                 livekit::TrackSource::SOURCE_MICROPHONE);
  auto cam = sm.createVideoTrack("cam", kWidth, kHeight,
                                 livekit::TrackSource::SOURCE_CAMERA);

  std::cout << "[receiver] Published audio track \"mic\" and video track "
               "\"cam\".\n";
  std::cout << "[receiver] Waiting for remote mute/unmute commands...\n";

  // ----- SDL Mic capture -----
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
          kSampleRate, kChannels, kSampleRate / 100,
          [&mic](const int16_t *samples, int num_samples_per_channel,
                 int /*sample_rate*/, int /*num_channels*/) {
            mic->pushFrame(samples, num_samples_per_channel);
          });

      if (sdl_mic->init()) {
        mic_using_sdl = true;
        std::cout << "[receiver] Using SDL microphone.\n";
        mic_thread = std::thread([&]() {
          while (mic_running.load()) {
            sdl_mic->pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        });
      } else {
        std::cerr << "[receiver] SDL mic init failed.\n";
        sdl_mic.reset();
      }
    }

    if (!mic_using_sdl) {
      std::cout << "[receiver] No microphone found; sending silence.\n";
      mic_thread = std::thread([&]() {
        const int kSamplesPerFrame = kSampleRate / 100;
        std::vector<std::int16_t> silence(kSamplesPerFrame * kChannels, 0);
        auto next = std::chrono::steady_clock::now();
        while (mic_running.load()) {
          mic->pushFrame(silence, kSamplesPerFrame);
          next += std::chrono::milliseconds(10);
          std::this_thread::sleep_until(next);
        }
      });
    }
  }

  // ----- SDL Camera capture -----
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
            const int dstPitch = width * 4;
            std::vector<std::uint8_t> buf(dstPitch * height);
            for (int y = 0; y < height; ++y) {
              std::memcpy(buf.data() + y * dstPitch, pixels + y * pitch,
                          dstPitch);
            }
            cam->pushFrame(buf.data(), buf.size(),
                           static_cast<std::int64_t>(timestampNS / 1000));
          });

      if (sdl_cam->init()) {
        cam_using_sdl = true;
        std::cout << "[receiver] Using SDL camera.\n";
        cam_thread = std::thread([&]() {
          while (cam_running.load()) {
            sdl_cam->pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        });
      } else {
        std::cerr << "[receiver] SDL camera init failed.\n";
        sdl_cam.reset();
      }
    }

    if (!cam_using_sdl) {
      std::cout << "[receiver] No camera found; sending solid-color frames.\n";
      cam_thread = std::thread([&]() {
        std::vector<std::uint8_t> frame(kWidth * kHeight * 4);
        std::int64_t ts = 0;
        int frame_num = 0;

        while (cam_running.load()) {
          bool blue = (frame_num / 30) % 2 == 0;
          for (int i = 0; i < kWidth * kHeight; ++i) {
            frame[i * 4 + 0] = 0;
            frame[i * 4 + 1] =
                blue ? static_cast<uint8_t>(0) : static_cast<uint8_t>(180);
            frame[i * 4 + 2] =
                blue ? static_cast<uint8_t>(200) : static_cast<uint8_t>(0);
            frame[i * 4 + 3] = 255;
          }

          cam->pushFrame(frame, ts);

          ++frame_num;
          ts += 33333;
          std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
      });
    }
  }

  // ----- Main loop: pump SDL events (needed for camera approval on macOS)
  // -----
  std::cout << "[receiver] Press Ctrl-C to stop.\n";
  while (g_running.load()) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT) {
        g_running.store(false);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // ----- Cleanup -----
  std::cout << "[receiver] Shutting down...\n";
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
  sm.disconnect();

  SDL_Quit();
  std::cout << "[receiver] Done.\n";
  return 0;
}
