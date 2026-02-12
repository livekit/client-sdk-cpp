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

LiveKitBridge::LiveKitBridge() = default;

LiveKitBridge::~LiveKitBridge() { disconnect(); }

// ---------------------------------------------------------------
// Connection
// ---------------------------------------------------------------

bool LiveKitBridge::connect(const std::string &url, const std::string &token) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (connected_) {
    return true; // already connected
  }

  // Initialize the LiveKit SDK (idempotent)
  if (!sdk_initialized_) {
    livekit::initialize(livekit::LogSink::kConsole);
    sdk_initialized_ = true;
  }

  // Create room and delegate
  room_ = std::make_unique<livekit::Room>();
  delegate_ = std::make_unique<BridgeRoomDelegate>(*this);
  room_->setDelegate(delegate_.get());

  // Connect with auto_subscribe enabled
  livekit::RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  bool result = room_->Connect(url, token, options);
  if (!result) {
    room_->setDelegate(nullptr);
    delegate_.reset();
    room_.reset();
    return false;
  }

  connected_ = true;
  return true;
}

void LiveKitBridge::disconnect() {
  // Collect threads to join outside the lock to avoid deadlock.
  std::vector<std::thread> threads_to_join;
  bool should_shutdown_sdk = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_) {
      return;
    }

    connected_ = false;

    // Close all streams (unblocks read loops) and collect threads
    for (auto &[key, reader] : active_readers_) {
      if (reader.audio_stream) {
        reader.audio_stream->close();
      }
      if (reader.video_stream) {
        reader.video_stream->close();
      }
      if (reader.thread.joinable()) {
        threads_to_join.push_back(std::move(reader.thread));
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
                                int num_channels) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!connected_ || !room_) {
    throw std::runtime_error(
        "LiveKitBridge::createAudioTrack: not connected to a room");
  }

  // 1. Create audio source (real-time mode, queue_size_ms=0)
  auto source = std::make_shared<livekit::AudioSource>(sample_rate,
                                                       num_channels, 0);

  // 2. Create local audio track
  auto track = livekit::LocalAudioTrack::createLocalAudioTrack(name, source);

  // 3. Publish with sensible defaults
  livekit::TrackPublishOptions opts;
  opts.source = livekit::TrackSource::SOURCE_MICROPHONE;

  auto publication =
      room_->localParticipant()->publishTrack(track, opts);

  // 4. Wrap in RAII handle
  return std::shared_ptr<BridgeAudioTrack>(
      new BridgeAudioTrack(name, sample_rate, num_channels, std::move(source),
                           std::move(track), std::move(publication),
                           room_->localParticipant()));
}

std::shared_ptr<BridgeVideoTrack>
LiveKitBridge::createVideoTrack(const std::string &name, int width,
                                int height) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!connected_ || !room_) {
    throw std::runtime_error(
        "LiveKitBridge::createVideoTrack: not connected to a room");
  }

  // 1. Create video source
  auto source = std::make_shared<livekit::VideoSource>(width, height);

  // 2. Create local video track
  auto track = livekit::LocalVideoTrack::createLocalVideoTrack(name, source);

  // 3. Publish with sensible defaults
  livekit::TrackPublishOptions opts;
  opts.source = livekit::TrackSource::SOURCE_CAMERA;

  auto publication =
      room_->localParticipant()->publishTrack(track, opts);

  // 4. Wrap in RAII handle
  return std::shared_ptr<BridgeVideoTrack>(
      new BridgeVideoTrack(name, width, height, std::move(source),
                           std::move(track), std::move(publication),
                           room_->localParticipant()));
}

// ---------------------------------------------------------------
// Incoming frame callbacks
// ---------------------------------------------------------------

void LiveKitBridge::registerOnAudioFrame(
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

void LiveKitBridge::registerOnVideoFrame(
    const std::string &participant_identity, livekit::TrackSource source,
    VideoFrameCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);

  CallbackKey key{participant_identity, source};
  video_callbacks_[key] = std::move(callback);
}

void LiveKitBridge::unregisterOnAudioFrame(
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

void LiveKitBridge::unregisterOnVideoFrame(
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
  std::lock_guard<std::mutex> lock(mutex_);

  CallbackKey key{participant_identity, source};

  if (track->kind() == livekit::TrackKind::KIND_AUDIO) {
    auto it = audio_callbacks_.find(key);
    if (it != audio_callbacks_.end()) {
      startAudioReader(key, track, it->second);
    }
  } else if (track->kind() == livekit::TrackKind::KIND_VIDEO) {
    auto it = video_callbacks_.find(key);
    if (it != video_callbacks_.end()) {
      startVideoReader(key, track, it->second);
    }
  }
}

void LiveKitBridge::onTrackUnsubscribed(
    const std::string &participant_identity, livekit::TrackSource source) {
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

void LiveKitBridge::stopReader(const CallbackKey &key) {
  // Caller must hold mutex_.
  // Closes the stream and detaches the thread.
  // Used internally when replacing readers (e.g. in startAudioReader).
  auto thread = extractReaderThread(key);
  if (thread.joinable()) {
    thread.detach();
  }
}

void LiveKitBridge::startAudioReader(
    const CallbackKey &key, const std::shared_ptr<livekit::Track> &track,
    AudioFrameCallback cb) {
  // Caller must hold mutex_
  // Stop any existing reader for this key
  stopReader(key);

  livekit::AudioStream::Options opts;
  auto stream = livekit::AudioStream::fromTrack(track, opts);
  if (!stream) {
    std::cerr << "[LiveKitBridge] Failed to create AudioStream for "
              << key.identity << "\n";
    return;
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
}

void LiveKitBridge::startVideoReader(
    const CallbackKey &key, const std::shared_ptr<livekit::Track> &track,
    VideoFrameCallback cb) {
  // Caller must hold mutex_
  stopReader(key);

  livekit::VideoStream::Options opts;
  opts.format = livekit::VideoBufferType::RGBA;
  auto stream = livekit::VideoStream::fromTrack(track, opts);
  if (!stream) {
    std::cerr << "[LiveKitBridge] Failed to create VideoStream for "
              << key.identity << "\n";
    return;
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
}

} // namespace livekit_bridge
