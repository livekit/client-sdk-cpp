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

#include "livekit/subscription_thread_dispatcher.h"

#include "livekit/lk_log.h"
#include "livekit/track.h"

#include <exception>
#include <utility>
#include <vector>

namespace livekit {

namespace {

const char *trackKindName(TrackKind kind) {
  if (kind == TrackKind::KIND_AUDIO)
    return "audio";
  if (kind == TrackKind::KIND_VIDEO)
    return "video";
  if (kind == TrackKind::KIND_UNKNOWN)
    return "unknown";

  return "unsupported";
}

} // namespace

SubscriptionThreadDispatcher::SubscriptionThreadDispatcher() = default;

SubscriptionThreadDispatcher::~SubscriptionThreadDispatcher() {
  LK_LOG_DEBUG("Destroying SubscriptionThreadDispatcher");
  stopAll();
}

void SubscriptionThreadDispatcher::setOnAudioFrameCallback(
    const std::string &participant_identity, TrackSource source,
    AudioFrameCallback callback, AudioStream::Options opts) {
  CallbackKey key{participant_identity, source};
  std::lock_guard<std::mutex> lock(lock_);
  const bool replacing = audio_callbacks_.find(key) != audio_callbacks_.end();
  audio_callbacks_[key] =
      RegisteredAudioCallback{std::move(callback), std::move(opts)};
  LK_LOG_DEBUG("Registered audio frame callback for participant={} source={} "
               "replacing_existing={} total_audio_callbacks={}",
               participant_identity, static_cast<int>(source), replacing,
               audio_callbacks_.size());
}

void SubscriptionThreadDispatcher::setOnVideoFrameCallback(
    const std::string &participant_identity, TrackSource source,
    VideoFrameCallback callback, VideoStream::Options opts) {
  CallbackKey key{participant_identity, source};
  std::lock_guard<std::mutex> lock(lock_);
  const bool replacing = video_callbacks_.find(key) != video_callbacks_.end();
  video_callbacks_[key] = RegisteredVideoCallback{
      std::move(callback),
      VideoFrameEventCallback{},
      std::move(opts),
  };
  LK_LOG_DEBUG("Registered legacy video frame callback for participant={} "
               "source={} replacing_existing={} total_video_callbacks={}",
               participant_identity, static_cast<int>(source), replacing,
               video_callbacks_.size());
}

void SubscriptionThreadDispatcher::setOnVideoFrameEventCallback(
    const std::string &participant_identity, TrackSource source,
    VideoFrameEventCallback callback, VideoStream::Options opts) {
  CallbackKey key{participant_identity, source};
  std::lock_guard<std::mutex> lock(lock_);
  const bool replacing = video_callbacks_.find(key) != video_callbacks_.end();
  video_callbacks_[key] = RegisteredVideoCallback{
      VideoFrameCallback{},
      std::move(callback),
      std::move(opts),
  };
  LK_LOG_DEBUG("Registered video frame callback for participant={} source={} "
               "replacing_existing={} total_video_callbacks={}",
               participant_identity, static_cast<int>(source), replacing,
               video_callbacks_.size());
}

void SubscriptionThreadDispatcher::clearOnAudioFrameCallback(
    const std::string &participant_identity, TrackSource source) {
  CallbackKey key{participant_identity, source};
  std::thread old_thread;
  bool removed_callback = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    removed_callback = audio_callbacks_.erase(key) > 0;
    old_thread = extractReaderThreadLocked(key);
    LK_LOG_DEBUG(
        "Clearing audio frame callback for participant={} source={} "
        "removed_callback={} stopped_reader={} remaining_audio_callbacks={}",
        participant_identity, static_cast<int>(source), removed_callback,
        old_thread.joinable(), audio_callbacks_.size());
  }
  if (old_thread.joinable()) {
    old_thread.join();
  }
}

void SubscriptionThreadDispatcher::clearOnVideoFrameCallback(
    const std::string &participant_identity, TrackSource source) {
  CallbackKey key{participant_identity, source};
  std::thread old_thread;
  bool removed_callback = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    removed_callback = video_callbacks_.erase(key) > 0;
    old_thread = extractReaderThreadLocked(key);
    LK_LOG_DEBUG(
        "Clearing video frame callback for participant={} source={} "
        "removed_callback={} stopped_reader={} remaining_video_callbacks={}",
        participant_identity, static_cast<int>(source), removed_callback,
        old_thread.joinable(), video_callbacks_.size());
  }
  if (old_thread.joinable()) {
    old_thread.join();
  }
}

