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

// Represents a pull-based stream of decoded PCM audio frames coming from
// a remote (or local) LiveKit track. Similar to VideoStream, but for audio.
//
// Typical usage:
//
//   AudioStream::Options opts;
//   auto stream = AudioStream::fromTrack(remoteAudioTrack, opts);
//
//   AudioFrameEvent ev;
//   while (stream->read(ev)) {
//     // ev.frame contains interleaved int16 PCM samples
//   }
//
//   stream->close();  // optional, called automatically in destructor
class AudioStream {
public:
  // Configuration options for AudioStream creation.
  struct Options {
    // Maximum number of AudioFrameEvent items buffered in the internal queue.
    // 0 means "unbounded" (the queue can grow without limit).
    //
    // Using a small non-zero capacity gives ring-buffer semantics:
    // if the queue is full, the oldest frame is dropped when a new one arrives.
    std::size_t capacity{0};

    // Optional: name of a noise cancellation module to enable for this stream.
    // Empty string means "no noise cancellation".
    std::string noise_cancellation_module;

    // Optional: JSON-encoded configuration for the noise cancellation module.
    // Empty string means "use module defaults".
    std::string noise_cancellation_options_json;
  };

  // Factory: create an AudioStream bound to a specific Track
  static std::shared_ptr<AudioStream>
  fromTrack(const std::shared_ptr<Track> &track, const Options &options);

  // Factory: create an AudioStream from a Participant + TrackSource
  static std::shared_ptr<AudioStream> fromParticipant(Participant &participant,
                                                      TrackSource track_source,
                                                      const Options &options);

  ~AudioStream();

  AudioStream(const AudioStream &) = delete;
  AudioStream &operator=(const AudioStream &) = delete;
  AudioStream(AudioStream &&) noexcept;
  AudioStream &operator=(AudioStream &&) noexcept;

  /// Blocking read: waits until there is an AudioFrameEvent available in the
  /// internal queue, or the stream reaches EOS / is closed.
  ///
  /// \param out_event  On success, filled with the next audio frame.
  /// \return true if a frame was delivered; false if the stream ended
  ///         (end-of-stream or close()) and no more data is available.
  bool read(AudioFrameEvent &out_event);

  /// Signal that we are no longer interested in audio frames.
  ///
  /// This disposes the underlying FFI audio stream, unregisters the listener
  /// from FfiClient, marks the stream as closed, and wakes any blocking read().
  /// After calling close(), further calls to read() will return false.
  void close();

private:
  AudioStream() = default;

  void initFromTrack(const std::shared_ptr<Track> &track,
                     const Options &options);
  void initFromParticipant(Participant &participant, TrackSource track_source,
                           const Options &options);

  // FFI event handler (registered with FfiClient)
  void onFfiEvent(const proto::FfiEvent &event);

  // Queue helpers
  void pushFrame(AudioFrameEvent &&ev);
  void pushEos();

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
