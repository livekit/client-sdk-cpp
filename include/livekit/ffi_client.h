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

#ifndef LIVEKIT_FFI_CLIENT_H
#define LIVEKIT_FFI_CLIENT_H

#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "livekit/stats.h"

namespace livekit {

namespace proto {
class FfiEvent;
class FfiResponse;
class FfiRequest;
class RoomInfo;
} // namespace proto

using FfiCallbackFn = void (*)(const uint8_t *, size_t);
extern "C" void livekit_ffi_initialize(FfiCallbackFn cb, bool capture_logs,
                                       const char *sdk,
                                       const char *sdk_version);

extern "C" void livekit_ffi_dispose();

extern "C" void LivekitFfiCallback(const uint8_t *buf, size_t len);

// The FfiClient is used to communicate with the FFI interface of the Rust SDK
// We use the generated protocol messages to facilitate the communication
class FfiClient {
public:
  using ListenerId = int;
  using Listener = std::function<void(const proto::FfiEvent &)>;
  using AsyncId = std::uint64_t;

  FfiClient(const FfiClient &) = delete;
  FfiClient &operator=(const FfiClient &) = delete;
  FfiClient(FfiClient &&) = delete;
  FfiClient &operator=(FfiClient &&) = delete;

  static FfiClient &instance() noexcept {
    static FfiClient instance;
    return instance;
  }

  // Called only once. After calling shutdown(), no further calls into FfiClient
  // are valid.
  void shutdown() noexcept;

  ListenerId AddListener(const Listener &listener);
  void RemoveListener(ListenerId id);

  // Room APIs
  std::future<proto::RoomInfo> connectAsync(const std::string &url,
                                            const std::string &token);

  // Track APIs
  std::future<std::vector<RtcStats>> getTrackStatsAsync(uintptr_t track_handle);
  std::future<bool> localTrackMuteAsync(uintptr_t track_handle, bool mute);
  std::future<bool> enableRemoteTrackAsync(uintptr_t track_handle,
                                           bool enabled);

  proto::FfiResponse SendRequest(const proto::FfiRequest &request) const;

private:
  // Base class for type-erased pending ops
  struct PendingBase {
    virtual ~PendingBase() = default;
    virtual bool matches(const proto::FfiEvent &event) const = 0;
    virtual void complete(const proto::FfiEvent &event) = 0;
  };
  template <typename T> struct Pending : PendingBase {
    std::promise<T> promise;
    std::function<bool(const proto::FfiEvent &)> match;
    std::function<void(const proto::FfiEvent &, std::promise<T> &)> handler;

    bool matches(const proto::FfiEvent &event) const override {
      return match && match(event);
    }

    void complete(const proto::FfiEvent &event) override {
      handler(event, promise);
    }
  };

  template <typename T>
  std::future<T> registerAsync(
      std::function<bool(const proto::FfiEvent &)> match,
      std::function<void(const proto::FfiEvent &, std::promise<T> &)> handler);

  std::unordered_map<ListenerId, Listener> listeners_;
  ListenerId nextListenerId = 1;
  mutable std::mutex lock_;
  mutable std::vector<std::unique_ptr<PendingBase>> pending_;

  FfiClient();
  ~FfiClient() = default;

  void PushEvent(const proto::FfiEvent &event) const;
  friend void LivekitFfiCallback(const uint8_t *buf, size_t len);
};
} // namespace livekit

#endif /* LIVEKIT_FFI_CLIENT_H */
