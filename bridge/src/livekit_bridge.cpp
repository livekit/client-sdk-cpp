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

/// @file livekit_bridge.cpp
/// @brief Implementation of the LiveKitBridge high-level API.

#include "livekit_bridge/livekit_bridge.h"
#include "bridge_room_delegate.h"
#include "livekit_bridge/rpc_constants.h"
#include "rpc_controller.h"

#include "livekit/audio_frame.h"
#include "livekit/audio_stream.h"
#include "livekit/livekit.h"
#include "livekit/lk_log.h"
#include "livekit/local_audio_track.h"
#include "livekit/local_participant.h"
#include "livekit/local_video_track.h"
#include "livekit/room.h"
#include "livekit/track.h"
#include "livekit/video_frame.h"
#include "livekit/video_stream.h"

#include <cassert>
#include <stdexcept>

namespace livekit_bridge {

// ---------------------------------------------------------------
// CallbackKey
// ---------------------------------------------------------------

bool LiveKitBridge::CallbackKey::operator==(const CallbackKey &o) const {
  return identity == o.identity && source == o.source;
}

std::size_t
LiveKitBridge::CallbackKeyHash::operator()(const CallbackKey &k) const {
  std::size_t h1 = std::hash<std::string>{}(k.identity);
  std::size_t h2 = std::hash<int>{}(static_cast<int>(k.source));
  return h1 ^ (h2 << 1);
}

// ---------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------

LiveKitBridge::LiveKitBridge()
    : connected_(false), connecting_(false), sdk_initialized_(false),
      rpc_controller_(std::make_unique<RpcController>(
          [this](const rpc::track_control::Action &action,
                 const std::string &track_name) {
            executeTrackAction(action, track_name);
          })) {}

LiveKitBridge::~LiveKitBridge() { disconnect(); }

// ---------------------------------------------------------------
// Connection
// ---------------------------------------------------------------

bool LiveKitBridge::connect(const std::string &url, const std::string &token,
                            const livekit::RoomOptions &options) {
  // ---- Phase 1: quick check under lock ----
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (connected_) {
      return true; // already connected
    }

    if (connecting_) {
      return false; // another thread is already connecting
    }

    connecting_ = true;

    // Initialize the LiveKit SDK (idempotent)
    if (!sdk_initialized_) {
      livekit::initialize();
      sdk_initialized_ = true;
    }
  }

  // ---- Phase 2: create room and connect without holding the lock ----
  // This avoids blocking other threads during the network handshake and
  // eliminates the risk of deadlock if the SDK delivers delegate callbacks
  // synchronously during Connect().
  auto room = std::make_unique<livekit::Room>();
  assert(room != nullptr);

  bool result = room->Connect(url, token, options);
  if (!result) {
    std::lock_guard<std::mutex> lock(mutex_);
    connecting_ = false;
    return false;
  }

  // ---- Phase 3: commit and attach delegate under lock ----
  // Setting the delegate here (after Connect) ensures that any queued
  // onTrackSubscribed events are delivered only after
  // room_/delegate_/connected_ are all in a consistent state.

  auto delegate = std::make_unique<BridgeRoomDelegate>(*this);
  assert(delegate != nullptr);
  room->setDelegate(delegate.get());
  livekit::LocalParticipant *lp = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    room_ = std::move(room);
    delegate_ = std::move(delegate);
    connected_ = true;
    connecting_ = false;

    lp = room_->localParticipant();
    assert(lp != nullptr);
  }

  rpc_controller_->enable(lp);
  return true;
}

void LiveKitBridge::disconnect() {
  // Disable the RPC controller before tearing down the room. This unregisters
  // built-in handlers while the LocalParticipant is still alive.
  if (rpc_controller_ && rpc_controller_->isEnabled()) {
    rpc_controller_->disable();
  }

  // Clear all user set callbacks
  std::vector<CallbackKey> audio_keys;
  std::vector<CallbackKey> video_keys;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    audio_keys.reserve(audio_callbacks_.size());
    for (const auto &[key, _] : audio_callbacks_) {
      audio_keys.push_back(key);
    }
    video_keys.reserve(video_callbacks_.size());
    for (const auto &[key, _] : video_callbacks_) {
      video_keys.push_back(key);
    }
  }

  for (const auto &key : audio_keys) {
    clearOnAudioFrameCallback(key.identity, key.source);
  }
  for (const auto &key : video_keys) {
    clearOnVideoFrameCallback(key.identity, key.source);
  }

  bool should_shutdown_sdk = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ && !room_) {
      LK_LOG_DEBUG("disconnect() called on an already disconnected bridge");
      return;
    }

    connected_ = false;
    connecting_ = false;

    // Tear down the room
    if (room_) {
      room_->setDelegate(nullptr);
    }
    delegate_.reset();
    room_.reset();

    if (sdk_initialized_) {
      sdk_initialized_ = false;
      should_shutdown_sdk = true;
    }
  }

  // Shut down the SDK outside the lock (may block)
  if (should_shutdown_sdk) {
    livekit::shutdown();
  }
}

