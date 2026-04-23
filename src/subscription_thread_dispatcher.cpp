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

#include "livekit/data_track_frame.h"
#include "livekit/data_track_stream.h"
#include "livekit/remote_data_track.h"
#include "livekit/track.h"
#include "lk_log.h"

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

SubscriptionThreadDispatcher::SubscriptionThreadDispatcher()
    : next_data_callback_id_(1) {}

SubscriptionThreadDispatcher::~SubscriptionThreadDispatcher() {
  LK_LOG_DEBUG("Destroying SubscriptionThreadDispatcher");
  stopAll();
}

void SubscriptionThreadDispatcher::setOnAudioFrameCallback(
    const std::string &participant_identity, TrackSource source,
    AudioFrameCallback callback, const AudioStream::Options &opts) {
  const CallbackKey key{participant_identity, source, ""};
  const std::lock_guard<std::mutex> lock(lock_);
  const bool replacing = audio_callbacks_.find(key) != audio_callbacks_.end();
  audio_callbacks_[key] = RegisteredAudioCallback{std::move(callback), opts};
  LK_LOG_DEBUG("Registered audio frame callback for participant={} source={} "
               "replacing_existing={} total_audio_callbacks={}",
               participant_identity, static_cast<int>(source), replacing,
               audio_callbacks_.size());
}

void SubscriptionThreadDispatcher::setOnAudioFrameCallback(
    const std::string &participant_identity, const std::string &track_name,
    AudioFrameCallback callback, const AudioStream::Options &opts) {
  const CallbackKey key{participant_identity, TrackSource::SOURCE_UNKNOWN,
                        track_name};
  const std::lock_guard<std::mutex> lock(lock_);
  const bool replacing = audio_callbacks_.find(key) != audio_callbacks_.end();
  audio_callbacks_[key] = RegisteredAudioCallback{std::move(callback), opts};
  LK_LOG_DEBUG(
      "Registered audio frame callback for participant={} track_name={} "
      "replacing_existing={} total_audio_callbacks={}",
      participant_identity, track_name, replacing, audio_callbacks_.size());
}

void SubscriptionThreadDispatcher::setOnVideoFrameCallback(
    const std::string &participant_identity, TrackSource source,
    VideoFrameCallback callback, const VideoStream::Options &opts) {
  const CallbackKey key{participant_identity, source, ""};
  const std::lock_guard<std::mutex> lock(lock_);
  const bool replacing = video_callbacks_.find(key) != video_callbacks_.end();
  video_callbacks_[key] = RegisteredVideoCallback{
      std::move(callback),
      VideoFrameEventCallback{},
      opts,
  };
  LK_LOG_DEBUG("Registered legacy video frame callback for participant={} "
               "source={} replacing_existing={} total_video_callbacks={}",
               participant_identity, static_cast<int>(source), replacing,
               video_callbacks_.size());
}

void SubscriptionThreadDispatcher::setOnVideoFrameEventCallback(
    const std::string &participant_identity, const std::string &track_name,
    VideoFrameEventCallback callback, const VideoStream::Options &opts) {
  const CallbackKey key{participant_identity, TrackSource::SOURCE_UNKNOWN,
                        track_name};
  const std::lock_guard<std::mutex> lock(lock_);
  const bool replacing = video_callbacks_.find(key) != video_callbacks_.end();
  video_callbacks_[key] = RegisteredVideoCallback{
      VideoFrameCallback{},
      std::move(callback),
      opts,
  };
  LK_LOG_DEBUG(
      "Registered video frame event callback for participant={} track_name={} "
      "replacing_existing={} total_video_callbacks={}",
      participant_identity, track_name, replacing, video_callbacks_.size());
}

void SubscriptionThreadDispatcher::setOnVideoFrameCallback(
    const std::string &participant_identity, const std::string &track_name,
    VideoFrameCallback callback, const VideoStream::Options &opts) {
  const CallbackKey key{participant_identity, TrackSource::SOURCE_UNKNOWN,
                        track_name};
  const std::lock_guard<std::mutex> lock(lock_);
  const bool replacing = video_callbacks_.find(key) != video_callbacks_.end();
  video_callbacks_[key] = RegisteredVideoCallback{
      std::move(callback),
      VideoFrameEventCallback{},
      opts,
  };
  LK_LOG_DEBUG(
      "Registered video frame callback for participant={} track_name={} "
      "replacing_existing={} total_video_callbacks={}",
      participant_identity, track_name, replacing, video_callbacks_.size());
}

