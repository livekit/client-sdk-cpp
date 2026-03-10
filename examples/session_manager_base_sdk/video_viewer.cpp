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
 * SessionManager + base SDK video viewer.
 *
 * SessionManager handles connect/disconnect; getRoom() provides the Room.
 * All other work uses the Room directly: room_info(), remoteParticipants(),
 * track publications, VideoStream::fromTrack(), etc.
 *
 * Usage:
 *   video_viewer <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... video_viewer
 *
 * Use identity "viewer" for the token. Run video_producer (identity
 * "producer") first. Displays the producer's camera track in an SDL window.
 */

#include "livekit/livekit.h"
#include "lk_log.h"
#include "session_manager/session_manager.h"
#include "sdl_media_manager.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

using namespace livekit;

std::atomic<bool> g_running{true};
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
    std::cerr << "Usage: video_viewer <ws-url> <token>\n"
              << "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... video_viewer\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    LK_LOG_ERROR("[video_viewer] SDL_Init failed: {}", SDL_GetError());
    return 1;
  }

  SDLMediaManager media;

  session_manager::SessionManager sm;
  LK_LOG_INFO("[video_viewer] Connecting to {} ...", url);
  livekit::RoomOptions options;
  options.auto_subscribe = true;
  if (!sm.connect(url, token, options)) {
    LK_LOG_ERROR("[video_viewer] Failed to connect.");
    SDL_Quit();
    return 1;
  }

  livekit::Room *room = sm.getRoom();
  if (!room) {
    LK_LOG_ERROR("[video_viewer] getRoom() returned nullptr.");
    sm.disconnect();
    SDL_Quit();
    return 1;
  }

  LK_LOG_INFO("[video_viewer] Connected. Waiting for producer...");

  auto info = room->room_info();
  std::cout << "[video_viewer] Room info:\n"
            << "  SID: " << (info.sid ? *info.sid : "(none)") << "\n"
            << "  Name: " << info.name << "\n"
            << "  Num participants: " << info.num_participants << "\n";

  std::cout << "[video_viewer] Waiting for video... Ctrl-C or close window to "
               "stop.\n";

  bool renderer_initialized = false;

  while (g_running.load()) {
    // Poll room for subscribed video tracks (SessionManager owns the delegate;
    // we discover tracks via room->remoteParticipants() and their publications)
    if (!renderer_initialized && room) {
      for (const auto &rp : room->remoteParticipants()) {
        for (const auto &[sid, pub] : rp->trackPublications()) {
          if (!pub->subscribed() || pub->kind() != TrackKind::KIND_VIDEO) {
            continue;
          }
          if (pub->source() != TrackSource::SOURCE_CAMERA) {
            continue;
          }
          auto track = pub->track();
          if (!track) {
            continue;
          }

          VideoStream::Options opts;
          opts.format = VideoBufferType::RGBA;
          auto video_stream = VideoStream::fromTrack(track, opts);
          if (video_stream && media.initRenderer(video_stream)) {
            std::cout << "[video_viewer] Subscribed to video track from "
                      << rp->identity() << "\n";
            renderer_initialized = true;
            break;
          }
        }
        if (renderer_initialized) {
          break;
        }
      }
    }

    media.render();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  LK_LOG_INFO("[video_viewer] Shutting down...");
  media.shutdownRenderer();
  sm.disconnect();
  SDL_Quit();
  LK_LOG_INFO("[video_viewer] Done.");
  return 0;
}
