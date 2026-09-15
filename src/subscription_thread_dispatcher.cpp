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

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <iterator>
#include <limits>
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

struct SubscriptionThreadDispatcher::ReaderCompletion {
  std::mutex lock;
  std::condition_variable cv;
  bool finished{false};
};

struct SubscriptionThreadDispatcher::ExtraState {
  struct TrackedReader {
    std::uint64_t drain_id{0};
    std::thread::id thread_id;
    std::shared_ptr<ReaderCompletion> completion;
  };

  /// Currently subscribed remote audio/video tracks keyed by CallbackKey.
  std::unordered_map<CallbackKey, std::shared_ptr<Track>, CallbackKeyHash> subscribed_tracks;

  /// Readers extracted by another lifecycle call but not yet disposed.
  std::unordered_map<CallbackKey, std::vector<TrackedReader>, CallbackKeyHash> draining_readers;
  std::unordered_map<DataFrameCallbackId, std::vector<TrackedReader>> draining_data_readers;

  /// Self-disposed readers remain observable until their callbacks return.
  std::vector<TrackedReader> detached_readers;

  /// Coordinates stopAll() with lifecycle operations that own extracted
  /// threads outside the dispatcher lock.
  std::condition_variable drain_cv;
  std::uint64_t next_drain_id{1};
  bool stopping{false};
  std::vector<std::thread::id> stopping_reader_ids;
};

struct SubscriptionThreadDispatcher::ExtraStateRegistry {
  std::mutex lock;
  std::unordered_map<SubscriptionThreadDispatcher*, std::unique_ptr<ExtraState>> states;
};

SubscriptionThreadDispatcher::ExtraStateRegistry& SubscriptionThreadDispatcher::extraStateRegistry() {
  static ExtraStateRegistry registry;
  return registry;
}

SubscriptionThreadDispatcher::ExtraState& SubscriptionThreadDispatcher::extraState() {
  auto& registry = extraStateRegistry();
  const std::scoped_lock<std::mutex> lock(registry.lock);
  return *registry.states.at(this);
}

void SubscriptionThreadDispatcher::removeExtraState() {
  auto& registry = extraStateRegistry();
  const std::scoped_lock<std::mutex> lock(registry.lock);
  registry.states.erase(this);
}

SubscriptionThreadDispatcher::SubscriptionThreadDispatcher() {
  auto& registry = extraStateRegistry();
  const std::scoped_lock<std::mutex> lock(registry.lock);
  registry.states.emplace(this, std::make_unique<ExtraState>());
}

// NOLINTBEGIN(bugprone-exception-escape)
// Exceptions can be thrown by stopAll() in this desctuctor, and clang flags as
// an exception escape suppressing for now
SubscriptionThreadDispatcher::~SubscriptionThreadDispatcher() {
  LK_LOG_DEBUG("Destroying SubscriptionThreadDispatcher");
  stopAll();
  removeExtraState();
}
// NOLINTEND(bugprone-exception-escape)

// -------------------------------------------------------------------
// Reader thread lifecycle helpers shared by every public entry point
// -------------------------------------------------------------------

bool SubscriptionThreadDispatcher::disposeReaderThread(std::thread&& thread, const char* operation) {
  if (!thread.joinable()) {
    return false;
  }
  if (isSelfThread(thread.get_id())) {
    // The caller IS this reader so this function was called from the set callback.
    // Joining here would be a self-join. Detaching is safe: no reader lambda captures `this`; each
    // owns its stream, callback, and per-reader state by value, so once
    // extracted the thread touches nothing owned by the dispatcher.
    LK_LOG_WARN(
        "{} was called from inside the frame callback of the reader it stops; "
        "detaching that reader instead of self-joining. It exits once the "
        "callback returns. Registering, clearing, or tearing down from within a "
        "frame callback is discouraged",
        operation);
    thread.detach();
    return true;
  }
  thread.join();
  return false;
}

void SubscriptionThreadDispatcher::markReaderFinished(const std::shared_ptr<ReaderCompletion>& completion) {
  if (!completion) {
    return;
  }
  {
    const std::scoped_lock<std::mutex> lock(completion->lock);
    completion->finished = true;
  }
  completion->cv.notify_all();
}