bool LiveKitBridge::isConnected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

// ---------------------------------------------------------------
// Track creation (publishing)
// ---------------------------------------------------------------

std::shared_ptr<livekit::LocalAudioTrack>
LiveKitBridge::createAudioTrack(
    const std::string &name,
    const std::shared_ptr<livekit::AudioSource> &source,
    livekit::TrackSource track_source) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!connected_ || !room_) {
    throw std::runtime_error(
        "createAudioTrack requires an active connection; call connect() first");
  }

  auto lp = room_->localParticipant();
  assert(lp != nullptr);

  auto track = lp->publishAudioTrack(name, source, track_source);

  published_audio_tracks_.emplace_back(track);
  return track;
}

std::shared_ptr<livekit::LocalVideoTrack>
LiveKitBridge::createVideoTrack(
    const std::string &name,
    const std::shared_ptr<livekit::VideoSource> &source,
    livekit::TrackSource track_source) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!connected_ || !room_) {
    throw std::runtime_error(
        "createVideoTrack requires an active connection; call connect() first");
  }

  auto lp = room_->localParticipant();
  assert(lp != nullptr);

  auto track = lp->publishVideoTrack(name, source, track_source);

  published_video_tracks_.emplace_back(track);
  return track;
}

// ---------------------------------------------------------------
// Incoming frame callbacks
// ---------------------------------------------------------------

void LiveKitBridge::setOnAudioFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source,
    AudioFrameCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);

  CallbackKey key{participant_identity, source};
  audio_callbacks_[key] = std::move(callback);

  // If there is already an active reader for this key (e.g., track was
  // subscribed before the callback was registered), we don't need to do
  // anything special -- the next time onTrackSubscribed fires it will
  // pick up the callback. However, since auto_subscribe is on, the track
  // may have already been subscribed. We don't have a way to retroactively
  // query subscribed tracks here, so the user should register callbacks
  // before connecting or before the remote participant joins. In practice,
  // the delegate fires onTrackSubscribed when the track arrives, so if we
  // register the callback first (before the participant joins), it will
  // be picked up.
}

void LiveKitBridge::setOnVideoFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source,
    VideoFrameCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);

  CallbackKey key{participant_identity, source};
  video_callbacks_[key] = std::move(callback);
}

void LiveKitBridge::clearOnAudioFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source) {
  std::thread thread_to_join;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    CallbackKey key{participant_identity, source};
    audio_callbacks_.erase(key);
    thread_to_join = extractReaderThread(key);
  }
  if (thread_to_join.joinable()) {
    thread_to_join.join();
  }
}

void LiveKitBridge::clearOnVideoFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source) {
  std::thread thread_to_join;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    CallbackKey key{participant_identity, source};
    video_callbacks_.erase(key);
    thread_to_join = extractReaderThread(key);
  }
  if (thread_to_join.joinable()) {
    thread_to_join.join();
  }
}

// ---------------------------------------------------------------
// RPC (delegates to RpcController)
// ---------------------------------------------------------------

std::optional<std::string>
LiveKitBridge::performRpc(const std::string &destination_identity,
                          const std::string &method, const std::string &payload,
                          const std::optional<double> &response_timeout) {

  if (!isConnected()) {
    return std::nullopt;
  }

  try {
    return rpc_controller_->performRpc(destination_identity, method, payload,
                                       response_timeout);
  } catch (const std::exception &e) {
    std::cerr << "[LiveKitBridge] Exception: " << e.what() << "\n";
    return std::nullopt;
  } catch (const std::runtime_error &e) {
    std::cerr << "[LiveKitBridge] Runtime error: " << e.what() << "\n";
    return std::nullopt;
  } catch (const livekit::RpcError &e) {
    std::cerr << "[LiveKitBridge] RPC error: " << e.what() << "\n";
    return std::nullopt;
  }
}

