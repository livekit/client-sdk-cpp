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

#include "ffi_client.h"

#include <cassert>
#include <csignal>
#include <cstdio>
#include <string>
#include <type_traits>

#include "data_track.pb.h"
#include "data_track_proto_converter.h"
#include "ffi.pb.h"
#include "livekit/build.h"
#include "livekit/data_track_error.h"
#include "livekit/ffi_handle.h"
#include "livekit/room.h"
#include "livekit/rpc_error.h"
#include "livekit_ffi.h"
#include "lk_log.h"
#include "room.pb.h"
#include "room_proto_converter.h"

namespace livekit {

namespace {

inline void logAndThrow(const std::string& error_msg) {
  LK_LOG_ERROR("LiveKit SDK Error: {}", error_msg);
  throw std::runtime_error(error_msg);
}

// Helper for debug logging of optional values
const auto optional_to_string = [](const auto& value) -> std::string {
  if (!value) {
    return "<unset>";
  }
  using Value = std::decay_t<decltype(*value)>;
  if constexpr (std::is_same_v<Value, bool>) {
    return *value ? "true" : "false";
  } else if constexpr (std::is_same_v<Value, std::chrono::milliseconds>) {
    return std::to_string(value->count());
  } else {
    return std::to_string(*value);
  }
};

Result<proto::OwnedDataTrackStream, SubscribeDataTrackError> subscribeDataTrackFailure(SubscribeDataTrackErrorCode code,
                                                                                       const std::string& message) {
  LK_LOG_WARN("Subscribe data track failed: code={} message={}", static_cast<std::uint32_t>(code), message);
  return Result<proto::OwnedDataTrackStream, SubscribeDataTrackError>::failure(SubscribeDataTrackError{code, message});
}

std::optional<FfiClient::AsyncId> ExtractAsyncId(const proto::FfiEvent& event) {
  using E = proto::FfiEvent;
  switch (event.message_case()) {
    case E::kConnect:
      return event.connect().async_id();
    case E::kDisconnect:
      return event.disconnect().async_id();
    case E::kDispose:
      return event.dispose().async_id();
    case E::kPublishTrack:
      return event.publish_track().async_id();
    case E::kUnpublishTrack:
      return event.unpublish_track().async_id();
    case E::kPublishData:
      return event.publish_data().async_id();
    case E::kPublishTranscription:
      return event.publish_transcription().async_id();
    case E::kCaptureAudioFrame:
      return event.capture_audio_frame().async_id();
    case E::kSetLocalMetadata:
      return event.set_local_metadata().async_id();
    case E::kSetLocalName:
      return event.set_local_name().async_id();
    case E::kSetLocalAttributes:
      return event.set_local_attributes().async_id();
    case E::kGetStats:
      return event.get_stats().async_id();
    case E::kGetSessionStats:
      return event.get_session_stats().async_id();
    case E::kPublishSipDtmf:
      return event.publish_sip_dtmf().async_id();
    case E::kChatMessage:
      return event.chat_message().async_id();
    case E::kPerformRpc:
      return event.perform_rpc().async_id();

    // low-level data stream callbacks
    case E::kSendStreamHeader:
      return event.send_stream_header().async_id();
    case E::kSendStreamChunk:
      return event.send_stream_chunk().async_id();
    case E::kSendStreamTrailer:
      return event.send_stream_trailer().async_id();

    // high-level
    case E::kByteStreamReaderReadAll:
      return event.byte_stream_reader_read_all().async_id();
    case E::kByteStreamReaderWriteToFile:
      return event.byte_stream_reader_write_to_file().async_id();
    case E::kByteStreamOpen:
      return event.byte_stream_open().async_id();
    case E::kByteStreamWriterWrite:
      return event.byte_stream_writer_write().async_id();
    case E::kByteStreamWriterClose:
      return event.byte_stream_writer_close().async_id();
    case E::kSendFile:
      return event.send_file().async_id();

    case E::kTextStreamReaderReadAll:
      return event.text_stream_reader_read_all().async_id();
    case E::kTextStreamOpen:
      return event.text_stream_open().async_id();
    case E::kTextStreamWriterWrite:
      return event.text_stream_writer_write().async_id();
    case E::kTextStreamWriterClose:
      return event.text_stream_writer_close().async_id();
    case E::kSendText:
      return event.send_text().async_id();
    case E::kSendBytes:
      return event.send_bytes().async_id();

    // data track async completions
    case E::kPublishDataTrack:
      return event.publish_data_track().async_id();

    // NOT async completion:
    case E::kRoomEvent:
    case E::kTrackEvent:
    case E::kVideoStreamEvent:
    case E::kAudioStreamEvent:
    case E::kByteStreamReaderEvent:
    case E::kTextStreamReaderEvent:
    case E::kDataTrackStreamEvent:
    case E::kRpcMethodInvocation:
    case E::kLogs:
    case E::kPanic:
    case E::MESSAGE_NOT_SET:
    default:
      return std::nullopt;
  }
}

} // namespace

FfiClient& FfiClient::instance() noexcept {
  static FfiClient instance;
  return instance;
}

FfiClient::~FfiClient() {
  if (lifecycle_state_.load() == LifecycleState::Initialized) {
    // Explicitly use this over spdlog/std::cerr which can throw
    // Wrapping spdlog try/catch also flags "empty catch" clang-tidy check
    std::fputs("[livekit] [warning] SDK was not shut down before process exit. Use livekit::shutdown()\n", stderr);
    std::fflush(stderr);
  }
}

void FfiClient::shutdown() noexcept {
  // Don't use string to avoid exceptions
  // (Also cleaner with exception.what() and printing)
  const char* shutdown_error = nullptr;
  try {
    // compare_exchange_strong atomically claims Initialized -> ShuttingDown so only one
    // concurrent shutdown() drains listeners and disposes the FFI.
    LifecycleState expected = LifecycleState::Initialized;
    if (!lifecycle_state_.compare_exchange_strong(expected, LifecycleState::ShuttingDown, std::memory_order_acq_rel)) {
      // If not Initialized, return early to avoid unnecessary cleanup
      std::fputs("[livekit] [warning] SDK was shutdown while not initialized\n", stderr);
      std::fflush(stderr);
      return;
    }

    std::vector<std::shared_ptr<ListenerSlot>> listeners_to_drain;
    std::vector<std::unique_ptr<PendingBase>> pending_to_cancel;
    {
      const std::scoped_lock<std::mutex> guard(lock_);
      listeners_to_drain.reserve(listeners_.size());
      for (auto& [id, slot] : listeners_) {
        (void)id;
        if (slot) {
          // Mark the listener as removed to prevent race conditions
          {
            const std::scoped_lock<std::mutex> slot_guard(slot->mutex);
            slot->removed = true;
          }
          // Add the listener to the list of listeners to drain
          listeners_to_drain.push_back(std::move(slot));
        }
      }
      listeners_.clear();

      // Add the pending operations to the list of pending operations to cancel
      pending_to_cancel.reserve(pending_by_id_.size());
      for (auto& [async_id, pending] : pending_by_id_) {
        (void)async_id;
        if (pending) {
          pending_to_cancel.push_back(std::move(pending));
        }
      }
      pending_by_id_.clear();
    }

    // Cancel the pending operations
    for (auto& pending : pending_to_cancel) {
      pending->cancel();
    }

    const auto this_thread = std::this_thread::get_id();
    // Wait for all in-flight listener callbacks to complete
    for (const auto& slot : listeners_to_drain) {
      std::unique_lock<std::mutex> slot_lock(slot->mutex);

      // When shutdown() isn't on a listener thread, self_active is 0 and we wait for active_callbacks == 0. When it's
      // called from inside a listener, self_active is 1 and the wait succeeds immediately with active_callbacks == 1,
      // so we don't wait on our own in-flight callback
      slot->cv.wait(slot_lock, [&slot, this_thread] {
        const auto thread_it = slot->active_threads.find(this_thread);
        const int self_active = thread_it == slot->active_threads.end() ? 0 : thread_it->second;
        return slot->active_callbacks == self_active;
      });
    }
  } catch (const std::exception& e) {
    shutdown_error = e.what();
  } catch (...) {
    shutdown_error = "unknown exception";
  }

  livekit_ffi_dispose();
  lifecycle_state_.store(LifecycleState::Uninitialized, std::memory_order_release);

  if (shutdown_error != nullptr) {
    // Explicitly use this over spdlog (method is noexcept)
    (void)std::fputs("[livekit] [error] SDK shutdown failed during local cleanup: ", stderr);
    (void)std::fputs(shutdown_error, stderr);
    (void)std::fputs("\n", stderr);
    (void)std::fflush(stderr);
  }
}

bool FfiClient::initialize(bool capture_logs) {
  LifecycleState expected = LifecycleState::Uninitialized;
  if (!lifecycle_state_.compare_exchange_strong(expected, LifecycleState::Initializing, std::memory_order_acq_rel)) {
    return false;
  }

  try {
    livekit_ffi_initialize(&ffiEventCallback, capture_logs, LIVEKIT_BUILD_FLAVOR, LIVEKIT_BUILD_VERSION);
  } catch (...) {
    lifecycle_state_.store(LifecycleState::Uninitialized, std::memory_order_release);
    throw;
  }

  lifecycle_state_.store(LifecycleState::Initialized, std::memory_order_release);
  return true;
}

bool FfiClient::isInitialized() const noexcept {
  return lifecycle_state_.load(std::memory_order_acquire) == LifecycleState::Initialized;
}

FfiClient::ListenerId FfiClient::addListener(const FfiClient::Listener& listener) {
  const std::scoped_lock<std::mutex> guard(lock_);
  if (lifecycle_state_.load(std::memory_order_acquire) == LifecycleState::ShuttingDown) {
    logAndThrow("FfiClient::addListener failed: LiveKit is shutting down");
  }
  const FfiClient::ListenerId id = next_listener_id++;
  listeners_[id] = std::make_shared<ListenerSlot>(listener);
  return id;
}

void FfiClient::removeListener(ListenerId id) {
  std::shared_ptr<ListenerSlot> slot;
  {
    const std::scoped_lock<std::mutex> guard(lock_);
    auto it = listeners_.find(id);
    if (it == listeners_.end()) {
      return;
    }
    slot = std::move(it->second);
    listeners_.erase(it);
  }

  const auto this_thread = std::this_thread::get_id();
  std::unique_lock<std::mutex> slot_lock(slot->mutex);
  slot->cv.wait(slot_lock, [&slot, this_thread] {
    const auto self_active = slot->active_threads.count(this_thread) != 0;
    return slot->active_callbacks == 0 || (self_active && slot->active_callbacks == 1);
  });
  slot->removed = true;
}

proto::FfiResponse FfiClient::sendRequest(const proto::FfiRequest& request) const {
  // The Rust FFI will lazily initialize the FFI client when the first request is sent,
  // but if not initialized none of the async operations will work. Guarding against that here.
  // Improvement ticket added to the Rust SDK to discuss this
  if (!isInitialized()) {
    throw std::runtime_error("FfiClient::sendRequest failed: LiveKit is not initialized");
  }

  std::string bytes;
  if (!request.SerializeToString(&bytes) || bytes.empty()) {
    throw std::runtime_error("failed to serialize FfiRequest");
  }
  const uint8_t* resp_ptr = nullptr;
  size_t resp_len = 0;
  const FfiHandleId handle =
      livekit_ffi_request(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &resp_ptr, &resp_len);
  if (handle == INVALID_HANDLE) {
    throw std::runtime_error("failed to send request, received an invalid handle");
  }

  // Ensure we drop the handle exactly once on all paths
  const FfiHandle handle_guard(static_cast<uintptr_t>(handle));
  if (!resp_ptr || resp_len == 0) {
    throw std::runtime_error("FFI returned empty response bytes");
  }

  proto::FfiResponse response;
  if (!response.ParseFromArray(resp_ptr, static_cast<int>(resp_len))) {
    throw std::runtime_error("failed to parse FfiResponse");
  }
  return response;
}

void FfiClient::pushEvent(const proto::FfiEvent& event) const {
  std::unique_ptr<PendingBase> to_complete;
  std::vector<std::shared_ptr<ListenerSlot>> listeners_copy;
  {
    const std::scoped_lock<std::mutex> guard(lock_);
    if (lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Initialized) {
      return;
    }

    // Complete pending future if this event is a callback with async_id
    if (auto async_id = ExtractAsyncId(event)) {
      auto it = pending_by_id_.find(*async_id);
      if (it != pending_by_id_.end() && it->second && it->second->matches(event)) {
        to_complete = std::move(it->second);
        pending_by_id_.erase(it);
      }
    }

    // Snapshot listeners
    listeners_copy.reserve(listeners_.size());
    for (const auto& kv : listeners_) {
      listeners_copy.push_back(kv.second);
    }
  }
  // Run handler outside lock
  if (to_complete) {
    to_complete->complete(event);
  }

  // Notify listeners outside lock
  for (const auto& slot : listeners_copy) {
    Listener listener;
    const auto this_thread = std::this_thread::get_id();
    {
      const std::scoped_lock<std::mutex> slot_guard(slot->mutex);
      if (slot->removed) {
        continue;
      }
      ++slot->active_callbacks;
      ++slot->active_threads[this_thread];
      listener = slot->listener;
    }

    try {
      listener(event);
    } catch (const std::exception& e) {
      LK_LOG_ERROR("FfiClient listener threw: {}", e.what());
    } catch (...) {
      LK_LOG_ERROR("FfiClient listener threw: unknown exception");
    }

    {
      const std::scoped_lock<std::mutex> slot_guard(slot->mutex);
      const auto thread_it = slot->active_threads.find(this_thread);
      if (thread_it != slot->active_threads.end()) {
        --thread_it->second;
        if (thread_it->second == 0) {
          slot->active_threads.erase(thread_it);
        }
      }
      --slot->active_callbacks;
    }

    // Notify in case this listener was marked for removal during the callback (will be waiting on this)
    slot->cv.notify_all();
  }
}

extern "C" LIVEKIT_INTERNAL_API void ffiEventCallback(const uint8_t* buf, size_t len) {
  proto::FfiEvent event;
  event.ParseFromArray(buf,
                       static_cast<int>(len)); // TODO: this fixes for now, what if len exceeds int?

  // We are in a unrecoverable state, terminate the process
  if (event.has_panic()) {
    LK_LOG_CRITICAL("FFI Panic: {}", event.panic().message());
    livekit::detail::getLogger()->flush(); // Flush the logger to ensure all messages are written
    std::raise(SIGTERM);
    return;
  }

  FfiClient::instance().pushEvent(event);
}

FfiClient::AsyncId FfiClient::generateAsyncId() { return next_async_id_.fetch_add(1, std::memory_order_relaxed); }

bool FfiClient::cancelPendingByAsyncId(AsyncId async_id) {
  std::unique_ptr<PendingBase> to_cancel;
  {
    const std::scoped_lock<std::mutex> guard(lock_);
    auto it = pending_by_id_.find(async_id);
    if (it != pending_by_id_.end()) {
      to_cancel = std::move(it->second);
      pending_by_id_.erase(it);
    }
  }
  if (to_cancel) {
    to_cancel->cancel();
    return true;
  }
  return false;
}

template <typename T>
std::future<T> FfiClient::registerAsync(AsyncId async_id, std::function<bool(const proto::FfiEvent&)> match,
                                        std::function<void(const proto::FfiEvent&, std::promise<T>&)> handler) {
  auto pending = std::make_unique<Pending<T>>();
  pending->async_id = async_id;
  auto fut = pending->promise.get_future();
  pending->match = std::move(match);
  pending->handler = std::move(handler);
  {
    const std::scoped_lock<std::mutex> guard(lock_);
    pending_by_id_.emplace(async_id, std::move(pending));
  }
  return fut;
}

// Room APIs Implementation
std::future<proto::ConnectCallback> FfiClient::connectAsync(const std::string& url, const std::string& token,
                                                            const RoomOptions& options) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<proto::ConnectCallback>(
      async_id,
      // match lambda: is this the connect event with our async_id?
      [async_id](const proto::FfiEvent& event) {
        return event.has_connect() && event.connect().async_id() == async_id;
      },
      // handler lambda: fill the promise with RoomInfo or an exception
      [](const proto::FfiEvent& event, std::promise<proto::ConnectCallback>& pr) {
        const auto& connectCb = event.connect();
        if (!connectCb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(connectCb.error())));
          return;
        }