bool SubscriptionThreadDispatcher::readerFinished(const std::shared_ptr<ReaderCompletion>& completion) {
  if (!completion) {
    return false;
  }
  const std::scoped_lock<std::mutex> lock(completion->lock);
  return completion->finished;
}

void SubscriptionThreadDispatcher::waitForReader(const std::shared_ptr<ReaderCompletion>& completion) {
  if (!completion) {
    return;
  }
  std::unique_lock<std::mutex> lock(completion->lock);
  completion->cv.wait(lock, [&completion] { return completion->finished; });
}

int SubscriptionThreadDispatcher::liveReaderCountLocked() {
  int count = 0;
  for (const auto& [key, reader] : active_readers_) {
    (void)key;
    if (!readerFinished(reader.completion)) {
      ++count;
    }
  }
  for (const auto& [id, reader] : active_data_readers_) {
    (void)id;
    if (reader && (reader->completion ? !readerFinished(reader->completion) : !reader->finished.load())) {
      ++count;
    }
  }

  auto& state = extraState();
  const auto count_tracked = [&count](const auto& drains) {
    for (const auto& [key, readers] : drains) {
      (void)key;
      for (const auto& reader : readers) {
        if (!readerFinished(reader.completion)) {
          ++count;
        }
      }
    }
  };
  count_tracked(state.draining_readers);
  count_tracked(state.draining_data_readers);
  for (const auto& reader : state.detached_readers) {
    if (!readerFinished(reader.completion)) {
      ++count;
    }
  }
  return count;
}

bool SubscriptionThreadDispatcher::rejectWhileStoppingLocked(const char* operation) {
  if (!extraState().stopping) {
    return false;
  }
  LK_LOG_WARN("{} ignored because subscription reader shutdown is in progress", operation);
  return true;
}

SubscriptionThreadDispatcher::ReaderDrain SubscriptionThreadDispatcher::extractReaderForDrainLocked(
    const CallbackKey& key) {
  ReaderDrain drain = extractReaderThreadLocked(key);
  if (drain.thread.joinable()) {
    auto& state = extraState();
    drain.drain_id = state.next_drain_id++;
    state.draining_readers[key].push_back({drain.drain_id, drain.thread_id, drain.completion});
  }
  return drain;
}

void SubscriptionThreadDispatcher::finishReaderDrainAndRestart(const CallbackKey& key, ReaderDrain drain,
                                                               const char* operation) {
  const bool drained = drain.thread.joinable();
  const bool detached = disposeReaderThread(std::move(drain.thread), operation);

  const std::scoped_lock<std::mutex> lock(lock_);
  auto& state = extraState();
  if (drained) {
    auto it = state.draining_readers.find(key);
    if (it != state.draining_readers.end()) {
      auto& readers = it->second;
      readers.erase(std::remove_if(readers.begin(), readers.end(),
                                   [&drain](const ExtraState::TrackedReader& reader) {
                                     return reader.drain_id == drain.drain_id;
                                   }),
                    readers.end());
      if (readers.empty()) {
        state.draining_readers.erase(it);
      }
    }
    if (detached && drain.completion && !readerFinished(drain.completion)) {
      state.detached_readers.push_back({drain.drain_id, drain.thread_id, drain.completion});
    }
  }
  state.detached_readers.erase(std::remove_if(state.detached_readers.begin(), state.detached_readers.end(),
                                              [](const auto& reader) { return readerFinished(reader.completion); }),
                               state.detached_readers.end());
  state.drain_cv.notify_all();
  if (!state.stopping) {
    startReaderForSubscribedTrackLocked(key);
  }
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
  ReaderDrain drain;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    if (rejectWhileStoppingLocked("setOnAudioFrameCallback")) {
      return;
    }
    drain = extractReaderForDrainLocked(key);
    const bool replacing = audio_callbacks_.find(key) != audio_callbacks_.end();
    audio_callbacks_[key] = RegisteredAudioCallback{std::move(callback), opts};
    LK_LOG_DEBUG(
        "Registered audio frame callback for participant={} track_name={} "
        "replacing_existing={} stopped_reader={} total_audio_callbacks={}",
        participant_identity, track_name, replacing, drain.thread.joinable(), audio_callbacks_.size());
  }
  // Joins the previous reader first, then starts a fresh one bound to the new
  // callback if the track is subscribed.
  finishReaderDrainAndRestart(key, std::move(drain), "setOnAudioFrameCallback");
}

