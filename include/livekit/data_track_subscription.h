/*
 * Copyright 2026 LiveKit
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

#include "livekit/data_frame.h"
#include "livekit/ffi_handle.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

namespace livekit {

namespace proto {
class FfiEvent;
}

/**
 * An active subscription to a remote data track.
 *
 * Provides a blocking read() interface similar to AudioStream / VideoStream.
 * Frames are delivered via FfiEvent callbacks and queued internally.
 *
 * Dropping (destroying) the subscription automatically unsubscribes from the
 * remote track by releasing the underlying FFI handle.
 *
 * Typical usage:
 *
 *   auto sub = remoteDataTrack->subscribe();
 *   DataFrame frame;
 *   while (sub->read(frame)) {
 *     // process frame.payload
 *   }
 */
class DataTrackSubscription {
public:
  struct Options {
    /// Maximum buffered frames (count). 0 = unbounded.
    /// When non-zero, behaves as a ring buffer (oldest dropped on overflow).
    std::size_t capacity{0};
  };

  virtual ~DataTrackSubscription();

  DataTrackSubscription(const DataTrackSubscription &) = delete;
  DataTrackSubscription &operator=(const DataTrackSubscription &) = delete;
  DataTrackSubscription(DataTrackSubscription &&) noexcept;
  DataTrackSubscription &operator=(DataTrackSubscription &&) noexcept;

  /**
   * Blocking read: waits until a DataFrame is available, or the
   * subscription reaches EOS / is closed.
   *
   * @param out  On success, filled with the next data frame.
   * @return true if a frame was delivered; false if the subscription ended.
   */
  bool read(DataFrame &out);

  /**
   * End the subscription early.
   *
   * Releases the FFI handle (which unsubscribes from the remote track),
   * unregisters the event listener, and wakes any blocking read().
   */
  void close();

private:
  friend class RemoteDataTrack;

  DataTrackSubscription() = default;
  /// Internal init helper, called by RemoteDataTrack.
  void init(FfiHandle subscription_handle, const Options &options);

  /// FFI event handler, called by FfiClient.
  void onFfiEvent(const proto::FfiEvent &event);

  /// Push a received DataFrame to the internal queue.
  void pushFrame(DataFrame &&frame);

  /// Push an end-of-stream signal (EOS).
  void pushEos();

  /** Protects all mutable state below. */
  mutable std::mutex mutex_;

  /** Signalled when a frame is pushed or the subscription ends. */
  std::condition_variable cv_;

  /** FIFO of received frames awaiting read(). */
  std::deque<DataFrame> queue_;

  /** Max buffered frames (0 = unbounded). Oldest dropped on overflow. */
  std::size_t capacity_{0};

  /** True once the remote side signals end-of-stream. */
  bool eof_{false};

  /** True after close() has been called by the consumer. */
  bool closed_{false};

  /** RAII handle for the Rust-owned subscription resource. */
  FfiHandle subscription_handle_;

  /** FfiClient listener id for routing FfiEvent callbacks to this object. */
  std::int64_t listener_id_{0};
};

} // namespace livekit