        pr.set_value(connectCb);
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* connect = req.mutable_connect();
  connect->set_url(url);
  connect->set_token(token);
  connect->set_request_async_id(async_id);
  connect->mutable_options()->CopyFrom(toProto(options));

  LK_LOG_DEBUG(
      "[FfiClient] connectAsync: auto_subscribe={}, adaptive_stream={}, dynacast={}, "
      "single_peer_connection={}, join_retries={}, connect_timeout_ms={}",
      options.auto_subscribe, optional_to_string(options.adaptive_stream), options.dynacast,
      options.single_peer_connection, optional_to_string(options.join_retries),
      optional_to_string(options.connect_timeout));

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_connect()) {
      logAndThrow("FfiResponse missing connect");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::disconnectAsync(uintptr_t room_handle, DisconnectReason reason) {
  const AsyncId async_id = generateAsyncId();

  auto fut = registerAsync<void>(
      async_id,
      // match: this DisconnectCallback's async_id
      [async_id](const proto::FfiEvent& event) {
        return event.has_disconnect() && event.disconnect().async_id() == async_id;
      },
      // handler: nothing to extract; the callback is signal-only
      [](const proto::FfiEvent& /*event*/, std::promise<void>& pr) { pr.set_value(); });

  proto::FfiRequest req;
  auto* disconnect = req.mutable_disconnect();
  disconnect->set_room_handle(room_handle);
  disconnect->set_request_async_id(async_id);
  disconnect->set_reason(toProto(reason));

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_disconnect()) {
      logAndThrow("FfiResponse missing disconnect");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

// Track APIs Implementation
std::future<std::vector<RtcStats>> FfiClient::getTrackStatsAsync(uintptr_t track_handle) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<std::vector<RtcStats>>(
      async_id,
      // match
      [async_id](const proto::FfiEvent& event) {
        return event.has_get_stats() && event.get_stats().async_id() == async_id;
      },
      // handler
      [](const proto::FfiEvent& event, std::promise<std::vector<RtcStats>>& pr) {
        const auto& gs = event.get_stats();

        if (!gs.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(gs.error())));
          return;
        }

        std::vector<RtcStats> stats_vec;
        stats_vec.reserve(gs.stats_size());
        for (const auto& ps : gs.stats()) {
          stats_vec.push_back(fromProto(ps));
        }
        pr.set_value(std::move(stats_vec));
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* get_stats_req = req.mutable_get_stats();
  get_stats_req->set_track_handle(track_handle);
  get_stats_req->set_request_async_id(async_id);

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_get_stats()) {
      logAndThrow("FfiResponse missing get_stats");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<SessionStats> FfiClient::getSessionStatsAsync(uintptr_t room_handle) {
  const AsyncId async_id = generateAsyncId();

  auto fut = registerAsync<SessionStats>(
      async_id,
      // match
      [async_id](const proto::FfiEvent& event) {
        return event.has_get_session_stats() && event.get_session_stats().async_id() == async_id;
      },
      // handler
      [](const proto::FfiEvent& event, std::promise<SessionStats>& pr) {
        const auto& cb = event.get_session_stats();
        if (cb.has_error()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        if (!cb.has_result()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error("GetSessionStatsCallback missing result and error")));
          return;
        }

        const auto& result = cb.result();
        SessionStats stats;
        stats.publisher_stats.reserve(result.publisher_stats_size());
        for (const auto& ps : result.publisher_stats()) {
          stats.publisher_stats.push_back(fromProto(ps));
        }
        stats.subscriber_stats.reserve(result.subscriber_stats_size());
        for (const auto& ps : result.subscriber_stats()) {
          stats.subscriber_stats.push_back(fromProto(ps));
        }
        pr.set_value(std::move(stats));
      });

  proto::FfiRequest req;
  auto* get_session_stats_req = req.mutable_get_session_stats();
  get_session_stats_req->set_room_handle(room_handle);
  get_session_stats_req->set_request_async_id(async_id);

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_get_session_stats()) {
      logAndThrow("FfiResponse missing get_session_stats");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

// Participant APIs Implementation
std::future<proto::OwnedTrackPublication> FfiClient::publishTrackAsync(std::uint64_t local_participant_handle,
                                                                       std::uint64_t track_handle,
                                                                       const TrackPublishOptions& options) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<proto::OwnedTrackPublication>(
      async_id,
      // Match: is this our PublishTrackCallback?
      [async_id](const proto::FfiEvent& event) {
        return event.has_publish_track() && event.publish_track().async_id() == async_id;
      },
      // Handler: resolve with publication or throw error
      [](const proto::FfiEvent& event, std::promise<proto::OwnedTrackPublication>& pr) {
        const auto& cb = event.publish_track();

        // Oneof message { string error = 2; OwnedTrackPublication publication =
        // 3; }
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        if (!cb.has_publication()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error("PublishTrackCallback missing publication")));
          return;
        }

        const proto::OwnedTrackPublication& pub = cb.publication();
        pr.set_value(pub);
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_publish_track();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_track_handle(track_handle);
  msg->set_request_async_id(async_id);
  auto optionProto = toProto(options);
  msg->mutable_options()->CopyFrom(optionProto);

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_publish_track()) {
      logAndThrow("FfiResponse missing publish_track");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::unpublishTrackAsync(std::uint64_t local_participant_handle, const std::string& track_sid,
                                                 bool stop_on_unpublish) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent& event) {
        return event.has_unpublish_track() && event.unpublish_track().async_id() == async_id;
      },
      [](const proto::FfiEvent& event, std::promise<void>& pr) {
        const auto& cb = event.unpublish_track();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_unpublish_track();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_track_sid(track_sid);
  msg->set_stop_on_unpublish(stop_on_unpublish);
  msg->set_request_async_id(async_id);

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_unpublish_track()) {
      logAndThrow("FfiResponse missing unpublish_track");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::publishDataAsync(std::uint64_t local_participant_handle, const std::uint8_t* data_ptr,
                                              std::uint64_t data_len, bool reliable,
                                              const std::vector<std::string>& destination_identities,
                                              const std::string& topic) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent& event) {
        return event.has_publish_data() && event.publish_data().async_id() == async_id;
      },
      [](const proto::FfiEvent& event, std::promise<void>& pr) {
        const auto& cb = event.publish_data();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_publish_data();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_data_ptr(reinterpret_cast<std::uint64_t>(data_ptr));
  msg->set_data_len(data_len);
  msg->set_reliable(reliable);
  msg->set_topic(topic);
  msg->set_request_async_id(async_id);
  for (const auto& id : destination_identities) {
    msg->add_destination_identities(id);
  }

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_publish_data()) {
      logAndThrow("FfiResponse missing publish_data");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<Result<proto::OwnedLocalDataTrack, PublishDataTrackError>> FfiClient::publishDataTrackAsync(
    std::uint64_t local_participant_handle, const DataTrackPublishOptions& options) {
  const AsyncId async_id = generateAsyncId();

  auto fut = registerAsync<Result<proto::OwnedLocalDataTrack, PublishDataTrackError>>(
      async_id,
      [async_id](const proto::FfiEvent& event) {
        return event.has_publish_data_track() && event.publish_data_track().async_id() == async_id;
      },
      [](const proto::FfiEvent& event, std::promise<Result<proto::OwnedLocalDataTrack, PublishDataTrackError>>& pr) {
        const auto& cb = event.publish_data_track();
        if (cb.has_error()) {
          pr.set_value(Result<proto::OwnedLocalDataTrack, PublishDataTrackError>::failure(
              PublishDataTrackError::fromProto(cb.error())));
          return;
        }
        if (!cb.has_track()) {
          pr.set_value(Result<proto::OwnedLocalDataTrack, PublishDataTrackError>::failure(PublishDataTrackError{
              PublishDataTrackErrorCode::PROTOCOL_ERROR, "PublishDataTrackCallback missing track"}));
          return;
        }
        pr.set_value(Result<proto::OwnedLocalDataTrack, PublishDataTrackError>::success(cb.track()));
      });

  proto::FfiRequest req;
  auto* msg = req.mutable_publish_data_track();
  msg->set_local_participant_handle(local_participant_handle);
  auto* opts = msg->mutable_options();
  opts->set_name(options.name);
  if (options.schema.has_value()) {
    *opts->mutable_schema() = toProto(*options.schema);
  }
  if (options.frame_encoding.has_value()) {
    *opts->mutable_frame_encoding() = toProto(*options.frame_encoding);
  }
  msg->set_request_async_id(async_id);

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_publish_data_track()) {
      cancelPendingByAsyncId(async_id);
      std::promise<Result<proto::OwnedLocalDataTrack, PublishDataTrackError>> pr;
      pr.set_value(Result<proto::OwnedLocalDataTrack, PublishDataTrackError>::failure(
          PublishDataTrackError{PublishDataTrackErrorCode::PROTOCOL_ERROR, "FfiResponse missing publish_data_track"}));
      return pr.get_future();
    }
  } catch (const std::exception& e) {
    cancelPendingByAsyncId(async_id);
    std::promise<Result<proto::OwnedLocalDataTrack, PublishDataTrackError>> pr;
    pr.set_value(Result<proto::OwnedLocalDataTrack, PublishDataTrackError>::failure(
        PublishDataTrackError{PublishDataTrackErrorCode::INTERNAL, e.what()}));
    return pr.get_future();
  }

  return fut;
}

