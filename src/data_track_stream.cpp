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

#include <cassert>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace livekit {

using proto::FfiEvent;

struct DataTrackStream::Impl {
  ~Impl() { close(); }

  void init(FfiHandle subscription_handle) {
    subscription_handle_ = std::move(subscription_handle);

    listener_id_ = FfiClient::instance().AddListener(
        [this](const FfiEvent &e) { this->onFfiEvent(e); });
  }

  bool read(DataTrackFrame &out) {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (closed_ || eof_) {
        return false;
      }

      const auto subscription_handle =
          static_cast<std::uint64_t>(subscription_handle_.get());

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

  void close() {
    FfiHandle subscription_handle;
    std::int32_t listener_id = 0;
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (closed_) {
        return;
      }
      closed_ = true;
      subscription_handle = std::move(subscription_handle_);
      listener_id = listener_id_;
      listener_id_ = 0;
    }

    if (subscription_handle.get() != 0) {
      subscription_handle.reset();
    }
    if (listener_id != 0) {
      FfiClient::instance().RemoveListener(listener_id);
    }

    cv_.notify_all();
  }

  void onFfiEvent(const FfiEvent &event) {
    if (event.message_case() != FfiEvent::kDataTrackStreamEvent) {
      return;
    }

    const auto &dts = event.data_track_stream_event();
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (closed_ || dts.stream_handle() != static_cast<std::uint64_t>(
                                                subscription_handle_.get())) {
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

  void pushFrame(DataTrackFrame &&frame) {
    const std::scoped_lock<std::mutex> lock(mutex_);

    if (closed_ || eof_) {
      return;
    }

    assert(!frame_.has_value());
    frame_ = std::move(frame);
    cv_.notify_one();
  }

  void pushEos() {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (eof_) {
        return;
      }
      eof_ = true;
    }
    cv_.notify_all();
  }

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::optional<DataTrackFrame> frame_;
  bool eof_{false};
  bool closed_{false};
  FfiHandle subscription_handle_;
  std::int32_t listener_id_{0};
};

DataTrackStream::DataTrackStream() : impl_(std::make_unique<Impl>()) {}

DataTrackStream::~DataTrackStream() = default;

void DataTrackStream::init(FfiHandle subscription_handle) {
  impl_->init(std::move(subscription_handle));
}

bool DataTrackStream::read(DataTrackFrame &out) {
  return impl_ ? impl_->read(out) : false;
}

void DataTrackStream::close() {
  if (impl_) {
    impl_->close();
  }
}

} // namespace livekit
