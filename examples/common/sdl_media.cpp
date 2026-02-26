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

#include "sdl_media.h"

#include <iostream>

// ---------------------- SDLMicSource -----------------------------

SDLMicSource::SDLMicSource(int sample_rate, int channels, int frame_samples,
                           AudioCallback cb)
    : sample_rate_(sample_rate), channels_(channels),
      frame_samples_(frame_samples), callback_(std::move(cb)) {}

SDLMicSource::~SDLMicSource() {
  if (stream_) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
}

bool SDLMicSource::init() {
  // desired output (what SDL will give us when we call SDL_GetAudioStreamData)
  SDL_zero(spec_);
  spec_.format = SDL_AUDIO_S16; // 16-bit signed
  spec_.channels = static_cast<Uint8>(channels_);
  spec_.freq = sample_rate_;

  // Open default recording device as an audio stream
  // This works for both playback and recording, depending on the device id.
  stream_ = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_RECORDING, // recording device
      &spec_,
      nullptr, // no callback, we'll poll
      nullptr);

  if (!stream_) {
    std::cerr << "Failed to open recording stream: " << SDL_GetError() << "\n";
    return false;
  }

  if (!SDL_ResumeAudioStreamDevice(stream_)) { // unpause device
    std::cerr << "Failed to resume recording device: " << SDL_GetError()
              << "\n";
    return false;
  }

  return true;
}

void SDLMicSource::pump() {
  if (!stream_ || !callback_)
    return;

  const int samples_per_frame_total = frame_samples_ * channels_;
  const int bytes_per_frame = samples_per_frame_total * sizeof(int16_t);

  // Only pull if at least one "frame" worth of audio is available
  const int available = SDL_GetAudioStreamAvailable(stream_); // bytes
  if (available < bytes_per_frame) {
    return;
  }

  std::vector<int16_t> buffer(samples_per_frame_total);

  const int got_bytes = SDL_GetAudioStreamData(stream_, buffer.data(),
                                               bytes_per_frame); //

  if (got_bytes <= 0) {
    return; // nothing or error (log if you like)
  }

  const int got_samples_total = got_bytes / sizeof(int16_t);
  const int got_samples_per_channel = got_samples_total / channels_;

  callback_(buffer.data(), got_samples_per_channel, sample_rate_, channels_);
}

void SDLMicSource::pause() {
  if (stream_) {
    SDL_PauseAudioStreamDevice(stream_); //
  }
}

void SDLMicSource::resume() {
  if (stream_) {
    SDL_ResumeAudioStreamDevice(stream_); //
  }
}

// ---------------------- DDLSpeakerSink -----------------------------

DDLSpeakerSink::DDLSpeakerSink(int sample_rate, int channels)
    : sample_rate_(sample_rate), channels_(channels) {}

DDLSpeakerSink::~DDLSpeakerSink() {
  if (stream_) {
    SDL_DestroyAudioStream(stream_); // also closes device
    stream_ = nullptr;
  }
}

bool DDLSpeakerSink::init() {
  SDL_zero(spec_);
  spec_.format = SDL_AUDIO_S16; // expect S16 input for playback
  spec_.channels = static_cast<Uint8>(channels_);
  spec_.freq = sample_rate_;

  // Open default playback device as a stream.
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec_,
                                      nullptr, // no callback; we'll push data
                                      nullptr);

  if (!stream_) {
    std::cerr << "Failed to open playback stream: " << SDL_GetError() << "\n";
    return false;
  }

  if (!SDL_ResumeAudioStreamDevice(stream_)) {
    std::cerr << "Failed to resume playback device: " << SDL_GetError() << "\n";
    return false;
  }

  return true;
}

void DDLSpeakerSink::enqueue(const int16_t *samples,
                             int num_samples_per_channel) {
  if (!stream_ || !samples)
    return;

  const int totalSamples = num_samples_per_channel * channels_;
  const int bytes = totalSamples * static_cast<int>(sizeof(int16_t));

  // SDL will resample / convert as needed on SDL_GetAudioStreamData() side.
  if (!SDL_PutAudioStreamData(stream_, samples, bytes)) {
    std::cerr << "SDL_PutAudioStreamData failed: " << SDL_GetError() << "\n";
  }
}

void DDLSpeakerSink::pause() {
  if (stream_) {
    SDL_PauseAudioStreamDevice(stream_);
  }
}

void DDLSpeakerSink::resume() {
  if (stream_) {
    SDL_ResumeAudioStreamDevice(stream_);
  }
}

// ---------------------- SDLCamSource -----------------------------

SDLCamSource::SDLCamSource(int desired_width, int desired_height,
                           int desired_fps, SDL_PixelFormat pixel_format,
                           VideoCallback cb)
    : width_(desired_width), height_(desired_height), fps_(desired_fps),
      format_(pixel_format), callback_(std::move(cb)) {}

SDLCamSource::~SDLCamSource() {
  if (camera_) {
    SDL_CloseCamera(camera_); //
    camera_ = nullptr;
  }
}

bool SDLCamSource::init() {
  int count = 0;
  SDL_CameraID *cams = SDL_GetCameras(&count); //
  if (!cams || count == 0) {
    std::cerr << "No cameras available: " << SDL_GetError() << "\n";
    if (cams)
      SDL_free(cams);
    return false;
  }

  SDL_CameraID camId = cams[0]; // first camera for now
  SDL_free(cams);

  SDL_zero(spec_);
  spec_.format = format_;
  spec_.colorspace = SDL_COLORSPACE_SRGB;
  spec_.width = width_;
  spec_.height = height_;
  spec_.framerate_numerator = fps_;
  spec_.framerate_denominator = 1;

  camera_ = SDL_OpenCamera(camId, &spec_);
  if (!camera_) {
    std::cerr << "Failed to open camera: " << SDL_GetError() << "\n";
    return false;
  }

  // On many platforms you must wait for SDL_EVENT_CAMERA_DEVICE_APPROVED;
  // here we assume the app’s main loop is already handling that.
  return true;
}

void SDLCamSource::pump() {
  if (!camera_ || !callback_)
    return;

  Uint64 tsNS = 0;
  SDL_Surface *surf = SDL_AcquireCameraFrame(camera_, &tsNS); // non-blocking
  if (!surf) {
    return;
  }

  callback_(static_cast<uint8_t *>(surf->pixels), surf->pitch, surf->w, surf->h,
            surf->format, tsNS);

  SDL_ReleaseCameraFrame(camera_, surf); //
}
