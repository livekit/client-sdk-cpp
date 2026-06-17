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

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#include "data_track.pb.h"
#include "livekit/data_track_error.h"
#include "livekit/data_track_schema.h"
#include "livekit/result.h"
#include "livekit/room_event_types.h"
#include "livekit/stats.h"
#include "livekit/visibility.h"
#include "lk_log.h"
#include "room.pb.h"

namespace livekit {

namespace proto {
class AudioFrameBufferInfo;
class ConnectCallback;
class FfiEvent;
class FfiResponse;
class FfiRequest;
class OwnedTrackPublication;
class OwnedLocalDataTrack;
class OwnedDataTrackStream;
class DataStream;

} // namespace proto

struct RoomOptions;
struct TrackPublishOptions;

using FfiCallbackFn = void (*)(const uint8_t*, size_t);
extern "C" void livekit_ffi_initialize(FfiCallbackFn cb, bool capture_logs, const char* sdk, const char* sdk_version);

extern "C" void livekit_ffi_dispose();

extern "C" LIVEKIT_INTERNAL_API void ffiEventCallback(const uint8_t* buf, size_t len);

// The FfiClient is used to communicate with the FFI interface of the Rust SDK
// We use the generated protocol messages to facilitate the communication.
class LIVEKIT_INTERNAL_API FfiClient {
public:
  using ListenerId = int;
  using Listener = std::function<void(const proto::FfiEvent&)>;
  using AsyncId = std::uint64_t;

  ~FfiClient();
  FfiClient(const FfiClient&) = delete;
  FfiClient& operator=(const FfiClient&) = delete;
  FfiClient(FfiClient&&) = delete;
  FfiClient& operator=(FfiClient&&) = delete;

  // Access the singleton instance of the FfiClient
  // Note: lazily created, not thread safe
  static FfiClient& instance() noexcept;

  // Must be called before any other FFI usage
  bool initialize(bool capture_logs);

  // Called only once. After calling shutdown(), no further calls into FfiClient
  // are valid.
  void shutdown() noexcept;

  bool isInitialized() const noexcept;

  ListenerId addListener(const Listener& listener);
  void removeListener(ListenerId id);

  // Room APIs
  std::future<proto::ConnectCallback> connectAsync(const std::string& url, const std::string& token,
                                                   const RoomOptions& options);

  std::future<void> disconnectAsync(uintptr_t room_handle, DisconnectReason reason);

  // Track APIs
  std::future<std::vector<RtcStats>> getTrackStatsAsync(uintptr_t track_handle);

  std::future<SessionStats> getSessionStatsAsync(uintptr_t room_handle);

  // Participant APIs
  std::future<proto::OwnedTrackPublication> publishTrackAsync(std::uint64_t local_participant_handle,
                                                              std::uint64_t track_handle,
                                                              const TrackPublishOptions& options);
  std::future<void> unpublishTrackAsync(std::uint64_t local_participant_handle, const std::string& track_sid,
                                        bool stop_on_unpublish);
  std::future<void> publishDataAsync(std::uint64_t local_participant_handle, const std::uint8_t* data_ptr,
                                     std::uint64_t data_len, bool reliable,
                                     const std::vector<std::string>& destination_identities, const std::string& topic);
  std::future<void> publishSipDtmfAsync(std::uint64_t local_participant_handle, std::uint32_t code,
                                        const std::string& digit,
                                        const std::vector<std::string>& destination_identities);
  std::future<void> setLocalMetadataAsync(std::uint64_t local_participant_handle, const std::string& metadata);
  std::future<void> captureAudioFrameAsync(std::uint64_t source_handle, const proto::AudioFrameBufferInfo& buffer);
  std::future<std::string> performRpcAsync(std::uint64_t local_participant_handle,
                                           const std::string& destination_identity, const std::string& method,
                                           const std::string& payload,
                                           std::optional<std::uint32_t> response_timeout_ms = std::nullopt);

  // Data Track schema APIs
  std::future<void> defineSchemaAsync(std::uint64_t local_participant_handle, const DataTrackSchemaId& schema_id,
                                      const std::string& definition);
  std::future<std::string> getSchemaAsync(std::uint64_t local_participant_handle, const DataTrackSchemaId& schema_id,
                                          const std::string& participant_identity);

  // Data Track APIs
  std::future<Result<proto::OwnedLocalDataTrack, PublishDataTrackError>> publishDataTrackAsync(
      std::uint64_t local_participant_handle, const std::string& track_name);