void SubscriptionThreadDispatcher::setOnVideoFrameEventCallback(const std::string& participant_identity,
                                                                const std::string& track_name,
                                                                VideoFrameEventCallback callback,
                                                                const VideoStream::Options& opts) {
  const CallbackKey key{participant_identity, track_name};
  ReaderDrain drain;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    if (rejectWhileStoppingLocked("setOnVideoFrameEventCallback")) {
      return;
    }
    drain = extractReaderForDrainLocked(key);
    const bool replacing = video_callbacks_.find(key) != video_callbacks_.end();
    video_callbacks_[key] = RegisteredVideoCallback{
        VideoFrameCallback{},
        std::move(callback),
        opts,
    };
    LK_LOG_DEBUG(
        "Registered video frame event callback for participant={} track_name={} "
        "replacing_existing={} stopped_reader={} total_video_callbacks={}",
        participant_identity, track_name, replacing, drain.thread.joinable(), video_callbacks_.size());
  }
  finishReaderDrainAndRestart(key, std::move(drain), "setOnVideoFrameEventCallback");
}

void SubscriptionThreadDispatcher::setOnVideoFrameCallback(const std::string& participant_identity,
                                                           const std::string& track_name, VideoFrameCallback callback,
                                                           const VideoStream::Options& opts) {
  const CallbackKey key{participant_identity, track_name};
  ReaderDrain drain;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    if (rejectWhileStoppingLocked("setOnVideoFrameCallback")) {
      return;
    }
    drain = extractReaderForDrainLocked(key);
    const bool replacing = video_callbacks_.find(key) != video_callbacks_.end();
    video_callbacks_[key] = RegisteredVideoCallback{
        std::move(callback),
        VideoFrameEventCallback{},
        opts,
    };
    LK_LOG_DEBUG(
        "Registered video frame callback for participant={} track_name={} "
        "replacing_existing={} stopped_reader={} total_video_callbacks={}",
        participant_identity, track_name, replacing, drain.thread.joinable(), video_callbacks_.size());
  }
  finishReaderDrainAndRestart(key, std::move(drain), "setOnVideoFrameCallback");
}

void SubscriptionThreadDispatcher::clearOnAudioFrameCallback(const std::string& participant_identity,
                                                             const std::string& track_name) {
  const CallbackKey key{participant_identity, track_name};
  ReaderDrain drain;
  bool removed_callback = false;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    if (rejectWhileStoppingLocked("clearOnAudioFrameCallback")) {
      return;
    }
    removed_callback = audio_callbacks_.erase(key) > 0;
    drain = extractReaderForDrainLocked(key);
    LK_LOG_DEBUG(
        "Clearing audio frame callback for participant={} track_name={} "
        "removed_callback={} stopped_reader={} remaining_audio_callbacks={}",
        participant_identity, track_name, removed_callback, drain.thread.joinable(), audio_callbacks_.size());
  }
  // With the registration gone nothing restarts here, unless a concurrent
  // caller re-registered while we were joining -- in which case it should.
  finishReaderDrainAndRestart(key, std::move(drain), "clearOnAudioFrameCallback");
}

