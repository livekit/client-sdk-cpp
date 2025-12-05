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
#include "e2ee.pb.h"
#include "ffi.pb.h"
#include "livekit/ffi_client.h"
#include "livekit/ffi_handle.h"
#include "livekit/room.h" // TODO, maybe avoid circular deps by moving RoomOptions to a room_types.h ?
#include "livekit/rpc_error.h"
#include "livekit/track.h"
#include "livekit_ffi.h"
#include "room.pb.h"
#include "room_proto_converter.h"

namespace livekit {

FfiClient::FfiClient() {
  livekit_ffi_initialize(&LivekitFfiCallback, false, LIVEKIT_BUILD_FLAVOR,
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
std::future<proto::ConnectCallback>
FfiClient::connectAsync(const std::string &url, const std::string &token,
                        const RoomOptions &options) {

  proto::FfiRequest req;
  auto *connect = req.mutable_connect();
  connect->set_url(url);
  connect->set_token(token);
  auto *opts = connect->mutable_options();
  opts->set_auto_subscribe(options.auto_subscribe);
  opts->set_dynacast(options.dynacast);
  std::cout << "connectAsync " << std::endl;
  // --- E2EE / encryption (optional) ---
  if (options.e2ee.has_value()) {
    std::cout << "connectAsync e2ee " << std::endl;
    const E2EEOptions &eo = *options.e2ee;

    // Use the non-deprecated encryption field
    auto *enc = opts->mutable_encryption();

    enc->set_encryption_type(
        static_cast<proto::EncryptionType>(eo.encryption_type));

    auto *kp = enc->mutable_key_provider_options();
    kp->set_shared_key(eo.shared_key);
    kp->set_ratchet_salt(eo.ratchet_salt);
    kp->set_failure_tolerance(eo.failure_tolerance);
    kp->set_ratchet_window_size(eo.ratchet_window_size);
  }

  // --- RTC configuration (optional) ---
  if (options.rtc_config.has_value()) {
    std::cout << "options.rtc_config.has_value() " << std::endl;
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
  std::cout << "connectAsync sendRequest  " << std::endl;
  proto::FfiResponse resp = sendRequest(req);
  if (!resp.has_connect()) {
    throw std::runtime_error("FfiResponse missing connect");
  }

  const AsyncId async_id = resp.connect().async_id();

  // Now we register an async op that completes with RoomInfo
  return registerAsync<proto::ConnectCallback>(
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
}

// Track APIs Implementation
std::future<std::vector<RtcStats>>
FfiClient::getTrackStatsAsync(uintptr_t track_handle) {
  proto::FfiRequest req;
  auto *get_stats_req = req.mutable_get_stats();
  get_stats_req->set_track_handle(track_handle);
  proto::FfiResponse resp = sendRequest(req);
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

// Participant APIs Implementation
std::future<proto::OwnedTrackPublication>
FfiClient::publishTrackAsync(std::uint64_t local_participant_handle,
                             std::uint64_t track_handle,
                             const TrackPublishOptions &options) {
  proto::FfiRequest req;
  auto *msg = req.mutable_publish_track();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_track_handle(track_handle);
  auto optionProto = toProto(options);
  msg->mutable_options()->CopyFrom(optionProto);

  proto::FfiResponse resp = sendRequest(req);
  if (!resp.has_publish_track()) {
    throw std::runtime_error("FfiResponse missing publish_track");
  }
  const AsyncId async_id = resp.publish_track().async_id();
  return registerAsync<proto::OwnedTrackPublication>(
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
}

std::future<void>
FfiClient::unpublishTrackAsync(std::uint64_t local_participant_handle,
                               const std::string &track_sid,
                               bool stop_on_unpublish) {
  proto::FfiRequest req;
  auto *msg = req.mutable_unpublish_track();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_track_sid(track_sid);
  msg->set_stop_on_unpublish(stop_on_unpublish);
  proto::FfiResponse resp = sendRequest(req);
  if (!resp.has_unpublish_track()) {
    throw std::runtime_error("FfiResponse missing unpublish_track");
  }
  const AsyncId async_id = resp.unpublish_track().async_id();
  return registerAsync<void>(
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
}

std::future<void> FfiClient::publishDataAsync(
    std::uint64_t local_participant_handle, const std::uint8_t *data_ptr,
    std::uint64_t data_len, bool reliable,
    const std::vector<std::string> &destination_identities,
    const std::string &topic) {
  proto::FfiRequest req;
  auto *msg = req.mutable_publish_data();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_data_ptr(reinterpret_cast<std::uint64_t>(data_ptr));
  msg->set_data_len(data_len);
  msg->set_reliable(reliable);
  msg->set_topic(topic);
  for (const auto &id : destination_identities) {
    msg->add_destination_identities(id);
  }

  proto::FfiResponse resp = sendRequest(req);
  if (!resp.has_publish_data()) {
    throw std::runtime_error("FfiResponse missing publish_data");
  }
  const AsyncId async_id = resp.publish_data().async_id();
  return registerAsync<void>(
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
}

std::future<void> FfiClient::publishTranscriptionAsync(
    std::uint64_t local_participant_handle,
    const std::string &participant_identity, const std::string &track_id,
    const std::vector<proto::TranscriptionSegment> &segments) {
  proto::FfiRequest req;
  auto *msg = req.mutable_publish_transcription();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_participant_identity(participant_identity);
  msg->set_track_id(track_id);
  for (const auto &seg : segments) {
    auto *dst = msg->add_segments();
    dst->CopyFrom(seg);
  }
  proto::FfiResponse resp = sendRequest(req);
  if (!resp.has_publish_transcription()) {
    throw std::runtime_error("FfiResponse missing publish_transcription");
  }
  const AsyncId async_id = resp.publish_transcription().async_id();
  return registerAsync<void>(
      [async_id](const proto::FfiEvent &event) {
        return event.has_publish_transcription() &&
               event.publish_transcription().async_id() == async_id;
      },
      [](const proto::FfiEvent &event, std::promise<void> &pr) {
        const auto &cb = event.publish_transcription();
        if (cb.has_error() && !cb.error().empty()) {
          pr.set_exception(
              std::make_exception_ptr(std::runtime_error(cb.error())));
          return;
        }
        pr.set_value();
      });
}

std::future<void> FfiClient::publishSipDtmfAsync(
    std::uint64_t local_participant_handle, std::uint32_t code,
    const std::string &digit,
    const std::vector<std::string> &destination_identities) {
  proto::FfiRequest req;
  auto *msg = req.mutable_publish_sip_dtmf();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_code(code);
  msg->set_digit(digit);
  for (const auto &id : destination_identities) {
    msg->add_destination_identities(id);
  }
  proto::FfiResponse resp = sendRequest(req);
  if (!resp.has_publish_sip_dtmf()) {
    throw std::runtime_error("FfiResponse missing publish_sip_dtmf");
  }
  const AsyncId async_id = resp.publish_sip_dtmf().async_id();
  return registerAsync<void>(
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
}

std::future<void>
FfiClient::setLocalMetadataAsync(std::uint64_t local_participant_handle,
                                 const std::string &metadata) {
  proto::FfiRequest req;
  auto *msg = req.mutable_set_local_metadata();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_metadata(metadata);
  proto::FfiResponse resp = sendRequest(req);
  if (!resp.has_set_local_metadata()) {
    throw std::runtime_error("FfiResponse missing set_local_metadata");
  }
  const AsyncId async_id = resp.set_local_metadata().async_id();
  return registerAsync<void>(
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
}

std::future<void>
FfiClient::captureAudioFrameAsync(std::uint64_t source_handle,
                                  const proto::AudioFrameBufferInfo &buffer) {
  proto::FfiRequest req;
  auto *msg = req.mutable_capture_audio_frame();
  msg->set_source_handle(source_handle);
  msg->mutable_buffer()->CopyFrom(buffer);

  proto::FfiResponse resp = sendRequest(req);
  if (!resp.has_capture_audio_frame()) {
    throw std::runtime_error("FfiResponse missing capture_audio_frame");
  }

  const AsyncId async_id = resp.capture_audio_frame().async_id();

  return registerAsync<void>(
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
}

std::future<std::string>
FfiClient::performRpcAsync(std::uint64_t local_participant_handle,
                           const std::string &destination_identity,
                           const std::string &method,
                           const std::string &payload,
                           std::optional<std::uint32_t> response_timeout_ms) {
  proto::FfiRequest req;
  auto *msg = req.mutable_perform_rpc();
  msg->set_local_participant_handle(local_participant_handle);
  msg->set_destination_identity(destination_identity);
  msg->set_method(method);
  msg->set_payload(payload);
  if (response_timeout_ms.has_value()) {
    msg->set_response_timeout_ms(*response_timeout_ms);
  }
  proto::FfiResponse resp = sendRequest(req);
  if (!resp.has_perform_rpc()) {
    throw std::runtime_error("FfiResponse missing perform_rpc");
  }
  const AsyncId async_id = resp.perform_rpc().async_id();
  return registerAsync<std::string>(
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
}

} // namespace livekit