  Result<proto::OwnedDataTrackStream, SubscribeDataTrackError> subscribeDataTrack(
      std::uint64_t track_handle, std::optional<std::uint32_t> buffer_size = std::nullopt);

  // Data stream functionalities
  std::future<void> sendStreamHeaderAsync(std::uint64_t local_participant_handle,
                                          const proto::DataStream::Header& header,
                                          const std::vector<std::string>& destination_identities,
                                          const std::string& sender_identity);
  std::future<void> sendStreamChunkAsync(std::uint64_t local_participant_handle, const proto::DataStream::Chunk& chunk,
                                         const std::vector<std::string>& destination_identities,
                                         const std::string& sender_identity);
  std::future<void> sendStreamTrailerAsync(std::uint64_t local_participant_handle,
                                           const proto::DataStream::Trailer& trailer,
                                           const std::string& sender_identity);

  // Generic function for sending a request to the Rust FFI.
  // Note: For asynchronous requests, use the dedicated async functions instead
  // of sendRequest.
  proto::FfiResponse sendRequest(const proto::FfiRequest& request) const;

private:
  FfiClient() = default;

  /// Lifecycle state of the FfiClient
  /// This is used to prevent race conditions/use-after-free scenarios
  enum class LifecycleState : std::uint8_t {
    Uninitialized,
    Initializing,
    Initialized,
    ShuttingDown,
  };

  // Base class for type-erased pending ops
  struct PendingBase {
    AsyncId async_id = 0; // Client-generated async ID for cancellation
    virtual ~PendingBase() = default;
    virtual bool matches(const proto::FfiEvent& event) const = 0;
    virtual void complete(const proto::FfiEvent& event) = 0;
    virtual void cancel() = 0; // Cancel the pending operation
  };
  template <typename T>
  struct Pending : PendingBase {
    std::promise<T> promise;
    std::function<bool(const proto::FfiEvent&)> match;
    std::function<void(const proto::FfiEvent&, std::promise<T>&)> handler;

    bool matches(const proto::FfiEvent& event) const override { return match && match(event); }

    void complete(const proto::FfiEvent& event) override { handler(event, promise); }

    void cancel() override {
      try {
        promise.set_exception(std::make_exception_ptr(std::runtime_error("Async operation cancelled")));
      } catch (const std::future_error& e) {
        // Unlikely to throw here as the promise should be satisfied before
        // cancel() Logging a debug message to avoid clang empty catch warning
        LK_LOG_DEBUG("FfiClient::cancel: promise already satisfied: {}", e.what());
      }
    }
  };

  /// Additional data structure to track listener callbacks and their state.
  /// This is used to coordinate the FFI thread and the app thread, and prevent race conditions/use-after-free scenarios
  struct ListenerSlot {
    explicit ListenerSlot(Listener cb) : listener(std::move(cb)) {}

    /// The user-provided listener callback
    Listener listener;
    /// Mutex to protect the listener slot
    std::mutex mutex;
    /// Condition variable to wait for the listener to finish
    std::condition_variable cv;
    /// Map of thread IDs to the number of active threads
    std::unordered_map<std::thread::id, int> active_threads;
    /// Number of active callbacks
    int active_callbacks = 0;
    /// Whether the listener has been removed (used for race mitigation before removal)
    bool removed = false;
  };

  template <typename T>
  std::future<T> registerAsync(AsyncId async_id, std::function<bool(const proto::FfiEvent&)> match,
                               std::function<void(const proto::FfiEvent&, std::promise<T>&)> handler);

  // Generate a unique client-side async ID for request correlation
  AsyncId generateAsyncId();

  // Cancel a pending async operation by its async_id. Returns true if found and
  // removed.
  bool cancelPendingByAsyncId(AsyncId async_id);

  /// Map of listener IDs to listener slots
  std::unordered_map<ListenerId, std::shared_ptr<ListenerSlot>> listeners_;
  /// Next listener ID to generate
  std::atomic<ListenerId> next_listener_id{1};
  mutable std::mutex lock_;
  /// Map of async IDs to pending operations
  mutable std::unordered_map<AsyncId, std::unique_ptr<PendingBase>> pending_by_id_;
  /// Next async ID to generate
  std::atomic<AsyncId> next_async_id_{1};

  void pushEvent(const proto::FfiEvent& event) const;
  friend void ffiEventCallback(const uint8_t* buf, size_t len);
  std::atomic<LifecycleState> lifecycle_state_{LifecycleState::Uninitialized};
};
} // namespace livekit
