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

#include <optional>
#include <utility>

#include "data_track.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"
#include "lk_log.h"

namespace livekit {

using proto::FfiEvent;

namespace {

constexpr char kMissingReadResponseError[] = "DataTrackStream::read: FFI response missing data_track_stream_read";

std::optional<SubscribeDataTrackError> terminalErrorFromEos(const proto::DataTrackStreamEOS& eos) {
  if (!eos.has_error()) {
    return std::nullopt;
  }
  return SubscribeDataTrackError::fromProto(eos.error());
}

} // namespace

DataTrackStream::~DataTrackStream() { close(); }

void DataTrackStream::init(FfiHandle subscription_handle) {
  subscription_handle_ = std::move(subscription_handle);

  listener_id_ = FfiClient::instance().addListener([this](const FfiEvent& e) { this->onFfiEvent(e); });
}

bool DataTrackStream::read(DataTrackFrame& out) {
  proto::DataTrackStreamReadResponse read_response;
  bool missing_read_response = false;
  std::uint64_t subscription_handle = 0;

  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (closed_ || eof_) {
      return false;
    }
    subscription_handle = static_cast<std::uint64_t>(subscription_handle_.get());
  }

  // Do not hold mutex_ across sendRequest: readFrameWithTimeout may call close()
  // from another thread on timeout, and close() also needs mutex_.
  proto::FfiRequest req;
  auto* msg = req.mutable_data_track_stream_read();
  msg->set_stream_handle(subscription_handle);
  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);

  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (closed_ || eof_) {
      return false;
    }
    if (!resp.has_data_track_stream_read()) {
      missing_read_response = true;
    } else {
      read_response = resp.data_track_stream_read();
    }
  }

  if (missing_read_response) {
    failProtocolError(kMissingReadResponseError);
    return false;
  }

  handleReadResponse(read_response);

  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return frame_.has_value() || eof_ || closed_; });

  if (closed_ || (!frame_.has_value() && eof_)) {
    return false;
  }

  out = std::move(*frame_); // NOLINT(bugprone-unchecked-optional-access)
  frame_.reset();
  return true;
}

std::optional<SubscribeDataTrackError> DataTrackStream::terminalError() const {
  const std::scoped_lock<std::mutex> lock(mutex_);
  return terminal_error_;
}

void DataTrackStream::close() {
  std::int32_t listener_id = -1;
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (closed_) {
      return;
    }
    closed_ = true;
    // Preserve errors reported by EOS for post-stream inspection, but do not
    // treat a local early close as a terminal subscription error.
    if (!eof_) {
      terminal_error_.reset();
    }
    subscription_handle_.reset();
    listener_id = listener_id_;
    listener_id_ = -1;
  }

  if (listener_id != -1) {
    FfiClient::instance().removeListener(listener_id);
  }

  cv_.notify_all();
}

void DataTrackStream::onFfiEvent(const FfiEvent& event) {
  if (event.message_case() != FfiEvent::kDataTrackStreamEvent) {
    return;
  }

  const auto& dts = event.data_track_stream_event();
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (closed_ || dts.stream_handle() != static_cast<std::uint64_t>(subscription_handle_.get())) {
      return;
    }
  }

  if (dts.has_frame_received()) {
    const auto& fr = dts.frame_received().frame();
    DataTrackFrame frame = DataTrackFrame::fromOwnedInfo(fr);
    pushFrame(std::move(frame));
  } else if (dts.has_eos()) {
    pushEos(terminalErrorFromEos(dts.eos()));
  }
}

void DataTrackStream::handleReadResponse(const proto::DataTrackStreamReadResponse& response) {
  if (!response.has_eos_event()) {
    return;
  }
  pushEos(terminalErrorFromEos(response.eos_event()));
}

void DataTrackStream::failProtocolError(const char* message) {
  LK_LOG_ERROR("{}", message);
  pushEos(SubscribeDataTrackError{SubscribeDataTrackErrorCode::PROTOCOL_ERROR, message});
  close();
}

void DataTrackStream::pushFrame(DataTrackFrame&& frame) {
  const std::scoped_lock<std::mutex> lock(mutex_);

  if (closed_ || eof_) {
    return;
  }

  // rust side handles buffering, so we should only really ever have one item
  assert(!frame_.has_value());

  frame_ = std::move(frame);

  // notify no matter what since we got a new frame
  cv_.notify_one();
}

void DataTrackStream::pushEos(std::optional<SubscribeDataTrackError> error) {
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (closed_ || eof_) {
      return;
    }
    eof_ = true;
    terminal_error_ = std::move(error);
  }
  cv_.notify_all();
}

} // namespace livekit
