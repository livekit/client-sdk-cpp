/*
 * Copyright 2025 LiveKit
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
 * Human example -- receives audio and video frames from a robot in a
 * LiveKit room and renders them using SDL3.
 *
 * This example demonstrates the base SDK's convenience frame callback API
 * (Room::setOnAudioFrameCallback / Room::setOnVideoFrameCallback) which
 * eliminates the need for a RoomDelegate subclass, manual AudioStream/
 * VideoStream creation, and reader threads.
 *
 * The robot publishes two video tracks and two audio tracks:
 *   - "robot-cam"        (SOURCE_CAMERA)            -- webcam or placeholder
 *   - "robot-sim-frame"  (SOURCE_SCREENSHARE)        -- simulated diagnostic
 * frame
 *   - "robot-mic"        (SOURCE_MICROPHONE)          -- real microphone or
 * silence
 *   - "robot-sim-audio"  (SOURCE_SCREENSHARE_AUDIO)   -- simulated siren tone
 *
 * Press 'w' to play the webcam feed + real mic, or 's' for sim frame + siren.
 * The selection controls both video and audio simultaneously.
 *
 * Usage:
 *   human [--no-audio] <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... human [--no-audio]
 *
 * --no-audio   Subscribe to audio tracks but suppress local playback.
 *
 * The token must grant identity "human". Generate one with:
 *   lk token create --api-key <key> --api-secret <secret> \
 *       --join --room my-room --identity human \
 *       --valid-for 24h
 *
 * Run alongside the "robot" example (which publishes with identity "robot").
 */

#include "livekit/audio_frame.h"
#include "livekit/livekit.h"
#include "livekit/track.h"
#include "livekit/video_frame.h"
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

// ---- Video source selection ----
enum class SelectedSource : int { Webcam = 0, SimFrame = 1 };
static std::atomic<int> g_selected_source{
    static_cast<int>(SelectedSource::Webcam)};

// ---- Thread-safe video frame slot ----
struct LatestVideoFrame {
  std::mutex mutex;
  std::vector<std::uint8_t> data;
  int width = 0;
  int height = 0;
  bool dirty = false;
};

static LatestVideoFrame g_latest_video;

