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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "livekit/data_track_subscription.h"

#include "data_track.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"

#include <utility>

namespace livekit {

using proto::FfiEvent;

DataTrackSubscription::~DataTrackSubscription() { close(); }

DataTrackSubscription::DataTrackSubscription(
    DataTrackSubscription &&other) noexcept {
  std::lock_guard<std::mutex> lock(other.mutex_);
  queue_ = std::move(other.queue_);
  capacity_ = other.capacity_;
  eof_ = other.eof_;
  closed_ = other.closed_;
  subscription_handle_ = std::move(other.subscription_handle_);
  listener_id_ = other.listener_id_;

  other.listener_id_ = 0;
  other.closed_ = true;
}

DataTrackSubscription &
DataTrackSubscription::operator=(DataTrackSubscription &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  close();

  {
    std::lock_guard<std::mutex> lock_this(mutex_);
    std::lock_guard<std::mutex> lock_other(other.mutex_);

    queue_ = std::move(other.queue_);
    capacity_ = other.capacity_;
    eof_ = other.eof_;
    closed_ = other.closed_;
    subscription_handle_ = std::move(other.subscription_handle_);
    listener_id_ = other.listener_id_;

    other.listener_id_ = 0;
    other.closed_ = true;
  }

  return *this;
}

void DataTrackSubscription::init(FfiHandle subscription_handle,
                                 const Options &options) {
  subscription_handle_ = std::move(subscription_handle);
  capacity_ = options.capacity;

  listener_id_ = FfiClient::instance().AddListener(
      [this](const FfiEvent &e) { this->onFfiEvent(e); });
}

bool DataTrackSubscription::read(DataTrackFrame &out) {
  std::unique_lock<std::mutex> lock(mutex_);

  cv_.wait(lock, [this] { return !queue_.empty() || eof_ || closed_; });

  if (closed_ || (queue_.empty() && eof_)) {
    return false;
  }

  out = std::move(queue_.front());
  queue_.pop_front();
  return true;
}

void DataTrackSubscription::close() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return;
    }
    closed_ = true;
  }

  if (subscription_handle_.get() != 0) {
    subscription_handle_.reset();
  }

  if (listener_id_ != 0) {
    FfiClient::instance().RemoveListener(listener_id_);
    listener_id_ = 0;
  }

  cv_.notify_all();
}

void DataTrackSubscription::onFfiEvent(const FfiEvent &event) {
  if (event.message_case() != FfiEvent::kDataTrackSubscriptionEvent) {
    return;
  }

  const auto &dts = event.data_track_subscription_event();
  if (dts.subscription_handle() !=
      static_cast<std::uint64_t>(subscription_handle_.get())) {
    return;
  }

  if (dts.has_frame_received()) {
    const auto &fr = dts.frame_received().frame();
    DataTrackFrame frame;
    const auto &payload_str = fr.payload();
    frame.payload.assign(
        reinterpret_cast<const std::uint8_t *>(payload_str.data()),
        reinterpret_cast<const std::uint8_t *>(payload_str.data()) +
            payload_str.size());
    if (fr.has_user_timestamp()) {
      frame.user_timestamp = fr.user_timestamp();
    }
    pushFrame(std::move(frame));
  } else if (dts.has_eos()) {
    pushEos();
  }
}

void DataTrackSubscription::pushFrame(DataTrackFrame &&frame) {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_ || eof_) {
      return;
    }

    if (capacity_ > 0 && queue_.size() >= capacity_) {
      queue_.pop_front();
    }

    queue_.push_back(std::move(frame));
  }
  cv_.notify_one();
}

void DataTrackSubscription::pushEos() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (eof_) {
      return;
    }
    eof_ = true;
  }
  cv_.notify_all();
}

} // namespace livekit