void SubscriptionThreadDispatcher::clearOnAudioFrameCallback(
    const std::string &participant_identity, TrackSource source) {
  const CallbackKey key{participant_identity, source, ""};
  std::thread old_thread;
  bool removed_callback = false;
  {
    const std::lock_guard<std::mutex> lock(lock_);
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

void SubscriptionThreadDispatcher::clearOnAudioFrameCallback(
    const std::string &participant_identity, const std::string &track_name) {
  const CallbackKey key{participant_identity, TrackSource::SOURCE_UNKNOWN,
                        track_name};
  std::thread old_thread;
  bool removed_callback = false;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    removed_callback = audio_callbacks_.erase(key) > 0;
    old_thread = extractReaderThreadLocked(key);
    LK_LOG_DEBUG(
        "Clearing audio frame callback for participant={} track_name={} "
        "removed_callback={} stopped_reader={} remaining_audio_callbacks={}",
        participant_identity, track_name, removed_callback,
        old_thread.joinable(), audio_callbacks_.size());
  }
  if (old_thread.joinable()) {
    old_thread.join();
  }
}

void SubscriptionThreadDispatcher::clearOnVideoFrameCallback(
    const std::string &participant_identity, TrackSource source) {
  const CallbackKey key{participant_identity, source, ""};
  std::thread old_thread;
  bool removed_callback = false;
  {
    const std::lock_guard<std::mutex> lock(lock_);
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

void SubscriptionThreadDispatcher::clearOnVideoFrameCallback(
    const std::string &participant_identity, const std::string &track_name) {
  const CallbackKey key{participant_identity, TrackSource::SOURCE_UNKNOWN,
                        track_name};
  std::thread old_thread;
  bool removed_callback = false;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    removed_callback = video_callbacks_.erase(key) > 0;
    old_thread = extractReaderThreadLocked(key);
    LK_LOG_DEBUG(
        "Clearing video frame callback for participant={} track_name={} "
        "removed_callback={} stopped_reader={} remaining_video_callbacks={}",
        participant_identity, track_name, removed_callback,
        old_thread.joinable(), video_callbacks_.size());
  }
  if (old_thread.joinable()) {
    old_thread.join();
  }
}

void SubscriptionThreadDispatcher::handleTrackSubscribed(
    const std::string &participant_identity, TrackSource source,
    const std::string &track_name, const std::shared_ptr<Track> &track) {
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

  CallbackKey key{participant_identity, TrackSource::SOURCE_UNKNOWN,
                  track_name};
  const CallbackKey fallback_key{participant_identity, source, ""};
  std::thread old_thread;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    if ((track->kind() == TrackKind::KIND_AUDIO &&
         audio_callbacks_.find(key) == audio_callbacks_.end()) ||
        (track->kind() == TrackKind::KIND_VIDEO &&
         video_callbacks_.find(key) == video_callbacks_.end())) {
      key = fallback_key;
    }
    old_thread = startReaderLocked(key, track);
  }
  if (old_thread.joinable()) {
    old_thread.join();
  }
}

void SubscriptionThreadDispatcher::handleTrackUnsubscribed(
    const std::string &participant_identity, TrackSource source,
    const std::string &track_name) {
  const CallbackKey key{participant_identity, TrackSource::SOURCE_UNKNOWN,
                        track_name};
  const CallbackKey fallback_key{participant_identity, source, ""};
  std::thread old_thread;
  std::thread fallback_old_thread;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    old_thread = extractReaderThreadLocked(key);
    fallback_old_thread = extractReaderThreadLocked(fallback_key);
    LK_LOG_DEBUG("Handling unsubscribed track for participant={} source={} "
                 "track_name={} stopped_reader={} fallback_stopped_reader={}",
                 participant_identity, static_cast<int>(source), track_name,
                 old_thread.joinable(), fallback_old_thread.joinable());
  }
  if (old_thread.joinable()) {
    old_thread.join();
  }
  if (fallback_old_thread.joinable()) {
    fallback_old_thread.join();
  }
}

// -------------------------------------------------------------------
// Data track callback registration
// -------------------------------------------------------------------

DataFrameCallbackId SubscriptionThreadDispatcher::addOnDataFrameCallback(
    const std::string &participant_identity, const std::string &track_name,
    DataFrameCallback callback) {
  std::thread old_thread;
  DataFrameCallbackId id;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    id = next_data_callback_id_++;
    const DataCallbackKey key{participant_identity, track_name};
    data_callbacks_[id] = RegisteredDataCallback{key, std::move(callback)};

    auto track_it = remote_data_tracks_.find(key);
    if (track_it != remote_data_tracks_.end()) {
      old_thread = startDataReaderLocked(id, key, track_it->second,
                                         data_callbacks_[id].callback);
    }
  }
  if (old_thread.joinable()) {
    old_thread.join();
  }
  return id;
}