static void renderFrame(const livekit::VideoFrame &frame) {
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

// ---- Counters for periodic status ----
static std::atomic<uint64_t> g_audio_frames{0};
static std::atomic<uint64_t> g_video_frames{0};

constexpr const char *kRobotMicTrackName = "robot-mic";
constexpr const char *kRobotSimAudioTrackName = "robot-sim-audio";
constexpr const char *kRobotCamTrackName = "robot-cam";
constexpr const char *kRobotSimVideoTrackName = "robot-sim-frame";

int main(int argc, char *argv[]) {
  // ----- Parse args / env -----
  bool no_audio = false;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--no-audio") {
      no_audio = true;
    } else {
      positional.push_back(argv[i]);
    }
  }

  std::string url, token;
  auto is_ws_url = [](const std::string &s) {
    return (s.size() >= 5 && s.compare(0, 5, "ws://") == 0) ||
           (s.size() >= 6 && s.compare(0, 6, "wss://") == 0);
  };
  if (positional.size() >= 2) {
    for (const auto &arg : positional) {
      if (is_ws_url(arg)) {
        url = arg;
        break;
      }
    }
    for (const auto &arg : positional) {
      if (arg != url) {
        token = arg;
        break;
      }
    }
    if (url.empty())
      url = positional[0], token = positional[1];
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
        << "Usage: human [--no-audio] <ws-url> <token>\n"
        << "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... human [--no-audio]\n";
    return 1;
  }
  if (no_audio) {
    std::cout << "[human] --no-audio: audio playback disabled.\n";
  }

  std::signal(SIGINT, handleSignal);

  // ----- Initialize SDL3 -----
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    std::cerr << "[human] SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  // ----- Create SDL window + renderer -----
  constexpr int kWindowWidth = 1280;
  constexpr int kWindowHeight = 720;

  SDL_Window *window = SDL_CreateWindow("Human - Robot Camera Feed",
                                        kWindowWidth, kWindowHeight, 0);
  if (!window) {
    std::cerr << "[human] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
  if (!renderer) {
    std::cerr << "[human] SDL_CreateRenderer failed: " << SDL_GetError()
              << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SDL_Texture *texture = nullptr;
  int tex_width = 0;
  int tex_height = 0;

  // ----- SDL speaker for audio playback -----
  std::unique_ptr<DDLSpeakerSink> speaker;
  std::mutex speaker_mutex;

  // ----- Connect to LiveKit using the base SDK -----
  livekit::initialize();

  auto room = std::make_unique<livekit::Room>();
  std::cout << "[human] Connecting to " << url << " ...\n";
  livekit::RoomOptions options;
  options.auto_subscribe = true;
  if (!room->Connect(url, token, options)) {
    std::cerr << "[human] Failed to connect.\n";
    livekit::shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  std::cout << "[human] Connected. Waiting for robot...\n";

  // Helper: enqueue audio to the speaker (lazily initializes on first call)
  auto playAudio = [&speaker,
                    &speaker_mutex](const livekit::AudioFrame &frame) {
    const auto &samples = frame.data();
    if (samples.empty())
      return;

    std::lock_guard<std::mutex> lock(speaker_mutex);

    if (!speaker) {
      speaker = std::make_unique<DDLSpeakerSink>(frame.sample_rate(),
                                                 frame.num_channels());
      if (!speaker->init()) {
        std::cerr << "[human] Failed to init SDL speaker.\n";
        speaker.reset();
        return;
      }
      std::cout << "[human] Speaker opened: " << frame.sample_rate() << " Hz, "
                << frame.num_channels() << " ch.\n";
    }

    speaker->enqueue(samples.data(), frame.samples_per_channel());
  };

  // ----- Set audio callbacks using Room::setOnAudioFrameCallback -----
  room->setOnAudioFrameCallback(
      "robot", kRobotMicTrackName,
      [playAudio, no_audio](const livekit::AudioFrame &frame) {
        g_audio_frames.fetch_add(1, std::memory_order_relaxed);
        if (!no_audio && g_selected_source.load(std::memory_order_relaxed) ==
                             static_cast<int>(SelectedSource::Webcam)) {
          playAudio(frame);
        }
      });

  room->setOnAudioFrameCallback(
      "robot", kRobotSimAudioTrackName,
      [playAudio, no_audio](const livekit::AudioFrame &frame) {
        g_audio_frames.fetch_add(1, std::memory_order_relaxed);
        if (!no_audio && g_selected_source.load(std::memory_order_relaxed) ==
                             static_cast<int>(SelectedSource::SimFrame)) {
          playAudio(frame);
        }
      });

  // ----- Set video callbacks using Room::setOnVideoFrameCallback -----
  room->setOnVideoFrameCallback(
      "robot", kRobotCamTrackName,
      [](const livekit::VideoFrame &frame, std::int64_t /*timestamp_us*/) {
        g_video_frames.fetch_add(1, std::memory_order_relaxed);
        if (g_selected_source.load(std::memory_order_relaxed) ==
            static_cast<int>(SelectedSource::Webcam)) {
          renderFrame(frame);
        }
      });

  room->setOnVideoFrameCallback(
      "robot", kRobotSimVideoTrackName,
      [](const livekit::VideoFrame &frame, std::int64_t /*timestamp_us*/) {
        g_video_frames.fetch_add(1, std::memory_order_relaxed);
        if (g_selected_source.load(std::memory_order_relaxed) ==
            static_cast<int>(SelectedSource::SimFrame)) {
          renderFrame(frame);
        }
      });

  // ----- Stdin input thread -----
  std::thread input_thread([&]() {
    std::string line;
    while (g_running.load() && std::getline(std::cin, line)) {
      if (line == "w" || line == "W") {
        g_selected_source.store(static_cast<int>(SelectedSource::Webcam),
                                std::memory_order_relaxed);
        std::cout << "[human] Switched to webcam + mic.\n";
      } else if (line == "s" || line == "S") {
        g_selected_source.store(static_cast<int>(SelectedSource::SimFrame),
                                std::memory_order_relaxed);
        std::cout << "[human] Switched to sim frame + siren.\n";
      }
    }
  });

  // ----- Main loop -----
  std::cout
      << "[human] Rendering robot feed. Press 'w' for webcam + mic, "
         "'s' for sim frame + siren (in this terminal or the SDL window). "
         "Ctrl-C or close window to stop.\n";

  auto last_report = std::chrono::steady_clock::now();

  std::vector<std::uint8_t> local_pixels;

  while (g_running.load()) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT) {
        g_running.store(false);
      } else if (ev.type == SDL_EVENT_KEY_DOWN) {
        if (ev.key.key == SDLK_W) {
          g_selected_source.store(static_cast<int>(SelectedSource::Webcam),
                                  std::memory_order_relaxed);
          std::cout << "[human] Switched to webcam + mic.\n";
        } else if (ev.key.key == SDLK_S) {
          g_selected_source.store(static_cast<int>(SelectedSource::SimFrame),
                                  std::memory_order_relaxed);
          std::cout << "[human] Switched to sim frame + siren.\n";
        }
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
        if (!texture) {
          std::cerr << "[human] SDL_CreateTexture failed: " << SDL_GetError()
                    << "\n";
        }
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

    auto now = std::chrono::steady_clock::now();
    if (now - last_report >= std::chrono::seconds(5)) {
      last_report = now;
      const char *src_name =
          g_selected_source.load(std::memory_order_relaxed) ==
                  static_cast<int>(SelectedSource::Webcam)
              ? "webcam"
              : "sim_frame";
      std::cout << "[human] Status: " << g_audio_frames.load()
                << " audio frames, " << g_video_frames.load()
                << " video frames received (showing: " << src_name << ").\n";
    }

    SDL_Delay(16);
  }

  // ----- Cleanup -----
  std::cout << "[human] Shutting down...\n";
  std::cout << "[human] Total received: " << g_audio_frames.load()
            << " audio frames, " << g_video_frames.load() << " video frames.\n";

  if (input_thread.joinable())
    input_thread.detach();

  // Room destructor calls stopAllReaders() which closes streams and joins
  // reader threads, then tears down FFI state.
  room.reset();
  livekit::shutdown();

  {
    std::lock_guard<std::mutex> lock(speaker_mutex);
    speaker.reset();
  }

  if (texture)
    SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  std::cout << "[human] Done.\n";
  return 0;
}
