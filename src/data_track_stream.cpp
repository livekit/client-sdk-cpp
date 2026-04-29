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

#include "data_track.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"

#include <utility>

namespace livekit {

using proto::FfiEvent;

DataTrackStream::~DataTrackStream() { close(); }

void DataTrackStream::init(FfiHandle subscription_handle) {
  subscription_handle_ = std::move(subscription_handle);

  listener_id_ = FfiClient::instance().AddListener(
      [this](const FfiEvent &e) { this->onFfiEvent(e); });
}

bool DataTrackStream::read(DataTrackFrame &out) {
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (closed_ || eof_) {
      return false;
    }

    const auto subscription_handle =
        static_cast<std::uint64_t>(subscription_handle_.get());

    // Signal the Rust side that we're ready to receive the next frame.
    // The Rust SubscriptionTask uses a demand-driven protocol: it won't pull
    // from the underlying stream until notified via this request.
    proto::FfiRequest req;
    auto *msg = req.mutable_data_track_stream_read();
    msg->set_stream_handle(subscription_handle);
    FfiClient::instance().sendRequest(req);
  }

  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return frame_.has_value() || eof_ || closed_; });

  if (closed_ || (!frame_.has_value() && eof_)) {
    return false;
  }

  out = std::move(*frame_); // NOLINT(bugprone-unchecked-optional-access)
  frame_.reset();
  return true;
}

void DataTrackStream::close() {
  std::int32_t listener_id = -1;
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (closed_) {
      return;
    }
    closed_ = true;
    subscription_handle_.reset();
    listener_id = listener_id_;
    listener_id_ = 0;
  }

  if (listener_id != -1) {
    FfiClient::instance().RemoveListener(listener_id);
  }

  cv_.notify_all();
}

void DataTrackStream::onFfiEvent(const FfiEvent &event) {
  if (event.message_case() != FfiEvent::kDataTrackStreamEvent) {
    return;
  }

  const auto &dts = event.data_track_stream_event();
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (closed_ || dts.stream_handle() !=
                       static_cast<std::uint64_t>(subscription_handle_.get())) {
      return;
    }
  }

  if (dts.has_frame_received()) {
    const auto &fr = dts.frame_received().frame();
    DataTrackFrame frame = DataTrackFrame::fromOwnedInfo(fr);
    pushFrame(std::move(frame));
  } else if (dts.has_eos()) {
    pushEos();
  }
}

void DataTrackStream::pushFrame(DataTrackFrame &&frame) {
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

void DataTrackStream::pushEos() {
  {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (eof_) {
      return;
    }
    eof_ = true;
  }
  cv_.notify_all();
}

} // namespace livekit
