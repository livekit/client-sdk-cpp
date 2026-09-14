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

#include <exception>
#include <utility>
#include <vector>

#include "livekit/data_track_frame.h"
#include "livekit/data_track_stream.h"
#include "livekit/remote_data_track.h"
#include "livekit/track.h"
#include "lk_log.h"

namespace livekit {

namespace {

const char* trackKindName(TrackKind kind) {
  if (kind == TrackKind::KIND_AUDIO) {
    return "audio";
  }
  if (kind == TrackKind::KIND_VIDEO) {
    return "video";
  }
  if (kind == TrackKind::KIND_UNKNOWN) {
    return "unknown";
  }

  return "unsupported";
}

} // namespace

SubscriptionThreadDispatcher::SubscriptionThreadDispatcher() = default;

// NOLINTBEGIN(bugprone-exception-escape)
// Exceptions can be thrown by stopAll() in this desctuctor, and clang flags as
// an exception escape suppressing for now
SubscriptionThreadDispatcher::~SubscriptionThreadDispatcher() {
  LK_LOG_DEBUG("Destroying SubscriptionThreadDispatcher");
  stopAll();
}
// NOLINTEND(bugprone-exception-escape)

// -------------------------------------------------------------------
// Reader thread lifecycle helpers shared by every public entry point
// -------------------------------------------------------------------

void SubscriptionThreadDispatcher::disposeReaderThread(std::thread&& thread, const char* operation) {
  if (!thread.joinable()) {
    return;
  }
  if (isSelfThread(thread.get_id())) {
    // The caller IS this reader, so it reached us from inside its own frame
    // callback. Joining here would be a self-join (std::system_error, and a
    // still-joinable std::thread destroyed during unwinding would terminate
    // the process). Detaching is safe: no reader lambda captures `this`; each
    // owns its stream, callback, and per-reader state by value, so once
    // extracted the thread touches nothing owned by the dispatcher.
    LK_LOG_WARN(
        "{} was called from inside the frame callback of the reader it stops; "
        "detaching that reader instead of self-joining. It exits once the "
        "callback returns. Registering, clearing, or tearing down from within a "
        "frame callback is discouraged",
        operation);
    thread.detach();
    return;
  }
  thread.join();
}

std::thread SubscriptionThreadDispatcher::extractReaderForDrainLocked(const CallbackKey& key) {
  std::thread old_thread = extractReaderThreadLocked(key);
  if (old_thread.joinable()) {
    // Block every start for this key until finishReaderDrainAndRestart has
    // joined (or detached) this thread. Without this, a replacement reader
    // could begin invoking the new callback while the old callback is still
    // mid-invocation on the thread we are about to join.
    ++draining_readers_[key];
  }
  return old_thread;
}

void SubscriptionThreadDispatcher::finishReaderDrainAndRestart(const CallbackKey& key, std::thread old_thread,
                                                               const char* operation) {
  const bool drained = old_thread.joinable();
  disposeReaderThread(std::move(old_thread), operation);

  const std::scoped_lock<std::mutex> lock(lock_);
  if (drained) {
    auto it = draining_readers_.find(key);
    if (it != draining_readers_.end() && --it->second <= 0) {
      draining_readers_.erase(it);
    }
  }
  startReaderForSubscribedTrackLocked(key);
  LK_LOG_DEBUG("{}: reader restart for participant={} track_name={} drained_previous={} reader_active={}", operation,
               key.participant_identity, key.track_name, drained, active_readers_.find(key) != active_readers_.end());
}

// -------------------------------------------------------------------
// Audio/video callback registration
// -------------------------------------------------------------------

void SubscriptionThreadDispatcher::setOnAudioFrameCallback(const std::string& participant_identity,
                                                           const std::string& track_name, AudioFrameCallback callback,
                                                           const AudioStream::Options& opts) {
  const CallbackKey key{participant_identity, track_name};
  std::thread old_thread;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    // Stop any reader still dispatching to the previous callback. Reader threads
    // hold their own copy of the callback, so overwriting the registration alone
    // would leave the old callback receiving frames.
    old_thread = extractReaderForDrainLocked(key);
    const bool replacing = audio_callbacks_.find(key) != audio_callbacks_.end();
    audio_callbacks_[key] = RegisteredAudioCallback{std::move(callback), opts};
    LK_LOG_DEBUG(
        "Registered audio frame callback for participant={} track_name={} "
        "replacing_existing={} stopped_reader={} total_audio_callbacks={}",
        participant_identity, track_name, replacing, old_thread.joinable(), audio_callbacks_.size());
  }
  // Joins the previous reader first, then starts a fresh one bound to the new
  // callback if the track is subscribed.
  finishReaderDrainAndRestart(key, std::move(old_thread), "setOnAudioFrameCallback");
}