Result<proto::OwnedDataTrackStream, SubscribeDataTrackError> FfiClient::subscribeDataTrack(
    std::uint64_t track_handle, std::optional<std::uint32_t> buffer_size) {
  proto::FfiRequest req;
  auto* msg = req.mutable_subscribe_data_track();
  msg->set_track_handle(track_handle);
  auto* opts = msg->mutable_options();
  if (buffer_size.has_value()) {
    opts->set_buffer_size(buffer_size.value());
  }

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_subscribe_data_track()) {
      return subscribeDataTrackFailure(SubscribeDataTrackErrorCode::PROTOCOL_ERROR,
                                       "FfiResponse missing subscribe_data_track");
    }
    if (!resp.subscribe_data_track().has_stream()) {
      return subscribeDataTrackFailure(SubscribeDataTrackErrorCode::PROTOCOL_ERROR,
                                       "FfiResponse subscribe_data_track missing stream");
    }
    proto::OwnedDataTrackStream sub = resp.subscribe_data_track().stream();
    return Result<proto::OwnedDataTrackStream, SubscribeDataTrackError>::success(std::move(sub));
  } catch (const std::exception& e) { // NOLINT(bugprone-empty-catch)
    return subscribeDataTrackFailure(SubscribeDataTrackErrorCode::INTERNAL, e.what());
  }
}

