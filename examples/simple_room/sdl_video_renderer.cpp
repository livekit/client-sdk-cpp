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

#include "sdl_video_renderer.h"

#include "livekit/livekit.h"
#include <cstring>
#include <iostream>

using namespace livekit;

SDLVideoRenderer::SDLVideoRenderer() = default;

SDLVideoRenderer::~SDLVideoRenderer() { shutdown(); }

bool SDLVideoRenderer::init(const char *title, int width, int height) {
  width_ = width;
  height_ = height;

  // Assume SDL_Init(SDL_INIT_VIDEO) already called in main()
  window_ = SDL_CreateWindow(title, width_, height_, 0);
  if (!window_) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    return false;
  }

  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if (!renderer_) {
    std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
    return false;
  }

  // Note, web will send out BGRA as default, and we can't use ARGB since ffi
  // does not support converting from BGRA to ARGB.
  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                               SDL_TEXTUREACCESS_STREAMING, width_, height_);
  if (!texture_) {
    std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << "\n";
    return false;
  }

  return true;
}

void SDLVideoRenderer::shutdown() {
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  stream_.reset();
}

void SDLVideoRenderer::setStream(std::shared_ptr<livekit::VideoStream> stream) {
  stream_ = std::move(stream);
}

void SDLVideoRenderer::render() {
  // 0) Basic sanity
  if (!window_ || !renderer_) {
    return;
  }

  // 1) Pump SDL events on the main thread
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_QUIT) {
      // TODO: set some global or member flag if you want to quit the app
    }
  }

  // 2) If no stream, nothing to render
  if (!stream_) {
    return;
  }

  // 3) Read a frame from VideoStream (blocking until one is available)
  livekit::VideoFrameEvent vfe;
  bool gotFrame = stream_->read(vfe);
  if (!gotFrame) {
    // EOS / closed – nothing more to render
    return;
  }

  livekit::LKVideoFrame &frame = vfe.frame;

  // 4) Ensure the frame is RGBA.
  //    Ideally you requested RGBA from VideoStream::Options so this is a no-op.
  if (frame.type() != livekit::VideoBufferType::RGBA) {
    try {
      frame = frame.convert(livekit::VideoBufferType::RGBA, false);
    } catch (const std::exception &ex) {
      std::cerr << "SDLVideoRenderer: convert to RGBA failed: " << ex.what()
                << "\n";
      return;
    }
  }

  // Handle size change: recreate texture if needed
  if (frame.width() != width_ || frame.height() != height_) {
    width_ = frame.width();
    height_ = frame.height();

    if (texture_) {
      SDL_DestroyTexture(texture_);
      texture_ = nullptr;
    }
    texture_ = SDL_CreateTexture(
        renderer_,
        SDL_PIXELFORMAT_RGBA32, // Note, SDL_PIXELFORMAT_RGBA8888 is not
                                // compatible with Livekit RGBA format.
        SDL_TEXTUREACCESS_STREAMING, width_, height_);
    if (!texture_) {
      std::cerr << "SDLVideoRenderer: SDL_CreateTexture failed: "
                << SDL_GetError() << "\n";
      return;
    }
  }

  // 6) Upload RGBA data to SDL texture
  void *pixels = nullptr;
  int pitch = 0;
  if (!SDL_LockTexture(texture_, nullptr, &pixels, &pitch)) {
    std::cerr << "SDLVideoRenderer: SDL_LockTexture failed: " << SDL_GetError()
              << "\n";
    return;
  }

  const std::uint8_t *src = frame.data();
  const int srcPitch = frame.width() * 4; // RGBA: 4 bytes per pixel

  for (int y = 0; y < frame.height(); ++y) {
    std::memcpy(static_cast<std::uint8_t *>(pixels) + y * pitch,
                src + y * srcPitch, srcPitch);
  }

  SDL_UnlockTexture(texture_);

  // 7) Present
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
  SDL_RenderPresent(renderer_);
}
