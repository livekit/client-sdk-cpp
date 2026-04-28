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

#include "livekit/encoded_tcp_ingest.h"

#include <stdexcept>
#include <utility>

#include "encoded_tcp_ingest.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/room.h"

namespace livekit {

EncodedTcpIngest::EncodedTcpIngest(FfiHandle handle, std::string track_sid,
                                   std::string track_name)
    : handle_(std::move(handle)), track_sid_(std::move(track_sid)),
      track_name_(std::move(track_name)) {
  registerListener();
}

EncodedTcpIngest::~EncodedTcpIngest() { unregisterListener(); }

EncodedTcpIngest::EncodedTcpIngest(EncodedTcpIngest &&other) noexcept {
  *this = std::move(other);
}

EncodedTcpIngest &
EncodedTcpIngest::operator=(EncodedTcpIngest &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  unregisterListener();
  handle_ = std::move(other.handle_);
  track_sid_ = std::move(other.track_sid_);
  track_name_ = std::move(other.track_name_);
  {
    std::lock_guard<std::mutex> lock(observer_lock_);
    std::lock_guard<std::mutex> other_lock(other.observer_lock_);
    observer_ = std::move(other.observer_);
  }
  other.unregisterListener();
  registerListener();
  return *this;
}

EncodedTcpIngest
EncodedTcpIngest::start(Room &room, const EncodedTcpIngestOptions &options) {
  std::uint64_t room_handle = 0;
  {
    std::lock_guard<std::mutex> lock(room.lock_);
    if (room.room_handle_) {
      room_handle = static_cast<std::uint64_t>(room.room_handle_->get());
    }
  }

  if (room_handle == 0) {
    throw std::runtime_error(
        "EncodedTcpIngest::start requires a connected room");
  }

  auto fut =
      FfiClient::instance().newEncodedTcpIngestAsync(room_handle, options);
  const proto::OwnedEncodedTcpIngest owned = fut.get();
  const auto &info = owned.info();
  return EncodedTcpIngest(
      FfiHandle(static_cast<std::uintptr_t>(owned.handle().id())),
      info.track_sid(), info.track_name());
}

void EncodedTcpIngest::stop() {
  const std::uint64_t handle = static_cast<std::uint64_t>(handle_.get());
  if (handle == 0) {
    return;
  }

  auto fut = FfiClient::instance().stopEncodedTcpIngestAsync(handle);
  fut.get();
  handle_.reset();
}

EncodedTcpIngestStats EncodedTcpIngest::stats() const {
  const std::uint64_t handle = static_cast<std::uint64_t>(handle_.get());
  if (handle == 0) {
    return {};
  }

  proto::FfiRequest req;
  req.mutable_get_encoded_tcp_ingest_stats()->set_ingest_handle(handle);
  const proto::FfiResponse resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_get_encoded_tcp_ingest_stats()) {
    throw std::runtime_error(
        "FfiResponse missing get_encoded_tcp_ingest_stats");
  }

  const auto &stats = resp.get_encoded_tcp_ingest_stats().stats();
  return EncodedTcpIngestStats{stats.frames_accepted(), stats.frames_dropped(),
                               stats.keyframes(), stats.tcp_reconnects()};
}

void EncodedTcpIngest::setObserver(
    std::shared_ptr<EncodedIngestObserver> observer) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  observer_ = std::move(observer);
}

void EncodedTcpIngest::registerListener() {
  if (!handle_ || listener_id_ != 0) {
    return;
  }

  listener_id_ = FfiClient::instance().AddListener(
      [this](const proto::FfiEvent &event) { handleEvent(event); });
}

void EncodedTcpIngest::unregisterListener() noexcept {
  if (listener_id_ == 0) {
    return;
  }
  FfiClient::instance().RemoveListener(listener_id_);
  listener_id_ = 0;
}

void EncodedTcpIngest::handleEvent(const proto::FfiEvent &event) const {
  if (!event.has_encoded_tcp_ingest_event()) {
    return;
  }

  const auto &ingest_event = event.encoded_tcp_ingest_event();
  if (ingest_event.ingest_handle() !=
      static_cast<std::uint64_t>(handle_.get())) {
    return;
  }

  std::shared_ptr<EncodedIngestObserver> observer;
  {
    std::lock_guard<std::mutex> lock(observer_lock_);
    observer = observer_;
  }
  if (!observer) {
    return;
  }

  switch (ingest_event.message_case()) {
  case proto::EncodedTcpIngestEvent::kConnected:
    observer->onConnected(ingest_event.connected().peer());
    break;
  case proto::EncodedTcpIngestEvent::kDisconnected:
    observer->onDisconnected(ingest_event.disconnected().reason());
    break;
  case proto::EncodedTcpIngestEvent::kKeyframeRequested:
    observer->onKeyframeRequested();
    break;
  case proto::EncodedTcpIngestEvent::kTargetBitrateChanged:
    observer->onTargetBitrate(
        ingest_event.target_bitrate_changed().bitrate_bps(),
        ingest_event.target_bitrate_changed().framerate_fps());
    break;
  case proto::EncodedTcpIngestEvent::MESSAGE_NOT_SET:
  default:
    break;
  }
}

} // namespace livekit
