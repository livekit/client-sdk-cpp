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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <SDL3/SDL.h>
#include <atomic>
#include <memory>
#include <thread>

namespace livekit {
class VideoStream;
}

class SDLVideoRenderer {
public:
  SDLVideoRenderer();
  ~SDLVideoRenderer();

  // Must be called on main thread, after SDL_Init(SDL_INIT_VIDEO).
  bool init(const char *title, int width, int height);

  // Set/replace the stream to render. Safe to call from main thread.
  void setStream(std::shared_ptr<livekit::VideoStream> stream);

  // Called on main thread each tick to pump events and draw latest frame.
  void render();

  void shutdown(); // destroy window/renderer/texture

private:
  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  SDL_Texture *texture_ = nullptr;

  std::shared_ptr<livekit::VideoStream> stream_;
  int width_ = 0;
  int height_ = 0;
};