void SubscriptionThreadDispatcher::setOnVideoFrameEventCallback(const std::string& participant_identity,
                                                                const std::string& track_name,
                                                                VideoFrameEventCallback callback,
                                                                const VideoStream::Options& opts) {
  const CallbackKey key{participant_identity, track_name};
  std::thread old_thread;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    old_thread = extractReaderForDrainLocked(key);
    const bool replacing = video_callbacks_.find(key) != video_callbacks_.end();
    video_callbacks_[key] = RegisteredVideoCallback{
        VideoFrameCallback{},
        std::move(callback),
        opts,
    };
    LK_LOG_DEBUG(
        "Registered video frame event callback for participant={} track_name={} "
        "replacing_existing={} stopped_reader={} total_video_callbacks={}",
        participant_identity, track_name, replacing, old_thread.joinable(), video_callbacks_.size());
  }
  finishReaderDrainAndRestart(key, std::move(old_thread), "setOnVideoFrameEventCallback");
}

void SubscriptionThreadDispatcher::setOnVideoFrameCallback(const std::string& participant_identity,
                                                           const std::string& track_name, VideoFrameCallback callback,
                                                           const VideoStream::Options& opts) {
  const CallbackKey key{participant_identity, track_name};
  std::thread old_thread;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    old_thread = extractReaderForDrainLocked(key);
    const bool replacing = video_callbacks_.find(key) != video_callbacks_.end();
    video_callbacks_[key] = RegisteredVideoCallback{
        std::move(callback),
        VideoFrameEventCallback{},
        opts,
    };
    LK_LOG_DEBUG(
        "Registered video frame callback for participant={} track_name={} "
        "replacing_existing={} stopped_reader={} total_video_callbacks={}",
        participant_identity, track_name, replacing, old_thread.joinable(), video_callbacks_.size());
  }
  finishReaderDrainAndRestart(key, std::move(old_thread), "setOnVideoFrameCallback");
}

void SubscriptionThreadDispatcher::clearOnAudioFrameCallback(const std::string& participant_identity,
                                                             const std::string& track_name) {
  const CallbackKey key{participant_identity, track_name};
  std::thread old_thread;
  bool removed_callback = false;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    removed_callback = audio_callbacks_.erase(key) > 0;
    old_thread = extractReaderForDrainLocked(key);
    LK_LOG_DEBUG(
        "Clearing audio frame callback for participant={} track_name={} "
        "removed_callback={} stopped_reader={} remaining_audio_callbacks={}",
        participant_identity, track_name, removed_callback, old_thread.joinable(), audio_callbacks_.size());
  }
  // With the registration gone nothing restarts here, unless a concurrent
  // caller re-registered while we were joining -- in which case it should.
  finishReaderDrainAndRestart(key, std::move(old_thread), "clearOnAudioFrameCallback");
}

void SubscriptionThreadDispatcher::clearOnVideoFrameCallback(const std::string& participant_identity,
                                                             const std::string& track_name) {
  const CallbackKey key{participant_identity, track_name};
  std::thread old_thread;
  bool removed_callback = false;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    removed_callback = video_callbacks_.erase(key) > 0;
    old_thread = extractReaderForDrainLocked(key);
    LK_LOG_DEBUG(
        "Clearing video frame callback for participant={} track_name={} "
        "removed_callback={} stopped_reader={} remaining_video_callbacks={}",
        participant_identity, track_name, removed_callback, old_thread.joinable(), video_callbacks_.size());
  }
  finishReaderDrainAndRestart(key, std::move(old_thread), "clearOnVideoFrameCallback");
}