void SubscriptionThreadDispatcher::handleTrackSubscribed(
    const std::string &participant_identity, TrackSource source,
    const std::shared_ptr<Track> &track) {
  if (!track) {
    LK_LOG_WARN(
        "Ignoring subscribed track dispatch for participant={} source={} "
        "because track is null",
        participant_identity, static_cast<int>(source));
    return;
  }

  LK_LOG_DEBUG("Handling subscribed track for participant={} source={} kind={}",
               participant_identity, static_cast<int>(source),
               trackKindName(track->kind()));

  CallbackKey key{participant_identity, source};
  std::thread old_thread;
  {
    std::lock_guard<std::mutex> lock(lock_);
    old_thread = startReaderLocked(key, track);
  }
  if (old_thread.joinable()) {
    old_thread.join();
  }
}

void SubscriptionThreadDispatcher::handleTrackUnsubscribed(
    const std::string &participant_identity, TrackSource source) {
  CallbackKey key{participant_identity, source};
  std::thread old_thread;
  {
    std::lock_guard<std::mutex> lock(lock_);
    old_thread = extractReaderThreadLocked(key);
    LK_LOG_DEBUG("Handling unsubscribed track for participant={} source={} "
                 "stopped_reader={}",
                 participant_identity, static_cast<int>(source),
                 old_thread.joinable());
  }
  if (old_thread.joinable()) {
    old_thread.join();
  }
}

void SubscriptionThreadDispatcher::stopAll() {
  std::vector<std::thread> threads;
  std::size_t active_reader_count = 0;
  std::size_t audio_callback_count = 0;
  std::size_t video_callback_count = 0;
  {
    std::lock_guard<std::mutex> lock(lock_);
    active_reader_count = active_readers_.size();
    audio_callback_count = audio_callbacks_.size();
    video_callback_count = video_callbacks_.size();
    LK_LOG_DEBUG("Stopping all subscription readers active_readers={} "
                 "audio_callbacks={} video_callbacks={}",
                 active_reader_count, audio_callback_count,
                 video_callback_count);
    for (auto &[key, reader] : active_readers_) {
      LK_LOG_TRACE("Closing active reader for participant={} source={}",
                   key.participant_identity, static_cast<int>(key.source));
      if (reader.audio_stream) {
        reader.audio_stream->close();
      }
      if (reader.video_stream) {
        reader.video_stream->close();
      }
      if (reader.thread.joinable()) {
        threads.push_back(std::move(reader.thread));
      }
    }
    active_readers_.clear();
    audio_callbacks_.clear();
    video_callbacks_.clear();
  }
  for (auto &thread : threads) {
    thread.join();
  }
  LK_LOG_DEBUG("Stopped {} subscription reader threads", threads.size());
}

std::thread SubscriptionThreadDispatcher::extractReaderThreadLocked(
    const CallbackKey &key) {
  auto it = active_readers_.find(key);
  if (it == active_readers_.end()) {
    LK_LOG_TRACE("No active reader to extract for participant={} source={}",
                 key.participant_identity, static_cast<int>(key.source));
    return {};
  }

  LK_LOG_DEBUG("Extracting active reader for participant={} source={}",
               key.participant_identity, static_cast<int>(key.source));
  ActiveReader reader = std::move(it->second);
  active_readers_.erase(it);

  if (reader.audio_stream) {
    reader.audio_stream->close();
  }
  if (reader.video_stream) {
    reader.video_stream->close();
  }
  return std::move(reader.thread);
}

std::thread SubscriptionThreadDispatcher::startReaderLocked(
    const CallbackKey &key, const std::shared_ptr<Track> &track) {
  if (track->kind() == TrackKind::KIND_AUDIO) {
    auto it = audio_callbacks_.find(key);
    if (it == audio_callbacks_.end()) {
      LK_LOG_TRACE("Skipping audio reader start for participant={} source={} "
                   "because no audio callback is registered",
                   key.participant_identity, static_cast<int>(key.source));
      return {};
    }
    return startAudioReaderLocked(key, track, it->second.callback,
                                  it->second.options);
  }
  if (track->kind() == TrackKind::KIND_VIDEO) {
    auto it = video_callbacks_.find(key);
    if (it == video_callbacks_.end()) {
      LK_LOG_TRACE("Skipping video reader start for participant={} source={} "
                   "because no video callback is registered",
                   key.participant_identity, static_cast<int>(key.source));
      return {};
    }
    return startVideoReaderLocked(key, track, it->second);
  }
  if (track->kind() == TrackKind::KIND_UNKNOWN) {
    LK_LOG_WARN(
        "Skipping reader start for participant={} source={} because track "
        "kind is unknown",
        key.participant_identity, static_cast<int>(key.source));
    return {};
  }

  LK_LOG_WARN(
      "Skipping reader start for participant={} source={} because track kind "
      "is unsupported",
      key.participant_identity, static_cast<int>(key.source));
  return {};
}

