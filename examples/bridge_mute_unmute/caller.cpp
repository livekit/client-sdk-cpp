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
 * Caller (controller) for the bridge mute/unmute example.
 *
 * Connects to the same room as the receiver, subscribes to the receiver's
 * "mic" and "cam" tracks, and renders them via SDL3 (speaker + window).
 * Every 5 seconds the caller toggles mute/unmute on both tracks via RPC,
 * so you can see and hear the tracks go silent and come back.
 *
 * Usage:
 *   BridgeMuteCaller <ws-url> <token> [receiver-identity]
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... BridgeMuteCaller [receiver-identity]
 *
 * The token must grant a different identity (e.g. "caller"). Generate with:
 *   lk token create --api-key <key> --api-secret <secret> \
 *       --join --room my-room --identity caller --valid-for 24h
 */

#include "livekit/audio_frame.h"
#include "livekit/rpc_error.h"
#include "livekit/track.h"
#include "livekit/video_frame.h"
#include "livekit_bridge/livekit_bridge.h"
#include "sdl_media.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};
static void handleSignal(int) { g_running.store(false); }

struct LatestVideoFrame {
  std::mutex mutex;
  std::vector<std::uint8_t> data;
  int width = 0;
  int height = 0;
  bool dirty = false;
};

static LatestVideoFrame g_latest_video;

static void storeFrame(const livekit::VideoFrame &frame) {
  const std::uint8_t *src = frame.data();
  const std::size_t size = frame.dataSize();
  if (!src || size == 0)
    return;

  std::lock_guard<std::mutex> lock(g_latest_video.mutex);
  g_latest_video.data.assign(src, src + size);
  g_latest_video.width = frame.width();
  g_latest_video.height = frame.height();
  g_latest_video.dirty = true;
}