bool LiveKitBridge::registerRpcMethod(
    const std::string &method_name,
    livekit::LocalParticipant::RpcHandler handler) {

  if (!isConnected()) {
    return false;
  }
  try {
    rpc_controller_->registerRpcMethod(method_name, std::move(handler));
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[LiveKitBridge] Exception: " << e.what() << "\n";
    return false;
  } catch (const std::runtime_error &e) {
    std::cerr << "[LiveKitBridge] Runtime error: " << e.what() << "\n";
    return false;
  } catch (const livekit::RpcError &e) {
    std::cerr << "[LiveKitBridge] RPC error: " << e.what() << "\n";
    return false;
  }
}

bool LiveKitBridge::unregisterRpcMethod(const std::string &method_name) {
  if (!isConnected()) {
    return false;
  }
  try {
    rpc_controller_->unregisterRpcMethod(method_name);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[LiveKitBridge] Exception: " << e.what() << "\n";
    return false;
  } catch (const std::runtime_error &e) {
    std::cerr << "[LiveKitBridge] Runtime error: " << e.what() << "\n";
    return false;
  } catch (const livekit::RpcError &e) {
    std::cerr << "[LiveKitBridge] RPC error: " << e.what() << "\n";
    return false;
  }
}

bool LiveKitBridge::requestRemoteTrackMute(
    const std::string &destination_identity, const std::string &track_name) {
  if (!isConnected()) {
    return false;
  }
  try {
    rpc_controller_->requestRemoteTrackMute(destination_identity, track_name);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[LiveKitBridge] Exception: " << e.what() << "\n";
    return false;
  } catch (const std::runtime_error &e) {
    std::cerr << "[LiveKitBridge] Runtime error: " << e.what() << "\n";
    return false;
  } catch (const livekit::RpcError &e) {
    std::cerr << "[LiveKitBridge] RPC error: " << e.what() << "\n";
    return false;
  }
}

bool LiveKitBridge::requestRemoteTrackUnmute(
    const std::string &destination_identity, const std::string &track_name) {
  if (!isConnected()) {
    return false;
  }
  try {
    rpc_controller_->requestRemoteTrackUnmute(destination_identity, track_name);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[LiveKitBridge] Exception: " << e.what() << "\n";
    return false;
  } catch (const std::runtime_error &e) {
    std::cerr << "[LiveKitBridge] Runtime error: " << e.what() << "\n";
    return false;
  } catch (const livekit::RpcError &e) {
    std::cerr << "[LiveKitBridge] RPC error: " << e.what() << "\n";
    return false;
  }
}

// ---------------------------------------------------------------
// Track action callback for RpcController
// ---------------------------------------------------------------

void LiveKitBridge::executeTrackAction(const rpc::track_control::Action &action,
                                       const std::string &track_name) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto &track : published_audio_tracks_) {
    if (!track || !track->publication()) {
      continue;
    }
    if (track->name() == track_name) {
      if (action == rpc::track_control::Action::kActionMute) {
        track->mute();
      } else {
        track->unmute();
      }
      return;
    }
  }

  for (auto &track : published_video_tracks_) {
    if (!track || !track->publication()) {
      continue;
    }
    if (track->name() == track_name) {
      if (action == rpc::track_control::Action::kActionMute) {
        track->mute();
      } else {
        track->unmute();
      }
      return;
    }
  }

  throw livekit::RpcError(livekit::RpcError::ErrorCode::APPLICATION_ERROR,
                          "track not found: " + track_name);
}

// ---------------------------------------------------------------
// Internal: track subscribe / unsubscribe from delegate
// ---------------------------------------------------------------