std::future<void> FfiClient::publishSipDtmfAsync(std::uint64_t local_participant_handle, std::uint32_t code,
                                                 const std::string& digit,
                                                 const std::vector<std::string>& destination_identities) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent& event) {
        return event.has_publish_sip_dtmf() && event.publish_sip_dtmf().async_id() == async_id;
      },
      [](const proto::FfiEvent& event, std::promise<void>& pr) {
        const auto& cb = event.publish_sip_dtmf();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_publish_sip_dtmf();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_code(code);
  msg->set_digit(digit);
  msg->set_request_async_id(async_id);
  for (const auto& id : destination_identities) {
    msg->add_destination_identities(id);
  }

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_publish_sip_dtmf()) {
      logAndThrow("FfiResponse missing publish_sip_dtmf");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::setLocalMetadataAsync(std::uint64_t local_participant_handle,
                                                   const std::string& metadata) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent& event) {
        return event.has_set_local_metadata() && event.set_local_metadata().async_id() == async_id;
      },
      [](const proto::FfiEvent& event, std::promise<void>& pr) {
        const auto& cb = event.set_local_metadata();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_set_local_metadata();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_metadata(metadata);
  msg->set_request_async_id(async_id);

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_set_local_metadata()) {
      logAndThrow("FfiResponse missing set_local_metadata");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::captureAudioFrameAsync(std::uint64_t source_handle,
                                                    const proto::AudioFrameBufferInfo& buffer) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      // match predicate
      [async_id](const proto::FfiEvent& event) {
        return event.has_capture_audio_frame() && event.capture_audio_frame().async_id() == async_id;
      },
      // completion handler
      [](const proto::FfiEvent& event, std::promise<void>& pr) {
        const auto& cb = event.capture_audio_frame();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_capture_audio_frame();
  msg->set_source_handle(source_handle);
  msg->set_request_async_id(async_id);
  msg->mutable_buffer()->CopyFrom(buffer);

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_capture_audio_frame()) {
      logAndThrow("FfiResponse missing capture_audio_frame");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<std::string> FfiClient::performRpcAsync(std::uint64_t local_participant_handle,
                                                    const std::string& destination_identity, const std::string& method,
                                                    const std::string& payload,
                                                    std::optional<std::uint32_t> response_timeout_ms) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<std::string>(
      async_id,
      // match predicate
      [async_id](const proto::FfiEvent& event) {
        return event.has_perform_rpc() && event.perform_rpc().async_id() == async_id;
      },
      [](const proto::FfiEvent& event, std::promise<std::string>& pr) {
        const auto& cb = event.perform_rpc();

        if (cb.has_error()) {
          // RpcError is a proto message; convert to C++ RpcError and throw
          pr.set_exception(std::make_exception_ptr(RpcError::fromProto(cb.error())));
          return;
        }
        pr.set_value(cb.payload());
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_perform_rpc();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_destination_identity(destination_identity);
  msg->set_method(method);
  msg->set_payload(payload);
  msg->set_request_async_id(async_id);
  if (response_timeout_ms.has_value()) {
    msg->set_response_timeout_ms(*response_timeout_ms);
  }

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_perform_rpc()) {
      logAndThrow("FfiResponse missing perform_rpc");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::sendStreamHeaderAsync(std::uint64_t local_participant_handle,
                                                   const proto::DataStream::Header& header,
                                                   const std::vector<std::string>& destination_identities,
                                                   const std::string& sender_identity) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent& e) {
        return e.has_send_stream_header() && e.send_stream_header().async_id() == async_id;
      },
      [](const proto::FfiEvent& e, std::promise<void>& pr) {
        const auto& cb = e.send_stream_header();
        if (!cb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_send_stream_header();
  msg->set_local_participant_handle(local_participant_handle);
  *msg->mutable_header() = header;
  msg->set_sender_identity(sender_identity);
  msg->set_request_async_id(async_id);
  for (const auto& id : destination_identities) {
    msg->add_destination_identities(id);
  }

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_send_stream_header()) {
      logAndThrow("FfiResponse missing send_stream_header");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::sendStreamChunkAsync(std::uint64_t local_participant_handle,
                                                  const proto::DataStream::Chunk& chunk,
                                                  const std::vector<std::string>& destination_identities,
                                                  const std::string& sender_identity) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent& e) {
        return e.has_send_stream_chunk() && e.send_stream_chunk().async_id() == async_id;
      },
      [](const proto::FfiEvent& e, std::promise<void>& pr) {
        const auto& cb = e.send_stream_chunk();
        if (!cb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_send_stream_chunk();
  msg->set_local_participant_handle(local_participant_handle);
  *msg->mutable_chunk() = chunk;
  msg->set_sender_identity(sender_identity);
  msg->set_request_async_id(async_id);
  for (const auto& id : destination_identities) {
    msg->add_destination_identities(id);
  }

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_send_stream_chunk()) {
      logAndThrow("FfiResponse missing send_stream_chunk");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::sendStreamTrailerAsync(std::uint64_t local_participant_handle,
                                                    const proto::DataStream::Trailer& trailer,
                                                    const std::string& sender_identity) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent& e) {
        return e.has_send_stream_trailer() && e.send_stream_trailer().async_id() == async_id;
      },
      [](const proto::FfiEvent& e, std::promise<void>& pr) {
        const auto& cb = e.send_stream_trailer();
        if (!cb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_send_stream_trailer();
  msg->set_local_participant_handle(local_participant_handle);
  *msg->mutable_trailer() = trailer;
  msg->set_sender_identity(sender_identity);
  msg->set_request_async_id(async_id);

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_send_stream_trailer()) {
      logAndThrow("FfiResponse missing send_stream_trailer");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::defineSchemaAsync(std::uint64_t local_participant_handle, const DataTrackSchemaId& schema_id,
                                               const std::string& definition) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent& event) {
        return event.has_define_schema() && event.define_schema().async_id() == async_id;
      },
      [](const proto::FfiEvent& event, std::promise<void>& pr) {
        const auto& cb = event.define_schema();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_define_schema();
  msg->set_local_participant_handle(local_participant_handle);
  *msg->mutable_schema_id() = toProto(schema_id);
  msg->set_definition(definition);
  msg->set_request_async_id(async_id);

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_define_schema()) {
      logAndThrow("FfiResponse missing define_schema");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<std::string> FfiClient::getSchemaAsync(std::uint64_t local_participant_handle,
                                                   const DataTrackSchemaId& schema_id,
                                                   const std::string& participant_identity) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<std::string>(
      async_id,
      [async_id](const proto::FfiEvent& event) {
        return event.has_get_schema() && event.get_schema().async_id() == async_id;
      },
      [](const proto::FfiEvent& event, std::promise<std::string>& pr) {
        const auto& cb = event.get_schema();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value(cb.definition());
      });

  // Build and send the request
  proto::FfiRequest req;
  auto* msg = req.mutable_get_schema();
  msg->set_local_participant_handle(local_participant_handle);
  *msg->mutable_schema_id() = toProto(schema_id);
  msg->set_participant_identity(participant_identity);
  msg->set_request_async_id(async_id);

  try {
    const proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_get_schema()) {
      logAndThrow("FfiResponse missing get_schema");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

} // namespace livekit