void SubscriptionThreadDispatcher::removeOnDataFrameCallback(
    DataFrameCallbackId id) {
  std::thread old_thread;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    data_callbacks_.erase(id);
    old_thread = extractDataReaderThreadLocked(id);
  }
  if (old_thread.joinable()) {
    old_thread.join();
  }
}

void SubscriptionThreadDispatcher::handleDataTrackPublished(
    const std::shared_ptr<RemoteDataTrack> &track) {
  if (!track) {
    LK_LOG_WARN("handleDataTrackPublished called with null track");
    return;
  }

  LK_LOG_INFO("Handling data track published: \"{}\" from \"{}\" (sid={})",
              track->info().name, track->publisherIdentity(),
              track->info().sid);

  std::vector<std::thread> old_threads;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    const DataCallbackKey key{track->publisherIdentity(), track->info().name};
    remote_data_tracks_[key] = track;

    for (auto &[id, reg] : data_callbacks_) {
      if (reg.key == key) {
        auto t = startDataReaderLocked(id, key, track, reg.callback);
        if (t.joinable()) {
          old_threads.push_back(std::move(t));
        }
      }
    }
  }
  for (auto &t : old_threads) {
    t.join();
  }
}

void SubscriptionThreadDispatcher::handleDataTrackUnpublished(
    const std::string &sid) {
  LK_LOG_INFO("Handling data track unpublished: sid={}", sid);

  std::vector<std::thread> old_threads;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    for (auto it = active_data_readers_.begin();
         it != active_data_readers_.end();) {
      auto &reader = it->second;
      if (reader->remote_track && reader->remote_track->info().sid == sid) {
        {
          const std::lock_guard<std::mutex> sub_guard(reader->sub_mutex);
          if (reader->stream) {
            reader->stream->close();
          }
        }
        if (reader->thread.joinable()) {
          old_threads.push_back(std::move(reader->thread));
        }
        it = active_data_readers_.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = remote_data_tracks_.begin(); it != remote_data_tracks_.end();
         ++it) {
      if (it->second && it->second->info().sid == sid) {
        remote_data_tracks_.erase(it);
        break;
      }
    }
  }
  for (auto &t : old_threads) {
    t.join();
  }
}

