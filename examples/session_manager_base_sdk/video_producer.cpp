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
 * SessionManager + base SDK video producer.
 *
 * SessionManager handles connect/disconnect; getRoom() provides the Room.
 * All other work uses the Room directly: room_info(), localParticipant(),
 * publishTrack(), etc.
 *
 * Usage:
 *   video_producer <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... video_producer
 *
 * Use identity "producer" for the token. Run video_viewer in another terminal.
 *
 * Requires a webcam. Falls back to solid-color frames if no camera is found.
 */

#include "livekit/livekit.h"
#include "lk_log.h"
#include "session_manager/session_manager.h"
#include "sdl_media.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace livekit;

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
    std::cerr << "Usage: video_producer <ws-url> <token>\n"
              << "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... video_producer\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);

  if (!SDL_Init(SDL_INIT_CAMERA)) {
    LK_LOG_ERROR("[video_producer] SDL_Init failed: {}", SDL_GetError());
    return 1;
  }

  session_manager::SessionManager sm;
  LK_LOG_INFO("[video_producer] Connecting to {} ...", url);
  livekit::RoomOptions options;
  options.auto_subscribe = true;
  if (!sm.connect(url, token, options)) {
    LK_LOG_ERROR("[video_producer] Failed to connect.");
    SDL_Quit();
    return 1;
  }

  livekit::Room *room = sm.getRoom();
  if (!room) {
    LK_LOG_ERROR("[video_producer] getRoom() returned nullptr.");
    sm.disconnect();
    SDL_Quit();
    return 1;
  }

  LK_LOG_INFO("[video_producer] Connected.");

  auto info = room->room_info();
  std::cout << "[video_producer] Room info:\n"
            << "  SID: " << (info.sid ? *info.sid : "(none)") << "\n"
            << "  Name: " << info.name << "\n"
            << "  Num participants: " << info.num_participants << "\n";

  constexpr int kWidth = 1280;
  constexpr int kHeight = 720;

  auto videoSource = std::make_shared<VideoSource>(kWidth, kHeight);
  auto videoTrack =
      LocalVideoTrack::createLocalVideoTrack("cam", videoSource);

  TrackPublishOptions videoOpts;
  videoOpts.source = TrackSource::SOURCE_CAMERA;
  videoOpts.dtx = false;
  videoOpts.simulcast = true;

  std::shared_ptr<LocalTrackPublication> videoPub;
  try {
    videoPub = room->localParticipant()->publishTrack(videoTrack, videoOpts);
    LK_LOG_INFO("[video_producer] Published cam track {}x{} (SID: {}).",
                kWidth, kHeight, videoPub->sid());
  } catch (const std::exception &e) {
    LK_LOG_ERROR("[video_producer] Failed to publish track: {}", e.what());
    sm.disconnect();
    SDL_Quit();
    return 1;
  }

  // ---- SDL Camera capture or fallback ----
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
          [videoSource](const uint8_t *pixels, int pitch, int width, int height,
                        SDL_PixelFormat /*fmt*/, Uint64 timestampNS) {
            auto frame =
                VideoFrame::create(width, height, VideoBufferType::RGBA);
            uint8_t *dst = frame.data();
            const int dstPitch = width * 4;
            for (int y = 0; y < height; ++y) {
              std::memcpy(dst + y * dstPitch, pixels + y * pitch, dstPitch);
            }
            try {
              videoSource->captureFrame(
                  frame, static_cast<std::int64_t>(timestampNS / 1000),
                  VideoRotation::VIDEO_ROTATION_0);
            } catch (const std::exception &e) {
              LK_LOG_ERROR("[video_producer] captureFrame error: {}", e.what());
            }
          });

      if (sdl_cam->init()) {
        cam_using_sdl = true;
        LK_LOG_INFO("[video_producer] Using SDL webcam.");
        cam_thread = std::thread([&]() {
          while (cam_running.load()) {
            sdl_cam->pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        });
      } else {
        LK_LOG_ERROR("[video_producer] SDL camera init failed.");
        sdl_cam.reset();
      }
    }

    if (!cam_using_sdl) {
      LK_LOG_INFO("[video_producer] No camera found; sending solid blue "
                  "frames.");
      cam_thread = std::thread([videoSource, &cam_running]() {
        auto frame =
            VideoFrame::create(kWidth, kHeight, VideoBufferType::RGBA);
        uint8_t *dst = frame.data();
        for (int i = 0; i < kWidth * kHeight; ++i) {
          dst[i * 4 + 0] = 0;
          dst[i * 4 + 1] = 0;
          dst[i * 4 + 2] = 180;
          dst[i * 4 + 3] = 255;
        }
        std::int64_t ts = 0;
        while (cam_running.load()) {
          try {
            videoSource->captureFrame(frame, ts,
                                     VideoRotation::VIDEO_ROTATION_0);
          } catch (...) {
            break;
          }
          ts += 33333;
          std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
      });
    }
  }

  LK_LOG_INFO("[video_producer] Streaming... press Ctrl-C to stop.");

  while (g_running.load()) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        g_running.store(false);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // ---- Cleanup ----
  LK_LOG_INFO("[video_producer] Shutting down...");
  cam_running.store(false);
  if (cam_thread.joinable())
    cam_thread.join();
  sdl_cam.reset();

  room->localParticipant()->unpublishTrack(videoPub->sid());
  sm.disconnect();
  SDL_Quit();
  LK_LOG_INFO("[video_producer] Done.");
  return 0;
}
