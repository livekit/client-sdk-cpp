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

#include "sdl_media_manager.h"

#include "fallback_capture.h"
#include "livekit/livekit.h"
#include "sdl_media.h"
#include "sdl_video_renderer.h"
#include <cstring>
#include <iostream>
#include <vector>
using namespace livekit;

SDLMediaManager::SDLMediaManager() = default;

SDLMediaManager::~SDLMediaManager() {
  stopMic();
  stopCamera();
  stopSpeaker();
  shutdownRenderer();
}

bool SDLMediaManager::ensureSDLInit(Uint32 flags) {
  if ((SDL_WasInit(flags) & flags) == flags) {
    return true; // already init
  }
  if (!SDL_InitSubSystem(flags)) {
    std::cerr << "SDL_InitSubSystem failed (flags=" << flags
              << "): " << SDL_GetError() << std::endl;
    return false;
  }
  return true;
}

// ---------- Mic control ----------

bool SDLMediaManager::startMic(
    const std::shared_ptr<AudioSource> &audio_source) {
  stopMic();

  if (!audio_source) {
    std::cerr << "startMic: audioSource is null\n";
    return false;
  }

  mic_source_ = audio_source;
  mic_running_.store(true, std::memory_order_relaxed);

  // Try SDL path
  if (!ensureSDLInit(SDL_INIT_AUDIO)) {
    std::cerr << "No SDL audio, falling back to noise loop.\n";
    mic_using_sdl_ = false;
    mic_thread_ =
        std::thread(runNoiseCaptureLoop, mic_source_, std::ref(mic_running_));
    return true;
  }

  int recCount = 0;
  SDL_AudioDeviceID *recDevs = SDL_GetAudioRecordingDevices(&recCount);
  if (!recDevs || recCount == 0) {
    std::cerr << "No microphone devices found, falling back to noise loop.\n";
    if (recDevs)
      SDL_free(recDevs);
    mic_using_sdl_ = false;
    mic_thread_ =
        std::thread(runNoiseCaptureLoop, mic_source_, std::ref(mic_running_));
    return true;
  }
  SDL_free(recDevs);

  // We have at least one mic; use SDL
  mic_using_sdl_ = true;

  mic_sdl_ = std::make_unique<SDLMicSource>(
      mic_source_->sample_rate(), mic_source_->num_channels(),
      mic_source_->sample_rate() / 100, // ~10ms
      [src = mic_source_](const int16_t *samples, int num_samples_per_channel,
                          int sample_rate, int num_channels) {
        AudioFrame frame = AudioFrame::create(sample_rate, num_channels,
                                              num_samples_per_channel);
        std::memcpy(frame.data().data(), samples,
                    num_samples_per_channel * num_channels * sizeof(int16_t));
        try {
          src->captureFrame(frame);
        } catch (const std::exception &e) {
          std::cerr << "Error in captureFrame (SDL mic): " << e.what()
                    << std::endl;
        }
      });

  if (!mic_sdl_->init()) {
    std::cerr << "Failed to init SDL mic, falling back to noise loop.\n";
    mic_using_sdl_ = false;
    mic_sdl_.reset();
    mic_thread_ =
        std::thread(runNoiseCaptureLoop, mic_source_, std::ref(mic_running_));
    return true;
  }

  mic_thread_ = std::thread(&SDLMediaManager::micLoopSDL, this);
  return true;
}

