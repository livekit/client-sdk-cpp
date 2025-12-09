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

#include "ffi_handle.h"
#include "participant.h"
#include "track.h"
#include "video_frame.h"
#include "video_source.h"

namespace livekit {

// A single video frame event delivered by VideoStream::read().
struct VideoFrameEvent {
  LKVideoFrame frame;
  std::int64_t timestamp_us;
  VideoRotation rotation;
};

namespace proto {
class FfiEvent;
}

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
//
class VideoStream {
public:
  struct Options {
    // Maximum number of VideoFrameEvent items buffered in the internal queue.
    // 0 means "unbounded" (the queue can grow without limit).
    //
    // With a non-zero capacity, the queue behaves like a ring-buffer: if it
    // is full, the oldest frame is dropped when a new one arrives.
    std::size_t capacity{0};

    // Preferred pixel format for frames delivered by read(). The FFI layer
    // converts into this format if supported (e.g., RGBA, BGRA, I420, ...).
    VideoBufferType format{VideoBufferType::RGBA};
  };

  // Factory: create a VideoStream bound to a specific Track
  static std::shared_ptr<VideoStream>
  fromTrack(const std::shared_ptr<Track> &track, const Options &options);

  // Factory: create a VideoStream from a Participant + TrackSource
  static std::shared_ptr<VideoStream> fromParticipant(Participant &participant,
                                                      TrackSource track_source,
                                                      const Options &options);

  virtual ~VideoStream();

  VideoStream(const VideoStream &) = delete;
  VideoStream &operator=(const VideoStream &) = delete;
  VideoStream(VideoStream &&) noexcept;
  VideoStream &operator=(VideoStream &&) noexcept;

  /// Blocking read: waits until a VideoFrameEvent is available in the internal
  /// queue, or the stream reaches EOS / is closed.
  ///
  /// \param out  On success, filled with the next video frame event.
  /// \return true if a frame was delivered; false if the stream ended
  ///         (end-of-stream or close()) and no more data is available.
  bool read(VideoFrameEvent &out);

  /// Signal that we are no longer interested in video frames.
  ///
  /// This disposes the underlying FFI video stream, unregisters the listener
  /// from FfiClient, marks the stream as closed, and wakes any blocking read().
  /// After calling close(), further calls to read() will return false.
  void close();

private:
  VideoStream() = default;

  // Internal init helpers, used by the factories
  void initFromTrack(const std::shared_ptr<Track> &track,
                     const Options &options);
  void initFromParticipant(Participant &participant, TrackSource source,
                           const Options &options);

  // FFI event handler (registered with FfiClient)
  void onFfiEvent(const proto::FfiEvent &event);

  // Queue helpers
  void pushFrame(VideoFrameEvent &&ev);
  void pushEos();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<VideoFrameEvent> queue_;
  std::size_t capacity_{0};
  bool eof_{false};
  bool closed_{false};

  // Underlying FFI handle for the video stream
  FfiHandle stream_handle_;

  // Listener id registered on FfiClient
  std::int64_t listener_id_{0};
};

} // namespace livekit
