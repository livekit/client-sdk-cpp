/*
 * Copyright 2025 LiveKit, Inc.
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

#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <functional>
#include <vector>

// -------------------------
// SDLMicSource
// -------------------------
// Periodically call pump() from your main loop or a capture thread.
// It will pull 10ms frames from the mic (by default) and pass them to the
// AudioCallback.
class SDLMicSource {
public:
  using AudioCallback = std::function<void(
      const int16_t *samples, // interleaved
      int num_samples_per_channel, int sample_rate, int num_channels)>;

  SDLMicSource(int sample_rate = 48000, int channels = 1,
               int frame_samples = 480, AudioCallback cb = nullptr);

  ~SDLMicSource();

  // Initialize SDL audio stream for recording
  bool init();

  // Call regularly to pull mic data and send to callback.
  void pump();

  void pause();
  void resume();

  bool isValid() const { return stream_ != nullptr; }

private:
  SDL_AudioStream *stream_ = nullptr;
  SDL_AudioSpec spec_{};
  int sample_rate_;
  int channels_;
  int frame_samples_;
  AudioCallback callback_;
};

// -------------------------
// DDLSpeakerSink
// -------------------------
// For remote audio: when you get a decoded PCM frame,
// call enqueue() with interleaved S16 samples.
class DDLSpeakerSink {
public:
  DDLSpeakerSink(int sample_rate = 48000, int channels = 1);

  ~DDLSpeakerSink();

  bool init();

  // Enqueue interleaved S16 samples for playback.
  void enqueue(const int16_t *samples, int num_samples_per_channel);

  void pause();
  void resume();

  bool isValid() const { return stream_ != nullptr; }

private:
  SDL_AudioStream *stream_ = nullptr;
  SDL_AudioSpec spec_{};
  int sample_rate_;
  int channels_;
};

// -------------------------
// SDLCamSource
// -------------------------
// Periodically call pump(); each time a new frame is available
// it will invoke the VideoCallback with the raw pixels.
//
// NOTE: pixels are in the SDL_Surface format returned by the camera
// (often SDL_PIXELFORMAT_ARGB8888). You can either:
//  - convert to whatever your LiveKit video source expects, or
//  - tell LiveKit that this is ARGB with the given stride.
class SDLCamSource {
public:
  using VideoCallback = std::function<void(
      const uint8_t *pixels,
      int pitch, // bytes per row
      int width, int height, SDL_PixelFormat format, Uint64 timestampNS)>;

  SDLCamSource(int desired_width = 1280, int desired_height = 720,
               int desired_fps = 30,
               SDL_PixelFormat pixelFormat = SDL_PIXELFORMAT_RGBA8888,
               VideoCallback cb = nullptr);

  ~SDLCamSource();

  bool init(); // open first available camera with (approximately) given spec

  // Call regularly; will call VideoCallback when a frame is available.
  void pump();

  bool isValid() const { return camera_ != nullptr; }

private:
  SDL_Camera *camera_ = nullptr;
  SDL_CameraSpec spec_{};
  int width_;
  int height_;
  int fps_;
  SDL_PixelFormat format_;
  VideoCallback callback_;
};
