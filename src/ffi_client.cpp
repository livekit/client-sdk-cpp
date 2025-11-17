/*
 * Copyright 2023 LiveKit
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

#include <cassert>

#include "build.h"
#include "ffi.pb.h"
#include "livekit/ffi_client.h"
#include "livekit/ffi_handle.h"
#include "livekit_ffi.h"

namespace livekit {

FfiClient::FfiClient() {
  livekit_ffi_initialize(&LivekitFfiCallback, true, LIVEKIT_BUILD_FLAVOR,
                         LIVEKIT_BUILD_VERSION_FULL);
}

void FfiClient::shutdown() noexcept { livekit_ffi_dispose(); }

FfiClient::ListenerId
FfiClient::AddListener(const FfiClient::Listener &listener) {
  std::lock_guard<std::mutex> guard(lock_);
  FfiClient::ListenerId id = nextListenerId++;
  listeners_[id] = listener;
  return id;
}

void FfiClient::RemoveListener(ListenerId id) {
  std::lock_guard<std::mutex> guard(lock_);
  listeners_.erase(id);
}

proto::FfiResponse
FfiClient::SendRequest(const proto::FfiRequest &request) const {
  std::string bytes;
  if (!request.SerializeToString(&bytes) || bytes.empty()) {
    throw std::runtime_error("failed to serialize FfiRequest");
  }
  const uint8_t *resp_ptr = nullptr;
  size_t resp_len = 0;
  FfiHandleId handle =
      livekit_ffi_request(reinterpret_cast<const uint8_t *>(bytes.data()),
                          bytes.size(), &resp_ptr, &resp_len);
  std::cout << "receive a handle " << handle << std::endl;

  if (handle == INVALID_HANDLE) {
    throw std::runtime_error(
        "failed to send request, received an invalid handle");
  }

  // Ensure we drop the handle exactly once on all paths
  FfiHandle handle_guard(static_cast<uintptr_t>(handle));
  if (!resp_ptr || resp_len == 0) {
    throw std::runtime_error("FFI returned empty response bytes");
  }

  proto::FfiResponse response;
  if (!response.ParseFromArray(resp_ptr, static_cast<int>(resp_len))) {
    throw std::runtime_error("failed to parse FfiResponse");
  }
  return response;
}

void FfiClient::PushEvent(const proto::FfiEvent &event) const {
  std::vector<std::unique_ptr<PendingBase>> to_complete;
  {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto it = pending_.begin(); it != pending_.end();) {
      if ((*it)->matches(event)) {
        to_complete.push_back(std::move(*it));
        it = pending_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Run handlers outside lock
  for (auto &p : to_complete) {
    p->complete(event);
  }

  // Notify listeners. Note, we copy the listeners here to avoid calling into
  // the listeners under the lock, which could potentially cause deadlock.
  std::vector<Listener> listeners_copy;
  {
    std::lock_guard<std::mutex> guard(lock_);
    listeners_copy.reserve(listeners_.size());
    for (auto &[_, listener] : listeners_) {
      listeners_copy.push_back(listener);
    }
  }
  for (auto &listener : listeners_copy) {
    listener(event);
  }
}

void LivekitFfiCallback(const uint8_t *buf, size_t len) {
  proto::FfiEvent event;
  event.ParseFromArray(buf, len);

  FfiClient::instance().PushEvent(event);
}

template <typename T>
std::future<T> FfiClient::registerAsync(
    std::function<bool(const proto::FfiEvent &)> match,
    std::function<void(const proto::FfiEvent &, std::promise<T> &)> handler) {
  auto pending = std::make_unique<Pending<T>>();
  auto fut = pending->promise.get_future();
  pending->match = std::move(match);
  pending->handler = std::move(handler);
  {
    std::lock_guard<std::mutex> guard(lock_);
    pending_.push_back(std::move(pending));
  }
  return fut;
}

// Room APIs Implementation
std::future<livekit::proto::RoomInfo>
FfiClient::connectAsync(const std::string &url, const std::string &token) {

  livekit::proto::FfiRequest req;
  auto *connect = req.mutable_connect();
  connect->set_url(url);
  connect->set_token(token);
  connect->mutable_options()->set_auto_subscribe(true);

  livekit::proto::FfiResponse resp = SendRequest(req);
  if (!resp.has_connect()) {
    throw std::runtime_error("FfiResponse missing connect");
  }

  const AsyncId async_id = resp.connect().async_id();

  // Now we register an async op that completes with RoomInfo
  return registerAsync<livekit::proto::RoomInfo>(
      // match lambda: is this the connect event with our async_id?
      [async_id](const livekit::proto::FfiEvent &event) {
        return event.has_connect() && event.connect().async_id() == async_id;
      },
      // handler lambda: fill the promise with RoomInfo or an exception
      [](const livekit::proto::FfiEvent &event,
         std::promise<livekit::proto::RoomInfo> &pr) {
        const auto &ce = event.connect();

        if (!ce.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(ce.error())));
          return;
        }

        // ce.result().room().info() is a const ref, so we copy it
        livekit::proto::RoomInfo info = ce.result().room().info();
        pr.set_value(std::move(info));
      });
}

// Track APIs Implementation
std::future<std::vector<RtcStats>>
FfiClient::getTrackStatsAsync(uintptr_t track_handle) {
  proto::FfiRequest req;
  auto *get_stats_req = req.mutable_get_stats();
  get_stats_req->set_track_handle(track_handle);
  proto::FfiResponse resp = SendRequest(req);
  if (!resp.has_get_stats()) {
    throw std::runtime_error("FfiResponse missing get_stats");
  }

  const AsyncId async_id = resp.get_stats().async_id();

  // Register pending op:
  //   - match: event.has_get_stats() && ids equal
  //   - handler: convert proto stats to C++ wrapper + fulfill promise
  return registerAsync<std::vector<RtcStats>>(
      // match
      [async_id](const proto::FfiEvent &event) {
        return event.has_get_stats() &&
               event.get_stats().async_id() == async_id;
      },
      // handler
      [](const proto::FfiEvent &event,
         std::promise<std::vector<RtcStats>> &pr) {
        const auto &gs = event.get_stats();

        if (!gs.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(gs.error())));
          return;
        }

        std::vector<RtcStats> stats_vec;
        stats_vec.reserve(gs.stats_size());
        for (const auto &ps : gs.stats()) {
          stats_vec.push_back(fromProto(ps));
        }
        pr.set_value(std::move(stats_vec));
      });
}

} // namespace livekit