int main(int argc, char *argv[]) {
  std::string url, token;
  std::string receiver_identity = "receiver";

  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    positional.push_back(argv[i]);
  }

  if (positional.size() >= 2) {
    url = positional[0];
    token = positional[1];
    if (positional.size() >= 3)
      receiver_identity = positional[2];
  } else {
    const char *e = std::getenv("LIVEKIT_URL");
    if (e)
      url = e;
    e = std::getenv("LIVEKIT_TOKEN");
    if (e)
      token = e;
    if (!positional.empty())
      receiver_identity = positional[0];
  }
  if (url.empty() || token.empty()) {
    std::cerr
        << "Usage: BridgeMuteCaller <ws-url> <token> [receiver-identity]\n"
        << "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... BridgeMuteCaller "
           "[receiver-identity]\n"
        << "Default receiver-identity: \"receiver\"\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);

  // ----- Initialize SDL3 -----
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    std::cerr << "[caller] SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  constexpr int kWindowWidth = 640;
  constexpr int kWindowHeight = 480;

  SDL_Window *window = SDL_CreateWindow("Caller - Receiver Feed", kWindowWidth,
                                        kWindowHeight, 0);
  if (!window) {
    std::cerr << "[caller] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer) {
    std::cerr << "[caller] SDL_CreateRenderer failed: " << SDL_GetError()
              << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SDL_Texture *texture = nullptr;
  int tex_width = 0;
  int tex_height = 0;

  std::unique_ptr<DDLSpeakerSink> speaker;
  std::mutex speaker_mutex;

  // ----- Connect to LiveKit -----
  livekit_bridge::LiveKitBridge bridge;
  std::cout << "[caller] Connecting to " << url << " ...\n";

  livekit::RoomOptions options;
  options.auto_subscribe = true;

  if (!bridge.connect(url, token, options)) {
    std::cerr << "[caller] Failed to connect.\n";
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::cout << "[caller] Connected.\n";
  std::cout << "[caller] Target receiver identity: \"" << receiver_identity
            << "\"\n";

  // ----- Subscribe to receiver's audio -----
  bridge.setOnAudioFrameCallback(
      receiver_identity, livekit::TrackSource::SOURCE_MICROPHONE,
      [&speaker, &speaker_mutex](const livekit::AudioFrame &frame) {
        const auto &samples = frame.data();
        if (samples.empty())
          return;

        std::lock_guard<std::mutex> lock(speaker_mutex);
        if (!speaker) {
          speaker = std::make_unique<DDLSpeakerSink>(frame.sample_rate(),
                                                     frame.num_channels());
          if (!speaker->init()) {
            std::cerr << "[caller] Failed to init SDL speaker.\n";
            speaker.reset();
            return;
          }
          std::cout << "[caller] Speaker opened: " << frame.sample_rate()
                    << " Hz, " << frame.num_channels() << " ch.\n";
        }
        speaker->enqueue(samples.data(), frame.samples_per_channel());
      });

  // ----- Subscribe to receiver's video -----
  bridge.setOnVideoFrameCallback(
      receiver_identity, livekit::TrackSource::SOURCE_CAMERA,
      [](const livekit::VideoFrame &frame, std::int64_t /*timestamp_us*/) {
        storeFrame(frame);
      });

  std::cout << "[caller] Subscribed to receiver's mic + cam.\n";

  // ----- Mute/unmute toggle thread -----
  std::atomic<bool> muted{false};
  std::atomic<int> cycle{0};

  std::atomic<bool> toggle_running{true};
  std::thread toggle_thread([&]() {
    // Let the receiver connect and publish before we start toggling
    for (int i = 0; i < 30 && toggle_running.load(); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

    while (toggle_running.load()) {
      bool currently_muted = muted.load();
      const char *action = currently_muted ? "UNMUTE" : "MUTE";
      int c = cycle.fetch_add(1) + 1;
      std::cout << "\n[caller] --- Cycle " << c << ": " << action << " ---\n";

      // Toggle audio track "mic"
      try {
        if (currently_muted) {
          bridge.requestRemoteTrackUnmute(receiver_identity, "mic");
          std::cout << "[caller]   mic: unmuted OK\n";
        } else {
          bridge.requestRemoteTrackMute(receiver_identity, "mic");
          std::cout << "[caller]   mic: muted OK\n";
        }
      } catch (const livekit::RpcError &e) {
        std::cerr << "[caller]   mic: RPC error (code=" << e.code() << " msg=\""
                  << e.message() << "\")\n";
      } catch (const std::exception &e) {
        std::cerr << "[caller]   mic: error: " << e.what() << "\n";
      }

      // Toggle video track "cam"
      try {
        if (currently_muted) {
          bridge.requestRemoteTrackUnmute(receiver_identity, "cam");
          std::cout << "[caller]   cam: unmuted OK\n";
        } else {
          bridge.requestRemoteTrackMute(receiver_identity, "cam");
          std::cout << "[caller]   cam: muted OK\n";
        }
      } catch (const livekit::RpcError &e) {
        std::cerr << "[caller]   cam: RPC error (code=" << e.code() << " msg=\""
                  << e.message() << "\")\n";
      } catch (const std::exception &e) {
        std::cerr << "[caller]   cam: error: " << e.what() << "\n";
      }

      muted.store(!currently_muted);

      // Wait ~100 seconds, checking for shutdown every 100ms
      for (int i = 0; i < 100 && toggle_running.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  // ----- Main loop: render video + pump SDL events -----
  std::cout << "[caller] Rendering receiver feed. Toggling mute every 5s. "
               "Close window or Ctrl-C to stop.\n";

  std::vector<std::uint8_t> local_pixels;

  while (g_running.load()) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT) {
        g_running.store(false);
      }
    }

    int fw = 0, fh = 0;
    bool have_frame = false;
    {
      std::lock_guard<std::mutex> lock(g_latest_video.mutex);
      if (g_latest_video.dirty && g_latest_video.width > 0 &&
          g_latest_video.height > 0) {
        fw = g_latest_video.width;
        fh = g_latest_video.height;
        local_pixels.swap(g_latest_video.data);
        g_latest_video.dirty = false;
        have_frame = true;
      }
    }

    if (have_frame) {
      if (fw != tex_width || fh != tex_height) {
        if (texture)
          SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING, fw, fh);
        tex_width = fw;
        tex_height = fh;
      }

      if (texture) {
        void *pixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(texture, nullptr, &pixels, &pitch)) {
          const int srcPitch = fw * 4;
          for (int y = 0; y < fh; ++y) {
            std::memcpy(static_cast<std::uint8_t *>(pixels) + y * pitch,
                        local_pixels.data() + y * srcPitch, srcPitch);
          }
          SDL_UnlockTexture(texture);
        }
      }
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (texture) {
      SDL_RenderTexture(renderer, texture, nullptr, nullptr);
    }
    SDL_RenderPresent(renderer);

    SDL_Delay(16);
  }

  // ----- Cleanup -----
  std::cout << "\n[caller] Shutting down...\n";
  toggle_running.store(false);
  if (toggle_thread.joinable())
    toggle_thread.join();

  bridge.disconnect();

  {
    std::lock_guard<std::mutex> lock(speaker_mutex);
    speaker.reset();
  }

  if (texture)
    SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  std::cout << "[caller] Done.\n";
  return 0;
}