void SubscriptionThreadDispatcher::handleTrackSubscribed(const std::string& participant_identity,
                                                         const std::string& track_name,
                                                         const std::shared_ptr<Track>& track) {
  if (!track) {
    LK_LOG_WARN("Ignoring subscribed track dispatch for participant={} track_name={} because track is null",
                participant_identity, track_name);
    return;
  }

  LK_LOG_DEBUG("Handling subscribed track for participant={} track_name={} kind={}", participant_identity, track_name,
               trackKindName(track->kind()));

  const CallbackKey key{participant_identity, track_name};
  std::thread old_thread;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    subscribed_tracks_[key] = track;
    auto existing = active_readers_.find(key);
    if (existing != active_readers_.end() && existing->second.track_sid == track->sid()) {
      // A duplicate track_subscribed for the publication this reader already
      // serves. Rebuilding the reader would only churn the stream.
      LK_LOG_DEBUG(
          "Skipping reader restart for participant={} track_name={} because a "
          "reader for sid={} is already active",
          participant_identity, track_name, track->sid());
      return;
    }
    // Either no reader, or a reader for a previous publication (republish
    // under the same name): stop it and rebuild against the new track.
    old_thread = extractReaderForDrainLocked(key);
  }
  finishReaderDrainAndRestart(key, std::move(old_thread), "handleTrackSubscribed");
}

void SubscriptionThreadDispatcher::handleTrackUnsubscribed(const std::string& participant_identity, TrackSource source,
                                                           const std::string& track_name) {
  const CallbackKey key{participant_identity, track_name};
  std::thread old_thread;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    subscribed_tracks_.erase(key);
    old_thread = extractReaderForDrainLocked(key);
    LK_LOG_DEBUG(
        "Handling unsubscribed track for participant={} source={} "
        "track_name={} stopped_reader={}",
        participant_identity, static_cast<int>(source), track_name, old_thread.joinable());
  }
  // Nothing restarts here unless the track was re-subscribed while we were
  // joining, in which case the retained track is picked up.
  finishReaderDrainAndRestart(key, std::move(old_thread), "handleTrackUnsubscribed");
}

// -------------------------------------------------------------------
// Data track callback registration
// -------------------------------------------------------------------

DataFrameCallbackId SubscriptionThreadDispatcher::addOnDataFrameCallback(const std::string& participant_identity,
                                                                         const std::string& track_name,
                                                                         DataFrameCallback callback) {
  std::thread old_thread;
  DataFrameCallbackId id;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    id = next_data_callback_id_++;
    const DataCallbackKey key{participant_identity, track_name};
    data_callbacks_[id] = RegisteredDataCallback{key, std::move(callback)};

    auto track_it = remote_data_tracks_.find(key);
    if (track_it != remote_data_tracks_.end()) {
      old_thread = startDataReaderLocked(id, key, track_it->second, data_callbacks_[id].callback);
    }
  }
  disposeReaderThread(std::move(old_thread), "addOnDataFrameCallback");
  return id;
}

void SubscriptionThreadDispatcher::removeOnDataFrameCallback(DataFrameCallbackId id) {
  std::thread old_thread;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    data_callbacks_.erase(id);
    old_thread = extractDataReaderThreadLocked(id);
  }
  disposeReaderThread(std::move(old_thread), "removeOnDataFrameCallback");
}