void SubscriptionThreadDispatcher::clearOnVideoFrameCallback(const std::string& participant_identity,
                                                             const std::string& track_name) {
  const CallbackKey key{participant_identity, track_name};
  ReaderDrain drain;
  bool removed_callback = false;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    if (rejectWhileStoppingLocked("clearOnVideoFrameCallback")) {
      return;
    }
    removed_callback = video_callbacks_.erase(key) > 0;
    drain = extractReaderForDrainLocked(key);
    LK_LOG_DEBUG(
        "Clearing video frame callback for participant={} track_name={} "
        "removed_callback={} stopped_reader={} remaining_video_callbacks={}",
        participant_identity, track_name, removed_callback, drain.thread.joinable(), video_callbacks_.size());
  }
  finishReaderDrainAndRestart(key, std::move(drain), "clearOnVideoFrameCallback");
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
  ReaderDrain drain;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    if (rejectWhileStoppingLocked("handleTrackSubscribed")) {
      return;
    }
    auto& state = extraState();
    state.subscribed_tracks[key] = track;
    auto existing = active_readers_.find(key);
    if (existing != active_readers_.end() && existing->second.track_sid == track->sid() &&
        !readerFinished(existing->second.completion)) {
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
    drain = extractReaderForDrainLocked(key);
  }
  finishReaderDrainAndRestart(key, std::move(drain), "handleTrackSubscribed");
}

void SubscriptionThreadDispatcher::handleTrackUnsubscribed(const std::string& participant_identity, TrackSource source,
                                                           const std::string& track_name) {
  const CallbackKey key{participant_identity, track_name};
  ReaderDrain drain;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    if (rejectWhileStoppingLocked("handleTrackUnsubscribed")) {
      return;
    }
    extraState().subscribed_tracks.erase(key);
    drain = extractReaderForDrainLocked(key);
    LK_LOG_DEBUG(
        "Handling unsubscribed track for participant={} source={} "
        "track_name={} stopped_reader={}",
        participant_identity, static_cast<int>(source), track_name, drain.thread.joinable());
  }
  // Nothing restarts here unless the track was re-subscribed while we were
  // joining, in which case the retained track is picked up.
  finishReaderDrainAndRestart(key, std::move(drain), "handleTrackUnsubscribed");
}

// -------------------------------------------------------------------
// Data track callback registration
// -------------------------------------------------------------------

DataFrameCallbackId SubscriptionThreadDispatcher::addOnDataFrameCallback(const std::string& participant_identity,
                                                                         const std::string& track_name,
                                                                         DataFrameCallback callback) {
  DataFrameCallbackId id;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    if (rejectWhileStoppingLocked("addOnDataFrameCallback")) {
      return std::numeric_limits<DataFrameCallbackId>::max();
    }
    id = next_data_callback_id_++;
    const DataCallbackKey key{participant_identity, track_name};
    data_callbacks_[id] = RegisteredDataCallback{key, std::move(callback)};
    startDataReaderForPublishedTrackLocked(id);
  }
  return id;
}

void SubscriptionThreadDispatcher::removeOnDataFrameCallback(DataFrameCallbackId id) {
  ReaderDrain drain;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    if (rejectWhileStoppingLocked("removeOnDataFrameCallback")) {
      return;
    }
    data_callbacks_.erase(id);
    drain = extractDataReaderForDrainLocked(id);
  }
  finishDataReaderDrainAndRestart(id, std::move(drain), "removeOnDataFrameCallback");
}

void SubscriptionThreadDispatcher::handleDataTrackPublished(const std::shared_ptr<RemoteDataTrack>& track) {
  if (!track) {
    LK_LOG_WARN("handleDataTrackPublished called with null track");
    return;
  }

  LK_LOG_INFO("Handling data track published: \"{}\" from \"{}\" (sid={})", track->info().name,
              track->publisherIdentity(), track->info().sid);

  std::vector<std::pair<DataFrameCallbackId, ReaderDrain>> drains;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    if (rejectWhileStoppingLocked("handleDataTrackPublished")) {
      return;
    }
    const DataCallbackKey key{track->publisherIdentity(), track->info().name};
    remote_data_tracks_[key] = track;

    for (auto& [id, reg] : data_callbacks_) {
      if (!(reg.key == key)) {
        continue;
      }
      auto existing = active_data_readers_.find(id);
      if (existing != active_data_readers_.end() && existing->second && !existing->second->finished.load() &&
          !readerFinished(existing->second->completion) && existing->second->remote_track &&
          existing->second->remote_track->info().sid == track->info().sid) {
        continue;
      }
      auto drain = extractDataReaderForDrainLocked(id);
      if (drain.thread.joinable()) {
        drains.emplace_back(id, std::move(drain));
      } else {
        startDataReaderForPublishedTrackLocked(id);
      }
    }
  }
  for (auto& [id, drain] : drains) {
    finishDataReaderDrainAndRestart(id, std::move(drain), "handleDataTrackPublished");
  }
}

