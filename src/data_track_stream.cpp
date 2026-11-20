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

#include "livekit/data_track_stream.h"

#include <cassert>
#include <condition_variable>
#include <mutex>
#include <utility>

#include "data_track.pb.h"
#include "ffi.pb.h"
#include "livekit_ffi.hpp"
#include "lk_log.h"

namespace livekit {

namespace {

constexpr char kMissingReadResponseError[] = "DataTrackStream::read: FFI response missing data_track_stream_read";

std::optional<SubscribeDataTrackError> terminalErrorFromEos(const proto::DataTrackStreamEOS& eos) {
  if (!eos.has_error()) {
    return std::nullopt;
  }
  return SubscribeDataTrackError::fromProto(eos.error());
}

SubscribeDataTrackError subscribeError(const std::exception& error) {
  auto code = SubscribeDataTrackErrorCode::INTERNAL;
  if (dynamic_cast<const livekit_datatrack::data_track_subscribe_error::Unpublished*>(&error)) {
    code = SubscribeDataTrackErrorCode::UNPUBLISHED;
  } else if (dynamic_cast<const livekit_datatrack::data_track_subscribe_error::Timeout*>(&error)) {
    code = SubscribeDataTrackErrorCode::TIMEOUT;
  } else if (dynamic_cast<const livekit_datatrack::data_track_subscribe_error::Disconnected*>(&error)) {
    code = SubscribeDataTrackErrorCode::DISCONNECTED;
  }
  return SubscribeDataTrackError{code, error.what()};
}

} // namespace

struct DataTrackStream::Impl {
  std::mutex mutex;
  std::condition_variable cv;
  std::optional<DataTrackFrame> frame;
  bool eof{false};
  bool closed{false};
  std::optional<SubscribeDataTrackError> terminal_error;
  std::shared_ptr<livekit_ffi::DataTrackStream> stream;
  uniffi::FutureContinuation continuation;

  void pushFrame(DataTrackFrame&& value) {
    const std::scoped_lock<std::mutex> lock(mutex);
    if (closed || eof) {
      return;
    }
    assert(!frame.has_value());
    frame = std::move(value);
    cv.notify_one();
  }

  void pushEos(std::optional<SubscribeDataTrackError> error = std::nullopt) {
    const std::scoped_lock<std::mutex> lock(mutex);
    if (closed || eof) {
      return;
    }
    eof = true;
    terminal_error = std::move(error);
    stream.reset();
    cv.notify_all();
  }
};

DataTrackStream::DataTrackStream() : impl_(std::make_shared<Impl>()) {}

DataTrackStream::~DataTrackStream() { close(); }

void DataTrackStream::init(std::uint64_t track_handle, const Options& options) {
  auto future = livekit_ffi::RemoteDataTrack::from_ffi_handle_id(track_handle)
                    ->subscribe_with_options(livekit_ffi::DataTrackSubscribeOptions{options.buffer_size.value_or(16)});
  const std::weak_ptr<Impl> weak = impl_;
  const std::scoped_lock<std::mutex> lock(impl_->mutex);
  impl_->continuation = std::move(future).then([weak](auto result) {
    const auto state = weak.lock();
    if (!state) {
      return;
    }
    try {
      auto stream = std::move(result).get();
      const std::scoped_lock<std::mutex> state_lock(state->mutex);
      if (!state->closed) {
        state->stream = std::move(stream);
        state->cv.notify_all();
      }
    } catch (const std::exception& error) {
      state->pushEos(subscribeError(error));
    }
  });
}

bool DataTrackStream::read(DataTrackFrame& out) {
  const auto state = impl_;
  std::unique_lock<std::mutex> lock(state->mutex);
  state->cv.wait(lock, [&state] { return state->stream || state->closed || state->eof; });
  if (state->closed || state->eof) {
    return false;
  }

  // Only the caller blocks; completion and cancellation use the shared UniFFI executor.
  const std::weak_ptr<Impl> weak = state;
  state->continuation = std::move(state->stream->next()).then([weak](auto result) {
    const auto target = weak.lock();
    if (!target) {
      return;
    }
    try {
      auto frame = std::move(result).get();
      if (frame) {
        target->pushFrame(DataTrackFrame(std::move(frame->payload), frame->user_timestamp));
      } else {
        target->pushEos();
      }
    } catch (const std::exception& error) {
      target->pushEos(subscribeError(error));
    }
  });
  state->cv.wait(lock, [&state] { return state->frame.has_value() || state->eof || state->closed; });
  if (state->closed || !state->frame) {
    return false;
  }
  out = std::move(*state->frame);
  state->frame.reset();
  return true;
}

std::optional<SubscribeDataTrackError> DataTrackStream::terminalError() const {
  const std::scoped_lock<std::mutex> lock(impl_->mutex);
  return impl_->terminal_error;
}

void DataTrackStream::close() {
  uniffi::FutureContinuation continuation;
  std::shared_ptr<livekit_ffi::DataTrackStream> stream;
  {
    const std::scoped_lock<std::mutex> lock(impl_->mutex);
    if (impl_->closed) {
      return;
    }
    impl_->closed = true;
    if (!impl_->eof) {
      impl_->terminal_error.reset();
    }
    continuation = std::move(impl_->continuation);
    stream = std::move(impl_->stream);
  }
  impl_->cv.notify_all();
  continuation.cancel();
  if (stream) {
    try {
      stream->close();
    } catch (const std::exception& error) {
      LK_LOG_ERROR("Failed to close data track stream: {}", error.what());
    }
  }
}

// Retained for the existing protobuf protocol-decoding unit tests during migration.
void DataTrackStream::onFfiEvent(const proto::FfiEvent& event) {
  if (event.message_case() != proto::FfiEvent::kDataTrackStreamEvent) {
    return;
  }
  const auto& stream_event = event.data_track_stream_event();
  if (stream_event.stream_handle() != 0) {
    return;
  }
  if (stream_event.has_frame_received()) {
    pushFrame(DataTrackFrame::fromOwnedInfo(stream_event.frame_received().frame()));
  } else if (stream_event.has_eos()) {
    pushEos(terminalErrorFromEos(stream_event.eos()));
  }
}

void DataTrackStream::handleReadResponse(const proto::DataTrackStreamReadResponse& response) {
  if (response.has_eos_event()) {
    pushEos(terminalErrorFromEos(response.eos_event()));
  }
}

void DataTrackStream::failProtocolError(const char* message) {
  LK_LOG_ERROR("{}", message);
  pushEos(SubscribeDataTrackError{SubscribeDataTrackErrorCode::PROTOCOL_ERROR, message});
  close();
}

void DataTrackStream::pushFrame(DataTrackFrame&& frame) { impl_->pushFrame(std::move(frame)); }

void DataTrackStream::pushEos(std::optional<SubscribeDataTrackError> error) { impl_->pushEos(std::move(error)); }

} // namespace livekit
