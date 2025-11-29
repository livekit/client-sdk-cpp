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

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "audio_frame.h"
#include "ffi_handle.h"
#include "participant.h"
#include "track.h"

namespace livekit {

namespace proto {
class FfiEvent;
}

struct AudioFrameEvent {
  AudioFrame frame;
};

class AudioStream {
public:
  struct Options {
    std::size_t capacity{0}; // 0 = unbounded
    int sample_rate{48000};
    int num_channels{1};
    std::string noise_cancellation_module;       // empty = disabled
    std::string noise_cancellation_options_json; // empty = no options
  };

  // Factory: create an AudioStream bound to a specific Track
  static std::unique_ptr<AudioStream>
  from_track(const std::shared_ptr<Track> &track, const Options &options);

  // Factory: create an AudioStream from a Participant + TrackSource
  static std::unique_ptr<AudioStream> from_participant(Participant &participant,
                                                       TrackSource track_source,
                                                       const Options &options);

  ~AudioStream();

  AudioStream(const AudioStream &) = delete;
  AudioStream &operator=(const AudioStream &) = delete;
  AudioStream(AudioStream &&) noexcept;
  AudioStream &operator=(AudioStream &&) noexcept;

  /// Blocking read: returns true if a frame was delivered,
  /// false if the stream has ended (EOS or closed).
  bool read(AudioFrameEvent &out_event);

  /// Signal that we are no longer interested in frames.
  /// Disposes the underlying FFI stream and removes the listener.
  void close();

private:
  AudioStream() = default;

  void init_from_track(const std::shared_ptr<Track> &track,
                       const Options &options);
  void init_from_participant(Participant &participant, TrackSource track_source,
                             const Options &options);

  // FFI event handler (registered with FfiClient)
  void on_ffi_event(const proto::FfiEvent &event);

  // Queue helpers
  void push_frame(AudioFrameEvent &&ev);
  void push_eos();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<AudioFrameEvent> queue_;
  std::size_t capacity_{0};
  bool eof_{false};
  bool closed_{false};

  Options options_;

  // Underlying FFI audio stream handle
  FfiHandle stream_handle_;

  // Listener id registered on FfiClient
  std::int64_t listener_id_{0};
};

} // namespace livekit
