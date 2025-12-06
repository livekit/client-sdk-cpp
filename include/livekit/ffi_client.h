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
class AudioFrameBufferInfo;
class ConnectCallback;
class FfiEvent;
class FfiResponse;
class FfiRequest;
class OwnedTrackPublication;
class TranscriptionSegment;
} // namespace proto

struct RoomOptions;
struct TrackPublishOptions;

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
  std::future<proto::ConnectCallback> connectAsync(const std::string &url,
                                                   const std::string &token,
                                                   const RoomOptions &options);

  // Track APIs
  std::future<std::vector<RtcStats>> getTrackStatsAsync(uintptr_t track_handle);

  // Participant APIs
  std::future<proto::OwnedTrackPublication>
  publishTrackAsync(std::uint64_t local_participant_handle,
                    std::uint64_t track_handle,
                    const TrackPublishOptions &options);
  std::future<void> unpublishTrackAsync(std::uint64_t local_participant_handle,
                                        const std::string &track_sid,
                                        bool stop_on_unpublish);
  std::future<void>
  publishDataAsync(std::uint64_t local_participant_handle,
                   const std::uint8_t *data_ptr, std::uint64_t data_len,
                   bool reliable,
                   const std::vector<std::string> &destination_identities,
                   const std::string &topic);
  std::future<void> publishTranscriptionAsync(
      std::uint64_t local_participant_handle,
      const std::string &participant_identity, const std::string &track_id,
      const std::vector<proto::TranscriptionSegment> &segments);
  std::future<void>
  publishSipDtmfAsync(std::uint64_t local_participant_handle,
                      std::uint32_t code, const std::string &digit,
                      const std::vector<std::string> &destination_identities);
  std::future<void>
  setLocalMetadataAsync(std::uint64_t local_participant_handle,
                        const std::string &metadata);
  std::future<void>
  captureAudioFrameAsync(std::uint64_t source_handle,
                         const proto::AudioFrameBufferInfo &buffer);
  std::future<std::string> performRpcAsync(
      std::uint64_t local_participant_handle,
      const std::string &destination_identity, const std::string &method,
      const std::string &payload,
      std::optional<std::uint32_t> response_timeout_ms = std::nullopt);

  // Generic function for sending a request to the Rust FFI.
  // Note: For asynchronous requests, use the dedicated async functions instead
  // of sendRequest.
  proto::FfiResponse sendRequest(const proto::FfiRequest &request) const;

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