void SDLMediaManager::micLoopSDL() {
  while (mic_running_.load(std::memory_order_relaxed)) {
    mic_sdl_->pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void SDLMediaManager::stopMic() {
  mic_running_.store(false, std::memory_order_relaxed);
  if (mic_thread_.joinable()) {
    mic_thread_.join();
  }
  mic_sdl_.reset();
  mic_source_.reset();
}

// ---------- Camera control ----------

bool SDLMediaManager::startCamera(
    const std::shared_ptr<VideoSource> &video_source) {
  stopCamera();

  if (!video_source) {
    std::cerr << "startCamera: videoSource is null\n";
    return false;
  }

  cam_source_ = video_source;
  cam_running_.store(true, std::memory_order_relaxed);

  // Try SDL
  if (!ensureSDLInit(SDL_INIT_CAMERA)) {
    std::cerr << "No SDL camera subsystem, using fake video loop.\n";
    cam_using_sdl_ = false;
    cam_thread_ = std::thread(runFakeVideoCaptureLoop, cam_source_,
                              std::ref(cam_running_));
    return true;
  }

  int camCount = 0;
  SDL_CameraID *cams = SDL_GetCameras(&camCount);
  if (!cams || camCount == 0) {
    std::cerr << "No camera devices found, using fake video loop.\n";
    if (cams)
      SDL_free(cams);
    cam_using_sdl_ = false;
    cam_thread_ = std::thread(runFakeVideoCaptureLoop, cam_source_,
                              std::ref(cam_running_));
    return true;
  }
  SDL_free(cams);

  cam_using_sdl_ = true;
  can_sdl_ = std::make_unique<SDLCamSource>(
      1280, 720, 30,
      SDL_PIXELFORMAT_RGBA32, // Note SDL_PIXELFORMAT_RGBA8888 is not compatable
                              // with Livekit RGBA format.
      [src = cam_source_](const uint8_t *pixels, int pitch, int width,
                          int height, SDL_PixelFormat /*fmt*/,
                          Uint64 timestampNS) {
        auto frame = VideoFrame::create(width, height, VideoBufferType::RGBA);
        uint8_t *dst = frame.data();
        const int dstPitch = width * 4;

        for (int y = 0; y < height; ++y) {
          std::memcpy(dst + y * dstPitch, pixels + y * pitch, dstPitch);
        }

        try {
          src->captureFrame(frame, timestampNS / 1000,
                            VideoRotation::VIDEO_ROTATION_0);
        } catch (const std::exception &e) {
          std::cerr << "Error in captureFrame (SDL cam): " << e.what()
                    << std::endl;
        }
      });

  if (!can_sdl_->init()) {
    std::cerr << "Failed to init SDL camera, using fake video loop.\n";
    cam_using_sdl_ = false;
    can_sdl_.reset();
    cam_thread_ = std::thread(runFakeVideoCaptureLoop, cam_source_,
                              std::ref(cam_running_));
    return true;
  }

  cam_thread_ = std::thread(&SDLMediaManager::cameraLoopSDL, this);
  return true;
}

void SDLMediaManager::cameraLoopSDL() {
  while (cam_running_.load(std::memory_order_relaxed)) {
    can_sdl_->pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void SDLMediaManager::stopCamera() {
  cam_running_.store(false, std::memory_order_relaxed);
  if (cam_thread_.joinable()) {
    cam_thread_.join();
  }
  can_sdl_.reset();
  cam_source_.reset();
}

// ---------- Speaker control (placeholder) ----------

bool SDLMediaManager::startSpeaker(
    const std::shared_ptr<AudioStream> &audio_stream) {
  stopSpeaker();

  if (!audio_stream) {
    std::cerr << "startSpeaker: audioStream is null\n";
    return false;
  }

  if (!ensureSDLInit(SDL_INIT_AUDIO)) {
    std::cerr << "startSpeaker: SDL_INIT_AUDIO failed\n";
    return false;
  }

  speaker_stream_ = audio_stream;
  speaker_running_.store(true, std::memory_order_relaxed);

  // Note, we don't open the speaker since the format is unknown yet.
  // Instead, open the speaker in the speakerLoopSDL thread with the native
  // format.
  try {
    speaker_thread_ = std::thread(&SDLMediaManager::speakerLoopSDL, this);
  } catch (const std::exception &e) {
    std::cerr << "startSpeaker: failed to start speaker thread: " << e.what()
              << "\n";
    speaker_running_.store(false, std::memory_order_relaxed);
    speaker_stream_.reset();
    return false;
  }

  return true;
}

void SDLMediaManager::speakerLoopSDL() {
  SDL_AudioStream *localStream = nullptr;
  SDL_AudioDeviceID dev = 0;

  while (speaker_running_.load(std::memory_order_relaxed)) {
    if (!speaker_stream_) {
      break;
    }

    livekit::AudioFrameEvent ev;
    if (!speaker_stream_->read(ev)) {
      // EOS or closed
      break;
    }

    const livekit::AudioFrame &frame = ev.frame;
    const auto &data = frame.data();
    if (data.empty()) {
      continue;
    }

    // Lazily open SDL audio stream based on the first frame's format, so no
    // resampler is needed.
    if (!localStream) {
      SDL_AudioSpec want{};
      want.format = SDL_AUDIO_S16;
      want.channels = static_cast<Uint8>(frame.num_channels());
      want.freq = frame.sample_rate();

      localStream =
          SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want,
                                    /*callback=*/nullptr,
                                    /*userdata=*/nullptr);

      if (!localStream) {
        std::cerr << "speakerLoopSDL: SDL_OpenAudioDeviceStream failed: "
                  << SDL_GetError() << "\n";
        break;
      }

      sdl_audio_stream_ = localStream; // store if you want to inspect later

      dev = SDL_GetAudioStreamDevice(localStream);
      if (dev == 0) {
        std::cerr << "speakerLoopSDL: SDL_GetAudioStreamDevice failed: "
                  << SDL_GetError() << "\n";
        break;
      }

      if (!SDL_ResumeAudioDevice(dev)) {
        std::cerr << "speakerLoopSDL: SDL_ResumeAudioDevice failed: "
                  << SDL_GetError() << "\n";
        break;
      }
    }

    // Push PCM to SDL. We assume frames are already S16, interleaved, matching
    // sample_rate / channels we used above.
    const int numBytes = static_cast<int>(data.size() * sizeof(std::int16_t));

    if (!SDL_PutAudioStreamData(localStream, data.data(), numBytes)) {
      std::cerr << "speakerLoopSDL: SDL_PutAudioStreamData failed: "
                << SDL_GetError() << "\n";
      break;
    }

    // Tiny sleep to avoid busy loop; SDL buffers internally.
    SDL_Delay(2);
  }

  if (localStream) {
    SDL_DestroyAudioStream(localStream);
    localStream = nullptr;
    sdl_audio_stream_ = nullptr;
  }

  speaker_running_.store(false, std::memory_order_relaxed);
}

void SDLMediaManager::stopSpeaker() {
  speaker_running_.store(false, std::memory_order_relaxed);
  if (speaker_thread_.joinable()) {
    speaker_thread_.join();
  }
  if (sdl_audio_stream_) {
    SDL_DestroyAudioStream(sdl_audio_stream_);
    sdl_audio_stream_ = nullptr;
  }
  speaker_stream_.reset();
}

// ---------- Renderer control (placeholder) ----------

bool SDLMediaManager::initRenderer(
    const std::shared_ptr<VideoStream> &video_stream) {
  if (!video_stream) {
    std::cerr << "startRenderer: videoStream is null\n";
    return false;
  }
  // Ensure SDL video subsystem is initialized
  if (!ensureSDLInit(SDL_INIT_VIDEO)) {
    std::cerr << "startRenderer: SDL_INIT_VIDEO failed\n";
    return false;
  }
  renderer_stream_ = video_stream;
  renderer_running_.store(true, std::memory_order_relaxed);

  // Lazily create the SDLVideoRenderer
  if (!sdl_renderer_) {
    sdl_renderer_ = std::make_unique<SDLVideoRenderer>();
    // You can tune these dimensions or even make them options
    if (!sdl_renderer_->init("LiveKit Remote Video", 1280, 720)) {
      std::cerr << "startRenderer: SDLVideoRenderer::init failed\n";
      sdl_renderer_.reset();
      renderer_stream_.reset();
      renderer_running_.store(false, std::memory_order_relaxed);
      return false;
    }
  }

  // Start the SDL renderer's own render thread
  sdl_renderer_->setStream(renderer_stream_);

  return true;
}

void SDLMediaManager::shutdownRenderer() {
  renderer_running_.store(false, std::memory_order_relaxed);

  // Shut down SDL renderer thread if it exists
  if (sdl_renderer_) {
    sdl_renderer_->shutdown();
  }

  // Old renderer_thread_ is no longer used, but if you still have it:
  if (renderer_thread_.joinable()) {
    renderer_thread_.join();
  }

  renderer_stream_.reset();
}

void SDLMediaManager::render() {
  if (renderer_running_.load(std::memory_order_relaxed) && sdl_renderer_) {
    sdl_renderer_->render();
  }
}