void SubscriptionThreadDispatcher::handleDataTrackUnpublished(const std::string& sid) {
  LK_LOG_INFO("Handling data track unpublished: sid={}", sid);

  std::vector<std::pair<DataFrameCallbackId, ReaderDrain>> drains;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    if (rejectWhileStoppingLocked("handleDataTrackUnpublished")) {
      return;
    }
    for (auto it = remote_data_tracks_.begin(); it != remote_data_tracks_.end(); ++it) {
      if (it->second && it->second->info().sid == sid) {
        remote_data_tracks_.erase(it);
        break;
      }
    }
    std::vector<DataFrameCallbackId> ids;
    for (const auto& [id, reader] : active_data_readers_) {
      if (reader && reader->remote_track && reader->remote_track->info().sid == sid) {
        ids.push_back(id);
      }
    }
    for (const auto id : ids) {
      auto drain = extractDataReaderForDrainLocked(id);
      if (drain.thread.joinable()) {
        drains.emplace_back(id, std::move(drain));
      }
    }
  }
  for (auto& [id, drain] : drains) {
    finishDataReaderDrainAndRestart(id, std::move(drain), "handleDataTrackUnpublished");
  }
}

void SubscriptionThreadDispatcher::stopAll() {
  std::vector<ReaderDrain> readers;
  const auto calling_thread = std::this_thread::get_id();
  {
    std::unique_lock<std::mutex> lock(lock_);
    auto& state = extraState();
    if (state.stopping) {
      const bool called_from_stopping_reader =
          std::find(state.stopping_reader_ids.begin(), state.stopping_reader_ids.end(), calling_thread) !=
          state.stopping_reader_ids.end();
      if (called_from_stopping_reader) {
        LK_LOG_WARN("stopAll called from a reader already being stopped; returning to avoid a shutdown deadlock");
        return;
      }
      state.drain_cv.wait(lock, [&state] { return !state.stopping; });
      return;
    }
    state.stopping = true;
    state.stopping_reader_ids.clear();

    const auto remember_reader_id = [&state](std::thread::id id) {
      if (id != std::thread::id{}) {
        state.stopping_reader_ids.push_back(id);
      }
    };
    for (const auto& [key, reader] : active_readers_) {
      (void)key;
      remember_reader_id(reader.thread_id);
    }
    for (const auto& [id, reader] : active_data_readers_) {
      (void)id;
      if (reader) {
        remember_reader_id(reader->thread_id);
      }
    }
    const auto remember_drains = [&remember_reader_id](const auto& drains) {
      for (const auto& [key, tracked_readers] : drains) {
        (void)key;
        for (const auto& reader : tracked_readers) {
          remember_reader_id(reader.thread_id);
        }
      }
    };
    remember_drains(state.draining_readers);
    remember_drains(state.draining_data_readers);
    for (const auto& reader : state.detached_readers) {
      remember_reader_id(reader.thread_id);
    }

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
        readers.push_back(
            {std::move(reader.thread), reader.thread_id, std::move(reader.completion), state.next_drain_id++});
      }
    }
    active_readers_.clear();
    state.subscribed_tracks.clear();
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
        readers.push_back(
            {std::move(reader->thread), reader->thread_id, std::move(reader->completion), state.next_drain_id++});
      }
    }
    active_data_readers_.clear();
    data_callbacks_.clear();
    remote_data_tracks_.clear();
  }
  std::vector<ExtraState::TrackedReader> detached_readers;
  for (auto& reader : readers) {
    if (disposeReaderThread(std::move(reader.thread), "stopAll") && reader.completion &&
        !readerFinished(reader.completion)) {
      detached_readers.push_back({reader.drain_id, reader.thread_id, std::move(reader.completion)});
    }
  }

  {
    std::unique_lock<std::mutex> lock(lock_);
    auto& state = extraState();
    state.detached_readers.insert(state.detached_readers.end(), std::make_move_iterator(detached_readers.begin()),
                                  std::make_move_iterator(detached_readers.end()));

    const auto has_nonself_drain = [&calling_thread](const auto& drains) {
      for (const auto& [key, tracked_readers] : drains) {
        (void)key;
        for (const auto& reader : tracked_readers) {
          if (reader.thread_id != calling_thread) {
            return true;
          }
        }
      }
      return false;
    };
    state.drain_cv.wait(lock, [&state, &has_nonself_drain] {
      return !has_nonself_drain(state.draining_readers) && !has_nonself_drain(state.draining_data_readers);
    });

    while (true) {
      state.detached_readers.erase(std::remove_if(state.detached_readers.begin(), state.detached_readers.end(),
                                                  [](const auto& reader) { return readerFinished(reader.completion); }),
                                   state.detached_readers.end());

      std::vector<std::shared_ptr<ReaderCompletion>> pending;
      for (const auto& reader : state.detached_readers) {
        if (reader.thread_id != calling_thread) {
          pending.push_back(reader.completion);
        }
      }
      if (pending.empty()) {
        break;
      }

      lock.unlock();
      for (const auto& completion : pending) {
        waitForReader(completion);
      }
      lock.lock();
    }

    state.stopping_reader_ids.clear();
    state.stopping = false;
    state.drain_cv.notify_all();
  }
  LK_LOG_DEBUG("Stopped {} subscription reader threads", readers.size());
}

