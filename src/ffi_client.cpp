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
#include <iostream>

#include "build.h"
#include "e2ee.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/e2ee.h"
#include "livekit/ffi_handle.h"
#include "livekit/room.h"
#include "livekit/rpc_error.h"
#include "livekit/track.h"
#include "livekit_ffi.h"
#include "room.pb.h"
#include "room_proto_converter.h"

namespace livekit {

namespace {

std::string bytesToString(const std::vector<std::uint8_t> &b) {
  return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

// Helper to log errors and throw
inline void logAndThrow(const std::string &error_msg) {
  std::cerr << "LiveKit SDK Error: " << error_msg << std::endl;
  throw std::runtime_error(error_msg);
}

std::optional<FfiClient::AsyncId> ExtractAsyncId(const proto::FfiEvent &event) {
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

  // NOT async completion:
  case E::kRoomEvent:
  case E::kTrackEvent:
  case E::kVideoStreamEvent:
  case E::kAudioStreamEvent:
  case E::kByteStreamReaderEvent:
  case E::kTextStreamReaderEvent:
  case E::kRpcMethodInvocation:
  case E::kLogs:
  case E::kPanic:
  case E::MESSAGE_NOT_SET:
  default:
    return std::nullopt;
  }
}

} // namespace

FfiClient::~FfiClient() {
  assert(!initialized_.load() &&
         "LiveKit SDK was not shut down before process exit. "
         "Call livekit::shutdown().");
}

void FfiClient::shutdown() noexcept {
  if (!isInitialized()) {
    return;
  }
  initialized_.store(false, std::memory_order_release);
  livekit_ffi_dispose();
}

bool FfiClient::initialize(bool capture_logs) {
  if (isInitialized()) {
    return false;
  }
  initialized_.store(true, std::memory_order_release);
  livekit_ffi_initialize(&LivekitFfiCallback, capture_logs,
                         LIVEKIT_BUILD_FLAVOR, LIVEKIT_BUILD_VERSION_FULL);
  return true;
}

bool FfiClient::isInitialized() const noexcept {
  return initialized_.load(std::memory_order_acquire);
}

FfiClient::ListenerId
FfiClient::AddListener(const FfiClient::Listener &listener) {
  std::lock_guard<std::mutex> guard(lock_);
  FfiClient::ListenerId id = next_listener_id++;
  listeners_[id] = listener;
  return id;
}

void FfiClient::RemoveListener(ListenerId id) {
  std::lock_guard<std::mutex> guard(lock_);
  listeners_.erase(id);
}

proto::FfiResponse
FfiClient::sendRequest(const proto::FfiRequest &request) const {
  std::string bytes;
  if (!request.SerializeToString(&bytes) || bytes.empty()) {
    throw std::runtime_error("failed to serialize FfiRequest");
  }
  const uint8_t *resp_ptr = nullptr;
  size_t resp_len = 0;
  FfiHandleId handle =
      livekit_ffi_request(reinterpret_cast<const uint8_t *>(bytes.data()),
                          bytes.size(), &resp_ptr, &resp_len);
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
  std::unique_ptr<PendingBase> to_complete;
  std::vector<Listener> listeners_copy;
  {
    std::lock_guard<std::mutex> guard(lock_);

    // Complete pending future if this event is a callback with async_id
    if (auto async_id = ExtractAsyncId(event)) {
      auto it = pending_by_id_.find(*async_id);
      if (it != pending_by_id_.end() && it->second &&
          it->second->matches(event)) {
        to_complete = std::move(it->second);
        pending_by_id_.erase(it);
      }
    }

    // Snapshot listeners
    listeners_copy.reserve(listeners_.size());
    for (const auto &kv : listeners_) {
      listeners_copy.push_back(kv.second);
    }
  }
  // Run handler outside lock
  if (to_complete) {
    to_complete->complete(event);
  }

  // Notify listeners outside lock
  for (auto &listener : listeners_copy) {
    listener(event);
  }
}

void LivekitFfiCallback(const uint8_t *buf, size_t len) {
  proto::FfiEvent event;
  event.ParseFromArray(buf, len);

  FfiClient::instance().PushEvent(event);
}

FfiClient::AsyncId FfiClient::generateAsyncId() {
  return next_async_id_.fetch_add(1, std::memory_order_relaxed);
}

bool FfiClient::cancelPendingByAsyncId(AsyncId async_id) {
  std::unique_ptr<PendingBase> to_cancel;
  {
    std::lock_guard<std::mutex> guard(lock_);
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
std::future<T> FfiClient::registerAsync(
    AsyncId async_id, std::function<bool(const proto::FfiEvent &)> match,
    std::function<void(const proto::FfiEvent &, std::promise<T> &)> handler) {
  auto pending = std::make_unique<Pending<T>>();
  pending->async_id = async_id;
  auto fut = pending->promise.get_future();
  pending->match = std::move(match);
  pending->handler = std::move(handler);
  {
    std::lock_guard<std::mutex> guard(lock_);
    pending_by_id_.emplace(async_id, std::move(pending));
  }
  return fut;
}

// Room APIs Implementation
std::future<proto::ConnectCallback>
FfiClient::connectAsync(const std::string &url, const std::string &token,
                        const RoomOptions &options) {

  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<proto::ConnectCallback>(
      async_id,
      // match lambda: is this the connect event with our async_id?
      [async_id](const proto::FfiEvent &event) {
        return event.has_connect() && event.connect().async_id() == async_id;
      },
      // handler lambda: fill the promise with RoomInfo or an exception
      [](const proto::FfiEvent &event,
         std::promise<proto::ConnectCallback> &pr) {
        const auto &connectCb = event.connect();
        if (!connectCb.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(connectCb.error())));
          return;
        }

        pr.set_value(connectCb);
      });

  // Build and send the request
  proto::FfiRequest req;
  auto *connect = req.mutable_connect();
  connect->set_url(url);
  connect->set_token(token);
  connect->set_request_async_id(async_id);
  auto *opts = connect->mutable_options();
  opts->set_auto_subscribe(options.auto_subscribe);
  opts->set_dynacast(options.dynacast);
  // --- E2EE / encryption (optional) ---
  if (options.encryption.has_value()) {
    const E2EEOptions &e2ee = *options.encryption;
    const auto &kpo = e2ee.key_provider_options;

    auto *enc = opts->mutable_encryption();
    enc->set_encryption_type(
        static_cast<proto::EncryptionType>(e2ee.encryption_type));
    auto *kp = enc->mutable_key_provider_options();
    // shared_key is optional. If not set, leave the field unset/cleared.
    if (kpo.shared_key && !kpo.shared_key->empty()) {
      kp->set_shared_key(bytesToString(*kpo.shared_key));
    } else {
      kp->clear_shared_key();
    }
    // Only set ratchet_salt if caller overrides. Otherwise clear so Rust side
    // uses default.
    if (!kpo.ratchet_salt.empty() &&
        kpo.ratchet_salt !=
            std::vector<std::uint8_t>(
                kDefaultRatchetSalt,
                kDefaultRatchetSalt +
                    std::char_traits<char>::length(kDefaultRatchetSalt))) {
      kp->set_ratchet_salt(bytesToString(kpo.ratchet_salt));
    } else {
      kp->clear_ratchet_salt();
    }
    // Same idea for window size / tolerance: set only on override; otherwise
    // clear.
    if (kpo.ratchet_window_size != kDefaultRatchetWindowSize) {
      kp->set_ratchet_window_size(kpo.ratchet_window_size);
    } else {
      kp->clear_ratchet_window_size();
    }
    if (kpo.failure_tolerance != kDefaultFailureTolerance) {
      kp->set_failure_tolerance(kpo.failure_tolerance);
    } else {
      kp->clear_failure_tolerance();
    }
  }

  // --- RTC configuration (optional) ---
  if (options.rtc_config.has_value()) {
    const RtcConfig &rc = *options.rtc_config;
    auto *rtc = opts->mutable_rtc_config();

    rtc->set_ice_transport_type(
        static_cast<proto::IceTransportType>(rc.ice_transport_type));
    rtc->set_continual_gathering_policy(
        static_cast<proto::ContinualGatheringPolicy>(
            rc.continual_gathering_policy));

    for (const IceServer &ice : rc.ice_servers) {
      auto *s = rtc->add_ice_servers();

      // proto: repeated string urls = 1
      if (!ice.url.empty()) {
        s->add_urls(ice.url);
      }
      if (!ice.username.empty()) {
        s->set_username(ice.username);
      }
      if (!ice.credential.empty()) {
        // proto: password = 3
        s->set_password(ice.credential);
      }
    }
  }

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_connect()) {
      logAndThrow("FfiResponse missing connect");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

// Track APIs Implementation
std::future<std::vector<RtcStats>>
FfiClient::getTrackStatsAsync(uintptr_t track_handle) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<std::vector<RtcStats>>(
      async_id,
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

  // Build and send the request
  proto::FfiRequest req;
  auto *get_stats_req = req.mutable_get_stats();
  get_stats_req->set_track_handle(track_handle);
  get_stats_req->set_request_async_id(async_id);

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_get_stats()) {
      logAndThrow("FfiResponse missing get_stats");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

// Participant APIs Implementation
std::future<proto::OwnedTrackPublication>
FfiClient::publishTrackAsync(std::uint64_t local_participant_handle,
                             std::uint64_t track_handle,
                             const TrackPublishOptions &options) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<proto::OwnedTrackPublication>(
      async_id,
      // Match: is this our PublishTrackCallback?
      [async_id](const proto::FfiEvent &event) {
        return event.has_publish_track() &&
               event.publish_track().async_id() == async_id;
      },
      // Handler: resolve with publication or throw error
      [](const proto::FfiEvent &event,
         std::promise<proto::OwnedTrackPublication> &pr) {
        const auto &cb = event.publish_track();

        // Oneof message { string error = 2; OwnedTrackPublication publication =
        // 3; }
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        if (!cb.has_publication()) {
          pr.set_exception(std::make_exception_ptr(
              std::runtime_error("PublishTrackCallback missing publication")));
          return;
        }

        proto::OwnedTrackPublication pub = cb.publication();
        pr.set_value(std::move(pub));
      });

  // Build and send the request
  proto::FfiRequest req;
  auto *msg = req.mutable_publish_track();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_track_handle(track_handle);
  msg->set_request_async_id(async_id);
  auto optionProto = toProto(options);
  msg->mutable_options()->CopyFrom(optionProto);

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_publish_track()) {
      logAndThrow("FfiResponse missing publish_track");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void>
FfiClient::unpublishTrackAsync(std::uint64_t local_participant_handle,
                               const std::string &track_sid,
                               bool stop_on_unpublish) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent &event) {
        return event.has_unpublish_track() &&
               event.unpublish_track().async_id() == async_id;
      },
      [](const proto::FfiEvent &event, std::promise<void> &pr) {
        const auto &cb = event.unpublish_track();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto *msg = req.mutable_unpublish_track();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_track_sid(track_sid);
  msg->set_stop_on_unpublish(stop_on_unpublish);
  msg->set_request_async_id(async_id);

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_unpublish_track()) {
      logAndThrow("FfiResponse missing unpublish_track");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::publishDataAsync(
    std::uint64_t local_participant_handle, const std::uint8_t *data_ptr,
    std::uint64_t data_len, bool reliable,
    const std::vector<std::string> &destination_identities,
    const std::string &topic) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent &event) {
        return event.has_publish_data() &&
               event.publish_data().async_id() == async_id;
      },
      [](const proto::FfiEvent &event, std::promise<void> &pr) {
        const auto &cb = event.publish_data();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto *msg = req.mutable_publish_data();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_data_ptr(reinterpret_cast<std::uint64_t>(data_ptr));
  msg->set_data_len(data_len);
  msg->set_reliable(reliable);
  msg->set_topic(topic);
  msg->set_request_async_id(async_id);
  for (const auto &id : destination_identities) {
    msg->add_destination_identities(id);
  }

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_publish_data()) {
      logAndThrow("FfiResponse missing publish_data");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::publishSipDtmfAsync(
    std::uint64_t local_participant_handle, std::uint32_t code,
    const std::string &digit,
    const std::vector<std::string> &destination_identities) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent &event) {
        return event.has_publish_sip_dtmf() &&
               event.publish_sip_dtmf().async_id() == async_id;
      },
      [](const proto::FfiEvent &event, std::promise<void> &pr) {
        const auto &cb = event.publish_sip_dtmf();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto *msg = req.mutable_publish_sip_dtmf();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_code(code);
  msg->set_digit(digit);
  msg->set_request_async_id(async_id);
  for (const auto &id : destination_identities) {
    msg->add_destination_identities(id);
  }

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_publish_sip_dtmf()) {
      logAndThrow("FfiResponse missing publish_sip_dtmf");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void>
FfiClient::setLocalMetadataAsync(std::uint64_t local_participant_handle,
                                 const std::string &metadata) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent &event) {
        return event.has_set_local_metadata() &&
               event.set_local_metadata().async_id() == async_id;
      },
      [](const proto::FfiEvent &event, std::promise<void> &pr) {
        const auto &cb = event.set_local_metadata();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto *msg = req.mutable_set_local_metadata();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_metadata(metadata);
  msg->set_request_async_id(async_id);

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_set_local_metadata()) {
      logAndThrow("FfiResponse missing set_local_metadata");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void>
FfiClient::captureAudioFrameAsync(std::uint64_t source_handle,
                                  const proto::AudioFrameBufferInfo &buffer) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      // match predicate
      [async_id](const proto::FfiEvent &event) {
        return event.has_capture_audio_frame() &&
               event.capture_audio_frame().async_id() == async_id;
      },
      // completion handler
      [](const proto::FfiEvent &event, std::promise<void> &pr) {
        const auto &cb = event.capture_audio_frame();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto *msg = req.mutable_capture_audio_frame();
  msg->set_source_handle(source_handle);
  msg->set_request_async_id(async_id);
  msg->mutable_buffer()->CopyFrom(buffer);

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_capture_audio_frame()) {
      logAndThrow("FfiResponse missing capture_audio_frame");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<std::string>
FfiClient::performRpcAsync(std::uint64_t local_participant_handle,
                           const std::string &destination_identity,
                           const std::string &method,
                           const std::string &payload,
                           std::optional<std::uint32_t> response_timeout_ms) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<std::string>(
      async_id,
      // match predicate
      [async_id](const proto::FfiEvent &event) {
        return event.has_perform_rpc() &&
               event.perform_rpc().async_id() == async_id;
      },
      [](const proto::FfiEvent &event, std::promise<std::string> &pr) {
        const auto &cb = event.perform_rpc();

        if (cb.has_error()) {
          // RpcError is a proto message; convert to C++ RpcError and throw
          pr.set_exception(
              std::make_exception_ptr(RpcError::fromProto(cb.error())));
          return;
        }
        pr.set_value(cb.payload());
      });

  // Build and send the request
  proto::FfiRequest req;
  auto *msg = req.mutable_perform_rpc();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_destination_identity(destination_identity);
  msg->set_method(method);
  msg->set_payload(payload);
  msg->set_request_async_id(async_id);
  if (response_timeout_ms.has_value()) {
    msg->set_response_timeout_ms(*response_timeout_ms);
  }

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_perform_rpc()) {
      logAndThrow("FfiResponse missing perform_rpc");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::sendStreamHeaderAsync(
    std::uint64_t local_participant_handle,
    const proto::DataStream::Header &header,
    const std::vector<std::string> &destination_identities,
    const std::string &sender_identity) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent &e) {
        return e.has_send_stream_header() &&
               e.send_stream_header().async_id() == async_id;
      },
      [](const proto::FfiEvent &e, std::promise<void> &pr) {
        const auto &cb = e.send_stream_header();
        if (!cb.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto *msg = req.mutable_send_stream_header();
  msg->set_local_participant_handle(local_participant_handle);
  *msg->mutable_header() = header;
  msg->set_sender_identity(sender_identity);
  msg->set_request_async_id(async_id);
  for (const auto &id : destination_identities) {
    msg->add_destination_identities(id);
  }

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_send_stream_header()) {
      logAndThrow("FfiResponse missing send_stream_header");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void> FfiClient::sendStreamChunkAsync(
    std::uint64_t local_participant_handle,
    const proto::DataStream::Chunk &chunk,
    const std::vector<std::string> &destination_identities,
    const std::string &sender_identity) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent &e) {
        return e.has_send_stream_chunk() &&
               e.send_stream_chunk().async_id() == async_id;
      },
      [](const proto::FfiEvent &e, std::promise<void> &pr) {
        const auto &cb = e.send_stream_chunk();
        if (!cb.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto *msg = req.mutable_send_stream_chunk();
  msg->set_local_participant_handle(local_participant_handle);
  *msg->mutable_chunk() = chunk;
  msg->set_sender_identity(sender_identity);
  msg->set_request_async_id(async_id);
  for (const auto &id : destination_identities) {
    msg->add_destination_identities(id);
  }

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_send_stream_chunk()) {
      logAndThrow("FfiResponse missing send_stream_chunk");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

std::future<void>
FfiClient::sendStreamTrailerAsync(std::uint64_t local_participant_handle,
                                  const proto::DataStream::Trailer &trailer,
                                  const std::string &sender_identity) {
  // Generate client-side async_id first
  const AsyncId async_id = generateAsyncId();

  // Register the async handler BEFORE sending the request
  auto fut = registerAsync<void>(
      async_id,
      [async_id](const proto::FfiEvent &e) {
        return e.has_send_stream_trailer() &&
               e.send_stream_trailer().async_id() == async_id;
      },
      [](const proto::FfiEvent &e, std::promise<void> &pr) {
        const auto &cb = e.send_stream_trailer();
        if (!cb.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });

  // Build and send the request
  proto::FfiRequest req;
  auto *msg = req.mutable_send_stream_trailer();
  msg->set_local_participant_handle(local_participant_handle);
  *msg->mutable_trailer() = trailer;
  msg->set_sender_identity(sender_identity);
  msg->set_request_async_id(async_id);

  try {
    proto::FfiResponse resp = sendRequest(req);
    if (!resp.has_send_stream_trailer()) {
      logAndThrow("FfiResponse missing send_stream_trailer");
    }
  } catch (...) {
    cancelPendingByAsyncId(async_id);
    throw;
  }

  return fut;
}

} // namespace livekit
