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

#include "livekit/audio_frame.h"
#include "livekit/audio_source.h"
#include "livekit/livekit.h"
#include "livekit/local_audio_track.h"
#include "livekit/local_participant.h"
#include "livekit/local_track_publication.h"
#include "livekit/local_video_track.h"
#include "livekit/room.h"
#include "livekit/track.h"
#include "livekit/video_frame.h"
#include "livekit/video_source.h"

#include <cassert>
#include <iostream>
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
  // eliminates the risk of deadlock if the SDK delivers callbacks synchronously
  // during Connect().
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

    if (!connected_) {
      std::cerr << "[warn] Attempting to disconnect an already disconnected "
                   "bridge. Things may not disconnect properly.\n";
    }

    connected_ = false;
    connecting_ = false;

    for (auto &track : published_audio_tracks_) {
      track->release();
    }
    for (auto &track : published_video_tracks_) {
      track->release();
    }
    published_audio_tracks_.clear();
    published_video_tracks_.clear();

    // Room destructor handles stopping all reader threads
    room_.reset();

    if (sdk_initialized_) {
      sdk_initialized_ = false;
      should_shutdown_sdk = true;
    }
  }

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

std::shared_ptr<BridgeAudioTrack>
LiveKitBridge::createAudioTrack(const std::string &name, int sample_rate,
                                int num_channels, livekit::TrackSource source) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!connected_ || !room_) {
    throw std::runtime_error(
        "createAudioTrack requires an active connection; call connect() first");
  }

  // 1. Create audio source (real-time mode, queue_size_ms=0)
  auto audio_source =
      std::make_shared<livekit::AudioSource>(sample_rate, num_channels, 0);

  // 2. Create local audio track
  auto track =
      livekit::LocalAudioTrack::createLocalAudioTrack(name, audio_source);

  // 3. Publish with the caller-specified source
  livekit::TrackPublishOptions opts;
  opts.source = source;

  auto lp = room_->localParticipant();
  assert(lp != nullptr);

  lp->publishTrack(track, opts);
  auto publication = track->publication();

  // 4. Wrap in handle and retain a reference
  auto bridge_track = std::shared_ptr<BridgeAudioTrack>(new BridgeAudioTrack(
      name, sample_rate, num_channels, std::move(audio_source),
      std::move(track), publication, lp));
  published_audio_tracks_.emplace_back(bridge_track);
  return bridge_track;
}

std::shared_ptr<BridgeVideoTrack>
LiveKitBridge::createVideoTrack(const std::string &name, int width, int height,
                                livekit::TrackSource source) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!connected_ || !room_) {
    throw std::runtime_error(
        "createVideoTrack requires an active connection; call connect() first");
  }

  // 1. Create video source
  auto video_source = std::make_shared<livekit::VideoSource>(width, height);

  // 2. Create local video track
  auto track =
      livekit::LocalVideoTrack::createLocalVideoTrack(name, video_source);

  // 3. Publish with the caller-specified source
  livekit::TrackPublishOptions opts;
  opts.source = source;

  auto lp = room_->localParticipant();
  assert(lp != nullptr);

  lp->publishTrack(track, opts);
  auto publication = track->publication();

  // 4. Wrap in handle and retain a reference
  auto bridge_track = std::shared_ptr<BridgeVideoTrack>(
      new BridgeVideoTrack(name, width, height, std::move(video_source),
                           std::move(track), publication, lp));
  published_video_tracks_.emplace_back(bridge_track);
  return bridge_track;
}

// ---------------------------------------------------------------
// Incoming frame callbacks
// ---------------------------------------------------------------

void LiveKitBridge::setOnAudioFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source,
    AudioFrameCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!room_) {
    std::cerr << "[warn] setOnAudioFrameCallback called before connect(); "
                 "ignored\n";
    return;
  }
  room_->setOnAudioFrameCallback(participant_identity, source,
                                 std::move(callback));
}

void LiveKitBridge::setOnVideoFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source,
    VideoFrameCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!room_) {
    std::cerr << "[warn] setOnVideoFrameCallback called before connect(); "
                 "ignored\n";
    return;
  }
  room_->setOnVideoFrameCallback(participant_identity, source,
                                 std::move(callback));
}

void LiveKitBridge::clearOnAudioFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!room_) {
    return;
  }
  room_->clearOnAudioFrameCallback(participant_identity, source);
}

void LiveKitBridge::clearOnVideoFrameCallback(
    const std::string &participant_identity, livekit::TrackSource source) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!room_) {
    return;
  }
  room_->clearOnVideoFrameCallback(participant_identity, source);
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
    if (track->name() == track_name && !track->isReleased()) {
      if (action == rpc::track_control::Action::kActionMute) {
        track->mute();
      } else {
        track->unmute();
      }
      return;
    }
  }

  for (auto &track : published_video_tracks_) {
    if (track->name() == track_name && !track->isReleased()) {
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
