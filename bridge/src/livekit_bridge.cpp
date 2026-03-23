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
#include "livekit_bridge/rpc_constants.h"
#include "rpc_controller.h"

#include "livekit/livekit.h"
#include "livekit/lk_log.h"
#include "livekit/local_audio_track.h"
#include "livekit/local_participant.h"
#include "livekit/local_video_track.h"
#include "livekit/room.h"

#include <cassert>
#include <stdexcept>

namespace livekit_bridge {

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

  // ---- Phase 3: commit under lock ----
  livekit::LocalParticipant *lp = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    room_ = std::move(room);
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

  bool should_shutdown_sdk = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ && !room_) {
      LK_LOG_DEBUG("disconnect() called on an already disconnected bridge");
      return;
    }

    connected_ = false;
    connecting_ = false;

    // Tear down the room (Room destructor stops all readers and cleans up)
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

std::shared_ptr<livekit::LocalAudioTrack> LiveKitBridge::createAudioTrack(
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

std::shared_ptr<livekit::LocalVideoTrack> LiveKitBridge::createVideoTrack(
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
// Incoming frame callbacks (delegates to Room)
// ---------------------------------------------------------------

void LiveKitBridge::setOnAudioFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source,
    AudioFrameCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (room_) {
    room_->setOnAudioFrameCallback(participant_identity, source,
                                   std::move(callback));
  } else {
    LK_LOG_WARN("setOnAudioFrameCallback called before connect(); "
                "callback will not be registered");
  }
}

void LiveKitBridge::setOnVideoFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source,
    VideoFrameCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (room_) {
    room_->setOnVideoFrameCallback(participant_identity, source,
                                   std::move(callback));
  } else {
    LK_LOG_WARN("setOnVideoFrameCallback called before connect(); "
                "callback will not be registered");
  }
}

void LiveKitBridge::clearOnAudioFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (room_) {
    room_->clearOnAudioFrameCallback(participant_identity, source);
  }
}

void LiveKitBridge::clearOnVideoFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (room_) {
    room_->clearOnVideoFrameCallback(participant_identity, source);
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
  } catch (const livekit::RpcError &e) {
    LK_LOG_ERROR("RPC error: {}", e.what());
    return std::nullopt;
  } catch (const std::exception &e) {
    LK_LOG_ERROR("RPC exception: {}", e.what());
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
  } catch (const livekit::RpcError &e) {
    LK_LOG_ERROR("RPC error: {}", e.what());
    return false;
  } catch (const std::exception &e) {
    LK_LOG_ERROR("RPC exception: {}", e.what());
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
  } catch (const livekit::RpcError &e) {
    LK_LOG_ERROR("RPC error: {}", e.what());
    return false;
  } catch (const std::exception &e) {
    LK_LOG_ERROR("RPC exception: {}", e.what());
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
  } catch (const livekit::RpcError &e) {
    LK_LOG_ERROR("RPC error: {}", e.what());
    return false;
  } catch (const std::exception &e) {
    LK_LOG_ERROR("RPC exception: {}", e.what());
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
  } catch (const livekit::RpcError &e) {
    LK_LOG_ERROR("RPC error: {}", e.what());
    return false;
  } catch (const std::exception &e) {
    LK_LOG_ERROR("RPC exception: {}", e.what());
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

} // namespace livekit_bridge
