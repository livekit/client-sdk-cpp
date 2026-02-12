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

#include <atomic>
#include <memory>
#include <thread>

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_camera.h>

#include "wav_audio_source.h"

namespace livekit {
class AudioSource;
class VideoSource;
class AudioStream;
class VideoStream;
} // namespace livekit

// Forward-declared SDL helpers (you can also keep these separate if you like)
class SDLMicSource;
class SDLCamSource;
class SDLVideoRenderer;

// SDLMediaManager gives you dedicated control over:
// - mic capture  -> AudioSource
// - camera capture -> VideoSource
// - speaker playback -> AudioStream (TODO: integrate your API)
// - renderer -> VideoStream (TODO: integrate your API)
class SDLMediaManager {
public:
  SDLMediaManager();
  ~SDLMediaManager();

  // Mic (local capture -> AudioSource)
  bool startMic(const std::shared_ptr<livekit::AudioSource> &audio_source);
  void stopMic();

  // Camera (local capture -> VideoSource)
  bool startCamera(const std::shared_ptr<livekit::VideoSource> &video_source);
  void stopCamera();

  // Speaker (remote audio playback)
  bool startSpeaker(const std::shared_ptr<livekit::AudioStream> &audio_stream);
  void stopSpeaker();

  // Renderer (remote video rendering)
  // Following APIs must be called on main thread
  bool initRenderer(const std::shared_ptr<VideoStream> &video_stream);
  void shutdownRenderer();
  void render();

private:
  // ---- SDL bootstrap helpers ----
  bool ensureSDLInit(Uint32 flags);

  // ---- Mic helpers ----
  void micLoopSDL();
  void micLoopNoise();

  // ---- Camera helpers ----
  void cameraLoopSDL();
  void cameraLoopFake();

  // ---- Speaker helpers (TODO: wire AudioStream -> SDL audio) ----
  void speakerLoopSDL();

  // Mic
  std::shared_ptr<livekit::AudioSource> mic_source_;
  std::unique_ptr<SDLMicSource> mic_sdl_;
  std::thread mic_thread_;
  std::atomic<bool> mic_running_{false};
  bool mic_using_sdl_ = false;

  // Camera
  std::shared_ptr<livekit::VideoSource> cam_source_;
  std::unique_ptr<SDLCamSource> can_sdl_;
  std::thread cam_thread_;
  std::atomic<bool> cam_running_{false};
  bool cam_using_sdl_ = false;

  // Speaker (remote audio) – left mostly as a placeholder
  std::shared_ptr<livekit::AudioStream> speaker_stream_;
  std::thread speaker_thread_;
  std::atomic<bool> speaker_running_{false};
  SDL_AudioStream *sdl_audio_stream_ = nullptr;

  // Renderer (remote video) – left mostly as a placeholder
  std::unique_ptr<SDLVideoRenderer> sdl_renderer_;
  std::shared_ptr<livekit::VideoStream> renderer_stream_;
  std::thread renderer_thread_;
  std::atomic<bool> renderer_running_{false};
};