void SubscriptionThreadDispatcher::handleDataTrackPublished(const std::shared_ptr<RemoteDataTrack>& track) {
  if (!track) {
    LK_LOG_WARN("handleDataTrackPublished called with null track");
    return;
  }

  LK_LOG_INFO("Handling data track published: \"{}\" from \"{}\" (sid={})", track->info().name,
              track->publisherIdentity(), track->info().sid);

  std::vector<std::thread> old_threads;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    const DataCallbackKey key{track->publisherIdentity(), track->info().name};
    remote_data_tracks_[key] = track;

    for (auto& [id, reg] : data_callbacks_) {
      if (reg.key == key) {
        auto t = startDataReaderLocked(id, key, track, reg.callback);
        if (t.joinable()) {
          old_threads.push_back(std::move(t));
        }
      }
    }
  }
  for (auto& t : old_threads) {
    disposeReaderThread(std::move(t), "handleDataTrackPublished");
  }
}

void SubscriptionThreadDispatcher::handleDataTrackUnpublished(const std::string& sid) {
  LK_LOG_INFO("Handling data track unpublished: sid={}", sid);

  std::vector<std::thread> old_threads;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    for (auto it = active_data_readers_.begin(); it != active_data_readers_.end();) {
      auto& reader = it->second;
      if (reader->remote_track && reader->remote_track->info().sid == sid) {
        // Mark cancelled before closing to guard in flight subscriptions
        reader->cancelled = true;
        {
          const std::scoped_lock<std::mutex> sub_guard(reader->sub_mutex);
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
    for (auto it = remote_data_tracks_.begin(); it != remote_data_tracks_.end(); ++it) {
      if (it->second && it->second->info().sid == sid) {
        remote_data_tracks_.erase(it);
        break;
      }
    }
  }
  for (auto& t : old_threads) {
    disposeReaderThread(std::move(t), "handleDataTrackUnpublished");
  }
}

void SubscriptionThreadDispatcher::stopAll() {
  std::vector<std::thread> threads;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    LK_LOG_DEBUG(
        "Stopping all subscription readers active_readers={} "
        "active_data_readers={} audio_callbacks={} "
        "video_callbacks={} data_callbacks={}",
        active_readers_.size(), active_data_readers_.size(), audio_callbacks_.size(), video_callbacks_.size(),
        data_callbacks_.size());

    for (auto& [key, reader] : active_readers_) {
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
    subscribed_tracks_.clear();
    audio_callbacks_.clear();
    video_callbacks_.clear();

    for (auto& [id, reader] : active_data_readers_) {
      // Mark cancelled before closing to guard in flight subscriptions
      reader->cancelled = true;
      {
        const std::scoped_lock<std::mutex> sub_guard(reader->sub_mutex);
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
  // A reader that reached stopAll() from inside its own callback (e.g. the
  // application called Room::disconnect() from a frame callback) is detached
  // rather than self-joined; every other reader is joined.
  for (auto& thread : threads) {
    disposeReaderThread(std::move(thread), "stopAll");
  }
  LK_LOG_DEBUG("Stopped {} subscription reader threads", threads.size());
}

std::thread SubscriptionThreadDispatcher::extractReaderThreadLocked(const CallbackKey& key) {
  auto it = active_readers_.find(key);
  if (it == active_readers_.end()) {
    LK_LOG_TRACE("No active reader to extract for participant={} track_name={}", key.participant_identity,
                 key.track_name);
    return {};
  }

  LK_LOG_DEBUG("Extracting active reader for participant={} track_name={}", key.participant_identity, key.track_name);
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

void SubscriptionThreadDispatcher::startReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track) {
  if (active_readers_.find(key) != active_readers_.end()) {
    // Callers stop the previous reader through the drain protocol before
    // getting here, so this indicates a lifecycle bug. Replacing the slot
    // would drop a joinable std::thread, so leave the existing reader alone.
    LK_LOG_ERROR(
        "Refusing to start a reader for participant={} track_name={} because one "
        "is already active; the previous reader must be stopped first",
        key.participant_identity, key.track_name);
    return;
  }

  if (track->kind() == TrackKind::KIND_AUDIO) {
    auto it = audio_callbacks_.find(key);
    if (it == audio_callbacks_.end()) {
      LK_LOG_TRACE(
          "Skipping audio reader start for participant={} track_name={} "
          "because no audio callback is registered",
          key.participant_identity, key.track_name);
      return;
    }
    startAudioReaderLocked(key, track, it->second.callback, it->second.options);
    return;
  }
  if (track->kind() == TrackKind::KIND_VIDEO) {
    auto it = video_callbacks_.find(key);
    if (it == video_callbacks_.end()) {
      LK_LOG_TRACE(
          "Skipping video reader start for participant={} track_name={} "
          "because no video callback is registered",
          key.participant_identity, key.track_name);
      return;
    }
    startVideoReaderLocked(key, track, it->second);
    return;
  }
  if (track->kind() == TrackKind::KIND_UNKNOWN) {
    LK_LOG_WARN(
        "Skipping reader start for participant={} track_name={} because track "
        "kind is unknown",
        key.participant_identity, key.track_name);
    return;
  }

  LK_LOG_WARN(
      "Skipping reader start for participant={} track_name={} because track kind "
      "is unsupported",
      key.participant_identity, key.track_name);
}

void SubscriptionThreadDispatcher::startReaderForSubscribedTrackLocked(const CallbackKey& key) {
  if (active_readers_.find(key) != active_readers_.end()) {
    return;
  }
  if (draining_readers_.find(key) != draining_readers_.end()) {
    // Another caller is still joining the previous reader for this key. It
    // will start the reader once the join completes; starting one here would
    // let the new callback overlap the old one.
    LK_LOG_TRACE("Deferring reader start for participant={} track_name={} until the previous reader is joined",
                 key.participant_identity, key.track_name);
    return;
  }
  const auto track_it = subscribed_tracks_.find(key);
  if (track_it == subscribed_tracks_.end() || !track_it->second) {
    return;
  }
  startReaderLocked(key, track_it->second);
}

void SubscriptionThreadDispatcher::startAudioReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track,
                                                          const AudioFrameCallback& cb,
                                                          const AudioStream::Options& opts) {
  LK_LOG_DEBUG("Starting audio reader for participant={} track_name={}", key.participant_identity, key.track_name);

  if (static_cast<int>(active_readers_.size()) >= kMaxActiveReaders) {
    LK_LOG_ERROR(
        "Cannot start audio reader for {} track_name={}: active reader limit ({}) "
        "reached",
        key.participant_identity, key.track_name, kMaxActiveReaders);
    return;
  }

  const auto stream = AudioStream::fromTrack(track, opts);
  if (!stream) {
    LK_LOG_ERROR("Failed to create AudioStream for {} track_name={}", key.participant_identity, key.track_name);
    return;
  }

  ActiveReader reader;
  reader.audio_stream = stream;
  reader.track_sid = track->sid();
  const std::string participant_identity = key.participant_identity;
  const std::string track_name = key.track_name;
  // NOLINTBEGIN(bugprone-lambda-function-name,bugprone-exception-escape)
  // Outer try/catch contains anything escaping the per-frame try/catch
  // (stream->read, LK_LOG formatting, etc.) so an exception in this reader
  // thread cannot std::terminate the process. clang-tidy still flags a
  // residual escape path through spdlog's own formatter; that's a logger
  // fault, not application logic -- suppressed at the lambda level.
  //
  // Deliberately captures no `this`: see disposeReaderThread.
  reader.thread = std::thread([stream, cb, participant_identity, track_name]() {
    try {
      LK_LOG_DEBUG("Audio reader thread started for participant={} track_name={}", participant_identity, track_name);
      AudioFrameEvent ev;
      while (stream->read(ev)) {
        try {
          cb(ev.frame);
        } catch (const std::exception& e) {
          LK_LOG_ERROR("Audio frame callback exception: {}", e.what());
        }
      }
      LK_LOG_DEBUG("Audio reader thread exiting for participant={} track_name={}", participant_identity, track_name);
    } catch (const std::exception& e) {
      LK_LOG_ERROR("Audio reader thread terminating due to exception: {}", e.what());
    } catch (...) {
      LK_LOG_ERROR("Audio reader thread terminating due to unknown exception");
    }
  });
  // NOLINTEND(bugprone-lambda-function-name,bugprone-exception-escape)
  reader.thread_id = reader.thread.get_id();
  active_readers_[key] = std::move(reader);
  LK_LOG_DEBUG(
      "Started audio reader for participant={} track_name={} "
      "active_readers={}",
      key.participant_identity, key.track_name, active_readers_.size());
}

void SubscriptionThreadDispatcher::startVideoReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track,
                                                          const RegisteredVideoCallback& callback) {
  LK_LOG_DEBUG("Starting video reader for participant={} track_name={}", key.participant_identity, key.track_name);

  if (static_cast<int>(active_readers_.size()) >= kMaxActiveReaders) {
    LK_LOG_ERROR(
        "Cannot start video reader for {} track_name={}: active reader limit ({}) "
        "reached",
        key.participant_identity, key.track_name, kMaxActiveReaders);
    return;
  }

  auto stream = VideoStream::fromTrack(track, callback.options);
  if (!stream) {
    LK_LOG_ERROR("Failed to create VideoStream for {} track_name={}", key.participant_identity, key.track_name);
    return;
  }

  ActiveReader reader;
  reader.video_stream = stream;
  reader.track_sid = track->sid();
  auto legacy_cb = callback.legacy_callback;
  auto event_cb = callback.event_callback;
  const std::string participant_identity = key.participant_identity;
  const std::string track_name = key.track_name;
  // NOLINTBEGIN(bugprone-lambda-function-name,bugprone-exception-escape)
  // Mirrors the audio reader: outer try/catch contains escapes from
  // stream->read, LK_LOG, etc. Residual diagnostic from spdlog's own
  // formatter is an unrelated logger-fault path and is suppressed.
  //
  // Deliberately captures no `this`: see disposeReaderThread.
  reader.thread = std::thread([stream = std::move(stream), legacy_cb, event_cb, participant_identity, track_name]() {
    try {
      LK_LOG_DEBUG("Video reader thread started for participant={} track_name={}", participant_identity, track_name);
      VideoFrameEvent ev;
      while (stream->read(ev)) {
        try {
          if (event_cb) {
            event_cb(ev);
          } else if (legacy_cb) {
            legacy_cb(ev.frame, ev.timestamp_us);
          }
        } catch (const std::exception& e) {
          LK_LOG_ERROR("Video frame callback exception: {}", e.what());
        }
      }
      LK_LOG_DEBUG("Video reader thread exiting for participant={} track_name={}", participant_identity, track_name);
    } catch (const std::exception& e) {
      LK_LOG_ERROR("Video reader thread terminating due to exception: {}", e.what());
    } catch (...) {
      LK_LOG_ERROR("Video reader thread terminating due to unknown exception");
    }
  });
  // NOLINTEND(bugprone-lambda-function-name,bugprone-exception-escape)
  reader.thread_id = reader.thread.get_id();
  active_readers_[key] = std::move(reader);
  LK_LOG_DEBUG(
      "Started video reader for participant={} track_name={} "
      "active_readers={}",
      key.participant_identity, key.track_name, active_readers_.size());
}

// -------------------------------------------------------------------
// Data track reader helpers
// -------------------------------------------------------------------

std::thread SubscriptionThreadDispatcher::extractDataReaderThreadLocked(DataFrameCallbackId id) {
  auto it = active_data_readers_.find(id);
  if (it == active_data_readers_.end()) {
    return {};
  }
  auto reader = std::move(it->second);
  active_data_readers_.erase(it);
  // Mark cancelled before closing to guard in flight subscriptions
  reader->cancelled = true;
  {
    const std::scoped_lock<std::mutex> guard(reader->sub_mutex);
    if (reader->stream) {
      reader->stream->close();
    }
  }
  // If the caller is this very reader (removal from inside its own callback),
  // disposeReaderThread detaches instead of self-joining. The stream is already
  // closed, so the reader exits as soon as the callback returns.
  return std::move(reader->thread);
}

void SubscriptionThreadDispatcher::markDataReaderFinished(const std::shared_ptr<ActiveDataReader>& reader) {
  reader->finished = true;
  const std::scoped_lock<std::mutex> guard(reader->sub_mutex);
  reader->stream.reset();
}

std::thread SubscriptionThreadDispatcher::startDataReaderLocked(DataFrameCallbackId id, const DataCallbackKey& key,
                                                                const std::shared_ptr<RemoteDataTrack>& track,
                                                                const DataFrameCallback& cb) {
  auto existing = active_data_readers_.find(id);
  if (existing != active_data_readers_.end() && !existing->second->finished.load() && existing->second->remote_track &&
      existing->second->remote_track->info().sid == track->info().sid) {
    LK_LOG_DEBUG(
        "Skipping data reader start for \"{}\" track=\"{}\" because a reader for "
        "sid={} is already active",
        key.participant_identity, key.track_name, track->info().sid);
    return {};
  }

  auto old_thread = extractDataReaderThreadLocked(id);

  const int total_active = static_cast<int>(active_readers_.size()) + static_cast<int>(active_data_readers_.size());
  if (total_active >= kMaxActiveReaders) {
    LK_LOG_ERROR(
        "Cannot start data reader for {} track={}: active reader "
        "limit ({}) reached",
        key.participant_identity, key.track_name, kMaxActiveReaders);
    return old_thread;
  }

  LK_LOG_INFO("Starting data reader for \"{}\" track=\"{}\"", key.participant_identity, key.track_name);

  auto reader = std::make_shared<ActiveDataReader>();
  reader->remote_track = track;
  auto identity = key.participant_identity;
  auto track_name = key.track_name;
  // NOLINTBEGIN(bugprone-lambda-function-name)
  // Deliberately captures no `this`: the thread reports its exit through the
  // shared ActiveDataReader only, which is what allows disposeReaderThread to
  // detach it when torn down from inside its own callback.
  reader->thread = std::thread([reader, track, cb, identity, track_name]() {
    LK_LOG_INFO("Data reader thread: subscribing to \"{}\" track=\"{}\"", identity, track_name);
    std::shared_ptr<DataTrackStream> stream;
    auto subscribe_result = track->subscribe();
    if (!subscribe_result) {
      const auto& error = subscribe_result.error();
      LK_LOG_ERROR(
          "Failed to subscribe to data track \"{}\" from \"{}\": code={} "
          "message={}",
          track_name, identity, static_cast<std::uint32_t>(error.code), error.message);
      markDataReaderFinished(reader);
      return;
    }
    stream = subscribe_result.value();
    LK_LOG_INFO("Data reader thread: subscribed to \"{}\" track=\"{}\"", identity, track_name);

    bool cancelled = false;
    {
      const std::scoped_lock<std::mutex> guard(reader->sub_mutex);
      // A replacement or teardown may have cancelled this reader while the
      // subscribe was in flight. Close the fresh stream so we do not leave a
      // second live subscription behind.
      if (reader->cancelled.load()) {
        cancelled = true;
        stream->close();
      } else {
        reader->stream = stream;
      }
    }
    if (cancelled) {
      markDataReaderFinished(reader);
      return;
    }

    DataTrackFrame frame;
    while (stream->read(frame)) {
      try {
        cb(frame.payload, frame.user_timestamp);
      } catch (const std::exception& e) {
        LK_LOG_ERROR("Data frame callback exception: {}", e.what());
      }
    }
    const auto error = stream->terminalError();
    if (error.has_value()) {
      LK_LOG_ERROR(
          "Data reader stream ended with subscription error for \"{}\" from "
          "\"{}\": code={} message={}",
          track_name, identity, static_cast<std::uint32_t>(error->code), error->message);
    }
    // Whether the stream ended on its own (server EOS) or was closed by an
    // extract/teardown, record that this reader is done so a slot still
    // holding it is treated as replaceable.
    markDataReaderFinished(reader);
    LK_LOG_INFO("Data reader thread exiting for \"{}\" track=\"{}\"", identity, track_name);
  });
  // NOLINTEND(bugprone-lambda-function-name)
  reader->thread_id = reader->thread.get_id();
  active_data_readers_[id] = reader;
  return old_thread;
}

} // namespace livekit