SubscriptionThreadDispatcher::ReaderDrain SubscriptionThreadDispatcher::extractReaderThreadLocked(
    const CallbackKey& key) {
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
  return {std::move(reader.thread), reader.thread_id, std::move(reader.completion), 0};
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
  if (extraState().stopping) {
    return;
  }
  if (active_readers_.find(key) != active_readers_.end()) {
    return;
  }
  auto& state = extraState();
  if (state.draining_readers.find(key) != state.draining_readers.end()) {
    // Another caller is still joining the previous reader for this key. It
    // will start the reader once the join completes; starting one here would
    // let the new callback overlap the old one.
    LK_LOG_TRACE("Deferring reader start for participant={} track_name={} until the previous reader is joined",
                 key.participant_identity, key.track_name);
    return;
  }
  const auto track_it = state.subscribed_tracks.find(key);
  if (track_it == state.subscribed_tracks.end() || !track_it->second) {
    return;
  }
  startReaderLocked(key, track_it->second);
}

void SubscriptionThreadDispatcher::startAudioReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track,
                                                          const AudioFrameCallback& cb,
                                                          const AudioStream::Options& opts) {
  LK_LOG_DEBUG("Starting audio reader for participant={} track_name={}", key.participant_identity, key.track_name);

  if (liveReaderCountLocked() >= kMaxActiveReaders) {
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
  reader.completion = std::make_shared<ReaderCompletion>();
  reader.track_sid = track->sid();
  auto completion = reader.completion;
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
  reader.thread = std::thread([stream, cb, completion, participant_identity, track_name]() {
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
    markReaderFinished(completion);
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

  if (liveReaderCountLocked() >= kMaxActiveReaders) {
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
  reader.completion = std::make_shared<ReaderCompletion>();
  reader.track_sid = track->sid();
  auto completion = reader.completion;
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
  reader.thread = std::thread([stream = std::move(stream), legacy_cb, event_cb, completion, participant_identity,
                               track_name]() {
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
    markReaderFinished(completion);
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

SubscriptionThreadDispatcher::ReaderDrain SubscriptionThreadDispatcher::extractDataReaderThreadLocked(
    DataFrameCallbackId id) {
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
  return {std::move(reader->thread), reader->thread_id, std::move(reader->completion), 0};
}

SubscriptionThreadDispatcher::ReaderDrain SubscriptionThreadDispatcher::extractDataReaderForDrainLocked(
    DataFrameCallbackId id) {
  ReaderDrain drain = extractDataReaderThreadLocked(id);
  if (drain.thread.joinable()) {
    auto& state = extraState();
    drain.drain_id = state.next_drain_id++;
    state.draining_data_readers[id].push_back({drain.drain_id, drain.thread_id, drain.completion});
  }
  return drain;
}

void SubscriptionThreadDispatcher::finishDataReaderDrainAndRestart(DataFrameCallbackId id, ReaderDrain drain,
                                                                   const char* operation) {
  const bool drained = drain.thread.joinable();
  const bool detached = disposeReaderThread(std::move(drain.thread), operation);

  const std::scoped_lock<std::mutex> lock(lock_);
  auto& state = extraState();
  if (drained) {
    auto it = state.draining_data_readers.find(id);
    if (it != state.draining_data_readers.end()) {
      auto& readers = it->second;
      readers.erase(std::remove_if(readers.begin(), readers.end(),
                                   [&drain](const ExtraState::TrackedReader& reader) {
                                     return reader.drain_id == drain.drain_id;
                                   }),
                    readers.end());
      if (readers.empty()) {
        state.draining_data_readers.erase(it);
      }
    }
    if (detached && drain.completion && !readerFinished(drain.completion)) {
      state.detached_readers.push_back({drain.drain_id, drain.thread_id, drain.completion});
    }
  }
  state.detached_readers.erase(std::remove_if(state.detached_readers.begin(), state.detached_readers.end(),
                                              [](const auto& reader) { return readerFinished(reader.completion); }),
                               state.detached_readers.end());
  state.drain_cv.notify_all();
  if (!state.stopping) {
    startDataReaderForPublishedTrackLocked(id);
  }
}

void SubscriptionThreadDispatcher::markDataReaderFinished(const std::shared_ptr<ActiveDataReader>& reader) {
  reader->finished = true;
  {
    const std::scoped_lock<std::mutex> guard(reader->sub_mutex);
    reader->stream.reset();
  }
  markReaderFinished(reader->completion);
}

void SubscriptionThreadDispatcher::startDataReaderForPublishedTrackLocked(DataFrameCallbackId id) {
  auto& state = extraState();
  if (state.stopping || state.draining_data_readers.find(id) != state.draining_data_readers.end() ||
      active_data_readers_.find(id) != active_data_readers_.end()) {
    return;
  }
  const auto callback_it = data_callbacks_.find(id);
  if (callback_it == data_callbacks_.end()) {
    return;
  }
  const auto track_it = remote_data_tracks_.find(callback_it->second.key);
  if (track_it == remote_data_tracks_.end() || !track_it->second) {
    return;
  }
  startDataReaderLocked(id, callback_it->second.key, track_it->second, callback_it->second.callback);
}

void SubscriptionThreadDispatcher::startDataReaderLocked(DataFrameCallbackId id, const DataCallbackKey& key,
                                                         const std::shared_ptr<RemoteDataTrack>& track,
                                                         const DataFrameCallback& cb) {
  if (active_data_readers_.find(id) != active_data_readers_.end()) {
    LK_LOG_ERROR("Refusing to start data reader id={} because one is already active", id);
    return;
  }

  if (liveReaderCountLocked() >= kMaxActiveReaders) {
    LK_LOG_ERROR(
        "Cannot start data reader for {} track={}: active reader "
        "limit ({}) reached",
        key.participant_identity, key.track_name, kMaxActiveReaders);
    return;
  }

  LK_LOG_INFO("Starting data reader for \"{}\" track=\"{}\"", key.participant_identity, key.track_name);

  auto reader = std::make_shared<ActiveDataReader>();
  reader->remote_track = track;
  reader->completion = std::make_shared<ReaderCompletion>();
  auto identity = key.participant_identity;
  auto track_name = key.track_name;
  // NOLINTBEGIN(bugprone-lambda-function-name)
  // Deliberately captures no `this`: the thread reports its exit through the
  // shared ActiveDataReader only, which is what allows disposeReaderThread to
  // detach it when torn down from inside its own callback.
  reader->thread = std::thread([reader, track, cb, identity, track_name]() {
    try {
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
    } catch (const std::exception& e) {
      LK_LOG_ERROR("Data reader thread terminating due to exception: {}", e.what());
    } catch (...) {
      LK_LOG_ERROR("Data reader thread terminating due to unknown exception");
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
}

} // namespace livekit
