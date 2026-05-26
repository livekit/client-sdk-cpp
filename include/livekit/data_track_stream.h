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

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

#include "livekit/data_track_error.h"
#include "livekit/data_track_frame.h"
#include "livekit/ffi_handle.h"
#include "livekit/visibility.h"

namespace livekit {

namespace proto {
class FfiEvent;
class DataTrackStreamReadResponse;
} // namespace proto

/// Represents a pull-based stream of frames from a remote data track.
///
/// Provides a blocking read() interface similar to AudioStream / VideoStream.
/// Frames are delivered via FfiEvent callbacks and stored internally.
///
/// Destroying the stream automatically unsubscribes from the remote track by
/// releasing the underlying FFI handle.
///
/// Typical usage:
///
///   auto sub_result = remoteDataTrack->subscribe();
///   if (sub_result) {
///     auto sub = sub_result.value();
///     DataTrackFrame frame;
///     while (sub->read(frame)) {
///       // process frame.payload
///     }
///   }
class LIVEKIT_API DataTrackStream {
public:
  struct Options {
    /// Maximum frames buffered on the Rust side. Rust defaults to 16.
    std::optional<std::uint32_t> buffer_size{std::nullopt};
  };

  virtual ~DataTrackStream();

  DataTrackStream(const DataTrackStream&) = delete;
  DataTrackStream& operator=(const DataTrackStream&) = delete;
  // The FFI listener captures `this`, so moving the object would leave the
  // registered callback pointing at the old address.
  DataTrackStream(DataTrackStream&&) noexcept = delete;
  // Instances are created and returned as std::shared_ptr, so value-move
  // support is not required by the current API.
  DataTrackStream& operator=(DataTrackStream&&) noexcept = delete;

  /// Blocking read: waits until a DataTrackFrame is available, or the
  /// stream reaches EOS / is closed.
  ///
  /// @param out  On success, filled with the next data frame.
  /// @return true if a frame was delivered; false if the stream ended.
  bool read(DataTrackFrame& out);

  /// Returns the terminal subscription error reported by the FFI stream.
  ///
  /// This is set when read() returns false because subscription establishment
  /// failed before any frames were emitted. It remains empty for normal EOS or
  /// when close() ends the stream locally.
  std::optional<SubscribeDataTrackError> terminalError() const;

  /// End the stream early.
  ///
  /// Releases the FFI handle (which unsubscribes from the remote track),
  /// unregisters the event listener, and wakes any blocking read().
  void close();

private:
  friend class RemoteDataTrack;
#ifdef LIVEKIT_TEST_ACCESS
  friend class DataTrackStreamTest;
#endif

  DataTrackStream() = default;
  /// Internal init helper, called by RemoteDataTrack.
  void init(FfiHandle subscription_handle);

  /// FFI event handler, called by FfiClient.
  void onFfiEvent(const proto::FfiEvent& event);

  /// Handle the immediate response returned by a read request.
  void handleReadResponse(const proto::DataTrackStreamReadResponse& response);

  /// Mark the stream failed due to an invalid FFI protocol response.
  void failProtocolError(const char* message);

  /// Push a received DataTrackFrame to the internal storage.
  void pushFrame(DataTrackFrame&& frame);

  /// Push an end-of-stream signal (EOS).
  void pushEos(std::optional<SubscribeDataTrackError> error = std::nullopt);

  /// Protects all mutable state below.
  mutable std::mutex mutex_;

  /// Signalled when a frame is pushed or the subscription ends.
  std::condition_variable cv_;

  /// Received frame awaiting read().
  /// NOTE: the Rust side handles buffering, so we should only really ever have
  /// one item.
  std::optional<DataTrackFrame> frame_;

  /// True once the remote side signals end-of-stream.
  bool eof_{false};

  /// True after close() has been called by the consumer.
  bool closed_{false};

  /// Typed terminal error reported with EOS, if subscription setup failed.
  std::optional<SubscribeDataTrackError> terminal_error_;

  /// RAII handle for the Rust-owned subscription resource.
  FfiHandle subscription_handle_;

  /// FfiClient listener id for routing FfiEvent callbacks to this object.
  std::int32_t listener_id_{-1};
};

} // namespace livekit