void LiveKitBridge::onTrackSubscribed(
    const std::string &participant_identity, livekit::TrackSource source,
    const std::shared_ptr<livekit::Track> &track) {
  std::thread old_thread;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    CallbackKey key{participant_identity, source};

    if (track->kind() == livekit::TrackKind::KIND_AUDIO) {
      auto it = audio_callbacks_.find(key);
      if (it != audio_callbacks_.end()) {
        old_thread = startAudioReader(key, track, it->second);
      }
    } else if (track->kind() == livekit::TrackKind::KIND_VIDEO) {
      auto it = video_callbacks_.find(key);
      if (it != video_callbacks_.end()) {
        old_thread = startVideoReader(key, track, it->second);
      }
    }
  }
  // If this key already had a reader (e.g. track was re-subscribed), the old
  // reader's stream was closed inside startAudioReader/startVideoReader. We
  // join its thread here -- outside the lock -- to guarantee it has finished
  // invoking the old callback before we return.
  if (old_thread.joinable()) {
    old_thread.join();
  }
}

void LiveKitBridge::onTrackUnsubscribed(const std::string &participant_identity,
                                        livekit::TrackSource source) {
  std::thread thread_to_join;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    CallbackKey key{participant_identity, source};
    thread_to_join = extractReaderThread(key);
  }
  if (thread_to_join.joinable()) {
    thread_to_join.join();
  }
}

// ---------------------------------------------------------------
// Internal: reader thread management
// ---------------------------------------------------------------

std::thread LiveKitBridge::extractReaderThread(const CallbackKey &key) {
  // Caller must hold mutex_.
  // Closes the stream and extracts the thread for the caller to join.
  auto it = active_readers_.find(key);
  if (it == active_readers_.end()) {
    return {};
  }

  auto &reader = it->second;

  // Close the stream to unblock the read() loop
  if (reader.audio_stream) {
    reader.audio_stream->close();
  }
  if (reader.video_stream) {
    reader.video_stream->close();
  }

  auto thread = std::move(reader.thread);
  active_readers_.erase(it);
  return thread;
}

std::thread
LiveKitBridge::startAudioReader(const CallbackKey &key,
                                const std::shared_ptr<livekit::Track> &track,
                                AudioFrameCallback cb) {
  // Caller must hold mutex_.
  // Returns the old reader thread (if any) for the caller to join outside
  // the lock.
  auto old_thread = extractReaderThread(key);

  livekit::AudioStream::Options opts;
  auto stream = livekit::AudioStream::fromTrack(track, opts);
  if (!stream) {
    LK_LOG_ERROR("Failed to create AudioStream for {}", key.identity);
    return old_thread;
  }

  auto stream_copy = stream;

  ActiveReader reader;
  reader.audio_stream = std::move(stream);
  reader.is_audio = true;
  reader.thread = std::thread([stream_copy, cb]() {
    livekit::AudioFrameEvent ev;
    while (stream_copy->read(ev)) {
      try {
        cb(ev.frame);
      } catch (const std::exception &e) {
        LK_LOG_ERROR("Audio callback exception: {}", e.what());
      }
    }
  });

  active_readers_[key] = std::move(reader);
  if (active_readers_.size() > kMaxActiveReaders) {
    LK_LOG_WARN("More than expected active readers. Need to evaluate how much "
                "to expect/support.");
  }
  return old_thread;
}

std::thread
LiveKitBridge::startVideoReader(const CallbackKey &key,
                                const std::shared_ptr<livekit::Track> &track,
                                VideoFrameCallback cb) {
  // Caller must hold mutex_.
  // Returns the old reader thread (if any) for the caller to join outside
  // the lock.
  auto old_thread = extractReaderThread(key);

  livekit::VideoStream::Options opts;
  opts.format = livekit::VideoBufferType::RGBA;
  auto stream = livekit::VideoStream::fromTrack(track, opts);
  if (!stream) {
    LK_LOG_ERROR("Failed to create VideoStream for {}", key.identity);
    return old_thread;
  }

  auto stream_copy = stream;

  ActiveReader reader;
  reader.video_stream = std::move(stream);
  reader.is_audio = false;
  reader.thread = std::thread([stream_copy, cb]() {
    livekit::VideoFrameEvent ev;
    while (stream_copy->read(ev)) {
      try {
        cb(ev.frame, ev.timestamp_us);
      } catch (const std::exception &e) {
        LK_LOG_ERROR("Video callback exception: {}", e.what());
      }
    }
  });

  active_readers_[key] = std::move(reader);
  if (active_readers_.size() > kMaxActiveReaders) {
    LK_LOG_WARN("More than expected active readers. Need to evaluate how much "
                "to expect/support.");
  }
  return old_thread;
}

} // namespace livekit_bridge