std::thread SubscriptionThreadDispatcher::startAudioReaderLocked(
    const CallbackKey &key, const std::shared_ptr<Track> &track,
    AudioFrameCallback cb, const AudioStream::Options &opts) {
  LK_LOG_DEBUG("Starting audio reader for participant={} source={}",
               key.participant_identity, static_cast<int>(key.source));
  auto old_thread = extractReaderThreadLocked(key);

  if (static_cast<int>(active_readers_.size()) >= kMaxActiveReaders) {
    LK_LOG_ERROR(
        "Cannot start audio reader for {} source={}: active reader limit ({}) "
        "reached",
        key.participant_identity, static_cast<int>(key.source),
        kMaxActiveReaders);
    return old_thread;
  }

  auto stream = AudioStream::fromTrack(track, opts);
  if (!stream) {
    LK_LOG_ERROR("Failed to create AudioStream for {} source={}",
                 key.participant_identity, static_cast<int>(key.source));
    return old_thread;
  }

  ActiveReader reader;
  reader.audio_stream = stream;
  auto stream_copy = stream;
  const std::string participant_identity = key.participant_identity;
  const TrackSource source = key.source;
  reader.thread =
      std::thread([stream_copy, cb, participant_identity, source]() {
        LK_LOG_DEBUG("Audio reader thread started for participant={} source={}",
                     participant_identity, static_cast<int>(source));
        AudioFrameEvent ev;
        while (stream_copy->read(ev)) {
          try {
            cb(ev.frame);
          } catch (const std::exception &e) {
            LK_LOG_ERROR("Audio frame callback exception: {}", e.what());
          }
        }
        LK_LOG_DEBUG("Audio reader thread exiting for participant={} source={}",
                     participant_identity, static_cast<int>(source));
      });
  active_readers_[key] = std::move(reader);
  LK_LOG_DEBUG("Started audio reader for participant={} source={} "
               "active_readers={}",
               key.participant_identity, static_cast<int>(key.source),
               active_readers_.size());
  return old_thread;
}

std::thread SubscriptionThreadDispatcher::startVideoReaderLocked(
    const CallbackKey &key, const std::shared_ptr<Track> &track,
    const RegisteredVideoCallback &callback) {
  LK_LOG_DEBUG("Starting video reader for participant={} source={}",
               key.participant_identity, static_cast<int>(key.source));
  auto old_thread = extractReaderThreadLocked(key);

  if (static_cast<int>(active_readers_.size()) >= kMaxActiveReaders) {
    LK_LOG_ERROR(
        "Cannot start video reader for {} source={}: active reader limit ({}) "
        "reached",
        key.participant_identity, static_cast<int>(key.source),
        kMaxActiveReaders);
    return old_thread;
  }

  auto stream = VideoStream::fromTrack(track, callback.options);
  if (!stream) {
    LK_LOG_ERROR("Failed to create VideoStream for {} source={}",
                 key.participant_identity, static_cast<int>(key.source));
    return old_thread;
  }

  ActiveReader reader;
  reader.video_stream = stream;
  auto stream_copy = stream;
  auto legacy_cb = callback.legacy_callback;
  auto event_cb = callback.event_callback;
  const std::string participant_identity = key.participant_identity;
  const TrackSource source = key.source;
  reader.thread =
      std::thread(
          [stream_copy, legacy_cb, event_cb, participant_identity, source]() {
        LK_LOG_DEBUG("Video reader thread started for participant={} source={}",
                     participant_identity, static_cast<int>(source));
        VideoFrameEvent ev;
        while (stream_copy->read(ev)) {
          try {
            if (event_cb) {
              event_cb(ev);
            } else if (legacy_cb) {
              legacy_cb(ev.frame, ev.timestamp_us);
            }
          } catch (const std::exception &e) {
            LK_LOG_ERROR("Video frame callback exception: {}", e.what());
          }
        }
        LK_LOG_DEBUG("Video reader thread exiting for participant={} source={}",
                     participant_identity, static_cast<int>(source));
      });
  active_readers_[key] = std::move(reader);
  LK_LOG_DEBUG("Started video reader for participant={} source={} "
               "active_readers={}",
               key.participant_identity, static_cast<int>(key.source),
               active_readers_.size());
  return old_thread;
}

} // namespace livekit