void SubscriptionThreadDispatcher::stopAll() {
  std::vector<std::thread> threads;
  {
    const std::lock_guard<std::mutex> lock(lock_);
    LK_LOG_DEBUG("Stopping all subscription readers active_readers={} "
                 "active_data_readers={} audio_callbacks={} "
                 "video_callbacks={} data_callbacks={}",
                 active_readers_.size(), active_data_readers_.size(),
                 audio_callbacks_.size(), video_callbacks_.size(),
                 data_callbacks_.size());

    for (auto &[key, reader] : active_readers_) {
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

    for (auto &[id, reader] : active_data_readers_) {
      {
        const std::lock_guard<std::mutex> sub_guard(reader->sub_mutex);
        if (reader->stream) {
          reader->stream->close();
        }
      }
      if (reader->thread.joinable()) {
        threads.push_back(std::move(reader->thread));
      }
    }
    active_data_readers_.clear();
    data_callbacks_.clear();
    remote_data_tracks_.clear();
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
    LK_LOG_TRACE("No active reader to extract for participant={} source={} "
                 "track_name={}",
                 key.participant_identity, static_cast<int>(key.source),
                 key.track_name);
    return {};
  }

  LK_LOG_DEBUG("Extracting active reader for participant={} source={} "
               "track_name={}",
               key.participant_identity, static_cast<int>(key.source),
               key.track_name);
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
    const AudioFrameCallback &cb, const AudioStream::Options &opts) {
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

  const auto stream = AudioStream::fromTrack(track, opts);
  if (!stream) {
    LK_LOG_ERROR("Failed to create AudioStream for {} source={}",
                 key.participant_identity, static_cast<int>(key.source));
    return old_thread;
  }

  ActiveReader reader;
  reader.audio_stream = stream;
  const std::string participant_identity = key.participant_identity;
  const TrackSource source = key.source;
  // NOLINTBEGIN(bugprone-lambda-function-name)
  reader.thread = std::thread([stream, cb, participant_identity, source]() {
    LK_LOG_DEBUG("Audio reader thread started for participant={} source={}",
                 participant_identity, static_cast<int>(source));
    AudioFrameEvent ev;
    while (stream->read(ev)) {
      try {
        cb(ev.frame);
      } catch (const std::exception &e) {
        LK_LOG_ERROR("Audio frame callback exception: {}", e.what());
      }
    }
    LK_LOG_DEBUG("Audio reader thread exiting for participant={} source={}",
                 participant_identity, static_cast<int>(source));
  });
  // NOLINTEND(bugprone-lambda-function-name)
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
  auto legacy_cb = callback.legacy_callback;
  auto event_cb = callback.event_callback;
  const std::string participant_identity = key.participant_identity;
  const TrackSource source = key.source;
  // NOLINTBEGIN(bugprone-lambda-function-name)
  reader.thread = std::thread([stream = std::move(stream), legacy_cb, event_cb,
                               participant_identity, source]() {
    LK_LOG_DEBUG("Video reader thread started for participant={} source={}",
                 participant_identity, static_cast<int>(source));
    VideoFrameEvent ev;
    while (stream->read(ev)) {
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
  // NOLINTEND(bugprone-lambda-function-name)
  active_readers_[key] = std::move(reader);
  LK_LOG_DEBUG("Started video reader for participant={} source={} "
               "active_readers={}",
               key.participant_identity, static_cast<int>(key.source),
               active_readers_.size());
  return old_thread;
}

// -------------------------------------------------------------------
// Data track reader helpers
// -------------------------------------------------------------------

std::thread SubscriptionThreadDispatcher::extractDataReaderThreadLocked(
    DataFrameCallbackId id) {
  auto it = active_data_readers_.find(id);
  if (it == active_data_readers_.end()) {
    return {};
  }
  auto reader = std::move(it->second);
  active_data_readers_.erase(it);
  {
    const std::lock_guard<std::mutex> guard(reader->sub_mutex);
    if (reader->stream) {
      reader->stream->close();
    }
  }
  return std::move(reader->thread);
}

std::thread SubscriptionThreadDispatcher::extractDataReaderThreadLocked(
    const DataCallbackKey &key) {
  for (auto it = active_data_readers_.begin(); it != active_data_readers_.end();
       ++it) {
    if (it->second && it->second->remote_track &&
        it->second->remote_track->publisherIdentity() ==
            key.participant_identity &&
        it->second->remote_track->info().name == key.track_name) {
      auto reader = std::move(it->second);
      active_data_readers_.erase(it);
      {
        const std::lock_guard<std::mutex> guard(reader->sub_mutex);
        if (reader->stream) {
          reader->stream->close();
        }
      }
      return std::move(reader->thread);
    }
  }
  return {};
}

std::thread SubscriptionThreadDispatcher::startDataReaderLocked(
    DataFrameCallbackId id, const DataCallbackKey &key,
    const std::shared_ptr<RemoteDataTrack> &track,
    const DataFrameCallback &cb) {
  auto old_thread = extractDataReaderThreadLocked(id);

  int total_active = static_cast<int>(active_readers_.size()) +
                     static_cast<int>(active_data_readers_.size());
  if (total_active >= kMaxActiveReaders) {
    LK_LOG_ERROR("Cannot start data reader for {} track={}: active reader "
                 "limit ({}) reached",
                 key.participant_identity, key.track_name, kMaxActiveReaders);
    return old_thread;
  }

  LK_LOG_INFO("Starting data reader for \"{}\" track=\"{}\"",
              key.participant_identity, key.track_name);

  auto reader = std::make_shared<ActiveDataReader>();
  reader->remote_track = track;
  auto identity = key.participant_identity;
  auto track_name = key.track_name;
  // NOLINTBEGIN(bugprone-lambda-function-name)
  reader->thread = std::thread([reader, track, cb, identity, track_name]() {
    LK_LOG_INFO("Data reader thread: subscribing to \"{}\" track=\"{}\"",
                identity, track_name);
    std::shared_ptr<DataTrackStream> stream;
    auto subscribe_result = track->subscribe();
    if (!subscribe_result) {
      const auto &error = subscribe_result.error();
      LK_LOG_ERROR(
          "Failed to subscribe to data track \"{}\" from \"{}\": code={} "
          "message={}",
          track_name, identity, static_cast<std::uint32_t>(error.code),
          error.message);
      return;
    }
    stream = subscribe_result.value();
    LK_LOG_INFO("Data reader thread: subscribed to \"{}\" track=\"{}\"",
                identity, track_name);

    {
      const std::lock_guard<std::mutex> guard(reader->sub_mutex);
      reader->stream = stream;
    }

    DataTrackFrame frame;
    while (stream->read(frame)) {
      try {
        cb(frame.payload, frame.user_timestamp);
      } catch (const std::exception &e) {
        LK_LOG_ERROR("Data frame callback exception: {}", e.what());
      }
    }
    LK_LOG_INFO("Data reader thread exiting for \"{}\" track=\"{}\"", identity,
                track_name);
  });
  // NOLINTEND(bugprone-lambda-function-name)
  active_data_readers_[id] = reader;
  return old_thread;
}

} // namespace livekit
