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

#include "livekit_bridge/livekit_bridge.h"
#include "bridge_room_delegate.h"

#include "livekit/audio_frame.h"
#include "livekit/audio_source.h"
#include "livekit/audio_stream.h"
#include "livekit/livekit.h"
#include "livekit/local_audio_track.h"
#include "livekit/local_participant.h"
#include "livekit/local_track_publication.h"
#include "livekit/local_video_track.h"
#include "livekit/room.h"
#include "livekit/track.h"
#include "livekit/video_frame.h"
#include "livekit/video_source.h"
#include "livekit/video_stream.h"

#include <cassert>
#include <iostream>
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
    : connected_(false), connecting_(false), sdk_initialized_(false) {}

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
      livekit::initialize(livekit::LogSink::kConsole);
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
  {
    std::lock_guard<std::mutex> lock(mutex_);
    room_ = std::move(room);
    delegate_ = std::move(delegate);
    connected_ = true;
    connecting_ = false;
  }
  return true;
}

void LiveKitBridge::disconnect() {
  // Collect threads to join outside the lock to avoid deadlock.
  std::vector<std::thread> threads_to_join;
  bool should_shutdown_sdk = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_) {
      std::cerr << "[LiveKitBridge] Attempting to disconnect an already "
                   "disconnected bridge. Things may not disconnect properly.\n";
    }

    connected_ = false;
    connecting_ = false;

    // Release all published tracks while the room/participant are still alive.
    // This calls unpublishTrack() on each, ensuring participant_ is valid.
    for (auto &track : published_audio_tracks_) {
      track->release();
    }
    for (auto &track : published_video_tracks_) {
      track->release();
    }
    published_audio_tracks_.clear();
    published_video_tracks_.clear();

    // Close all streams (unblocks read loops) and collect threads
    for (auto &[key, reader] : active_readers_) {
      if (reader.audio_stream) {
        reader.audio_stream->close();
      }
      if (reader.video_stream) {
        reader.video_stream->close();
      }
      if (reader.thread.joinable()) {
        threads_to_join.emplace_back(std::move(reader.thread));
      }
    }
    active_readers_.clear();

    // Clear callback registrations
    audio_callbacks_.clear();
    video_callbacks_.clear();

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

  // Join threads outside the lock
  for (auto &t : threads_to_join) {
    if (t.joinable()) {
      t.join();
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

std::shared_ptr<BridgeAudioTrack>
LiveKitBridge::createAudioTrack(const std::string &name, int sample_rate,
                                int num_channels, livekit::TrackSource source) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!connected_ || !room_) {
    throw std::runtime_error(
        "LiveKitBridge::createAudioTrack: not connected to a room");
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

  auto publication = room_->localParticipant()->publishTrack(track, opts);

  // 4. Wrap in handle and retain a reference
  auto bridge_track = std::shared_ptr<BridgeAudioTrack>(new BridgeAudioTrack(
      name, sample_rate, num_channels, std::move(audio_source),
      std::move(track), std::move(publication), room_->localParticipant()));
  published_audio_tracks_.emplace_back(bridge_track);
  return bridge_track;
}

std::shared_ptr<BridgeVideoTrack>
LiveKitBridge::createVideoTrack(const std::string &name, int width, int height,
                                livekit::TrackSource source) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!connected_ || !room_) {
    throw std::runtime_error(
        "LiveKitBridge::createVideoTrack: not connected to a room");
  }

  // 1. Create video source
  auto video_source = std::make_shared<livekit::VideoSource>(width, height);

  // 2. Create local video track
  auto track =
      livekit::LocalVideoTrack::createLocalVideoTrack(name, video_source);

  // 3. Publish with the caller-specified source
  livekit::TrackPublishOptions opts;
  opts.source = source;

  auto publication = room_->localParticipant()->publishTrack(track, opts);

  // 4. Wrap in handle and retain a reference
  auto bridge_track = std::shared_ptr<BridgeVideoTrack>(new BridgeVideoTrack(
      name, width, height, std::move(video_source), std::move(track),
      std::move(publication), room_->localParticipant()));
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
    std::cerr << "[LiveKitBridge] Failed to create AudioStream for "
              << key.identity << "\n";
    return old_thread;
  }

  auto stream_copy = stream; // captured by the thread

  ActiveReader reader;
  reader.audio_stream = std::move(stream);
  reader.is_audio = true;
  reader.thread = std::thread([stream_copy, cb]() {
    livekit::AudioFrameEvent ev;
    while (stream_copy->read(ev)) {
      try {
        cb(ev.frame);
      } catch (const std::exception &e) {
        std::cerr << "[LiveKitBridge] Audio callback exception: " << e.what()
                  << "\n";
      }
    }
  });

  active_readers_[key] = std::move(reader);
  if (active_readers_.size() > kMaxActiveReaders) {
    std::cerr << "[LiveKitBridge] More than expected active readers. Need to "
                 "evaluate how much to expect/support.";
    "solution";
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
    std::cerr << "[LiveKitBridge] Failed to create VideoStream for "
              << key.identity << "\n";
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
        std::cerr << "[LiveKitBridge] Video callback exception: " << e.what()
                  << "\n";
      }
    }
  });

  active_readers_[key] = std::move(reader);
  if (active_readers_.size() > kMaxActiveReaders) {
    std::cerr << "[LiveKitBridge] More than expected active readers. Need to "
                 "evaluate how much to expect/support.";
  }
  return old_thread;
}

} // namespace livekit_bridge
