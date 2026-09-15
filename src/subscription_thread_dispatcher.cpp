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
#include <exception>
#include <system_error>
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
// Exceptions can be thrown by stopAll() in this destructor, and clang flags as
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
    // Callers normally catch this in beginDrainLocked / extractDataReaderThreadLocked;
    // this is the last line of defense against std::system_error on self-join.
    LK_LOG_WARN("{}: refusing to self-join the calling reader thread; detaching it instead", operation);
    thread.detach();
    return;
  }
  // Blocking: returns once the reader loop has exited, i.e. after any in-flight
  // callback invocation has returned and the reader released its stream.
  thread.join();
}

void SubscriptionThreadDispatcher::waitForReaderExit(const std::shared_ptr<ReaderExit>& exit, std::thread::id owner,
                                                     const char* operation) {
  if (!exit) {
    return;
  }
  if (isSelfThread(owner)) {
    LK_LOG_WARN(
        "{}: called from inside the frame callback of the reader that is being "
        "stopped; not waiting for it to exit. Registering, clearing, or tearing "
        "down from within a frame callback is discouraged",
        operation);
    return;
  }
  // Blocking: another caller owns the join for this reader; wait until the
  // reader itself signals that it has exited.
  exit->wait();
}

void SubscriptionThreadDispatcher::closeSlotStreams(ActiveReader& slot) {
  if (slot.audio_stream) {
    slot.audio_stream->close();
    slot.audio_stream.reset();
  }
  if (slot.video_stream) {
    slot.video_stream->close();
    slot.video_stream.reset();
  }
}

bool SubscriptionThreadDispatcher::slotReaderKindIs(const ActiveReader& slot, TrackKind kind) {
  // A reader is only ever started for the retained track's kind, so the track
  // tells us which kind of reader (if any) the slot is running.
  return slot.track != nullptr && slot.track->kind() == kind;
}

void SubscriptionThreadDispatcher::markDataReaderFinished(const std::shared_ptr<ActiveDataReader>& reader) {
  {
    const std::scoped_lock<std::mutex> guard(reader->sub_mutex);
    reader->stream.reset();
  }
  if (reader->exit) {
    reader->exit->signal();
  }
}

std::thread SubscriptionThreadDispatcher::extractReaderThreadLocked(const CallbackKey& key) {
  auto it = active_readers_.find(key);
  if (it == active_readers_.end() || !it->second.thread.joinable() || it->second.draining) {
    LK_LOG_TRACE("No active reader to extract for participant={} track_name={}", key.participant_identity,
                 key.track_name);
    return {};
  }

  LK_LOG_DEBUG("Extracting active reader for participant={} track_name={}", key.participant_identity, key.track_name);
  auto& slot = it->second;
  closeSlotStreams(slot);
  slot.draining = true;
  return std::move(slot.thread);
}

SubscriptionThreadDispatcher::DrainHandle SubscriptionThreadDispatcher::beginDrainLocked(const CallbackKey& key,
                                                                                         const char* operation) {
  auto it = active_readers_.find(key);
  if (it == active_readers_.end() || !it->second.thread.joinable() || it->second.draining) {
    return {};
  }
  auto& slot = it->second;
  if (isSelfThread(slot.thread_id)) {
    // The caller IS this reader, i.e. a lifecycle call was made from inside the
    // frame callback. Joining would be a self-join. Detaching is safe: no
    // reader lambda captures `this`; each owns its stream, callback and exit
    // signal by value, so once its stream is closed the thread touches nothing
    // owned by the dispatcher and exits as soon as the callback returns. The
    // exit signal is kept so stopAll() can still wait for it.
    LK_LOG_WARN(
        "{} was called from inside the frame callback of the reader it stops "
        "(participant={} track_name={}); detaching that reader instead of "
        "self-joining. It exits once the callback returns. Registering, clearing, "
        "or tearing down from within a frame callback is discouraged",
        operation, key.participant_identity, key.track_name);
    closeSlotStreams(slot);
    slot.thread.detach();
    slot.detached.emplace_back(slot.exit, slot.thread_id);
    return {};
  }

  DrainHandle handle;
  handle.key = key;
  handle.token = slot.exit;
  handle.thread = extractReaderThreadLocked(key);
  return handle;
}

void SubscriptionThreadDispatcher::finishDrain(DrainHandle&& handle, const char* operation) {
  if (!handle.joinable()) {
    return;
  }
  // Blocking join, performed with lock_ released.
  disposeReaderThread(std::move(handle.thread), operation);

  const std::scoped_lock<std::mutex> lock(lock_);
  auto it = active_readers_.find(handle.key);
  if (it == active_readers_.end()) {
    LK_LOG_TRACE("{}: slot for participant={} track_name={} is gone after drain (stopAll or unsubscribe)", operation,
                 handle.key.participant_identity, handle.key.track_name);
    return;
  }
  auto& slot = it->second;
  if (!slot.draining || slot.exit != handle.token) {
    // The slot was recreated (stopAll + re-subscribe) while we were joining;
    // it is not our drain anymore.
    LK_LOG_TRACE("{}: drain token mismatch for participant={} track_name={}; leaving slot untouched", operation,
                 handle.key.participant_identity, handle.key.track_name);
    return;
  }
  slot.draining = false;
  reconcileLocked(handle.key);

  const auto after = active_readers_.find(handle.key);
  LK_LOG_DEBUG("{}: drain complete for participant={} track_name={} slot_retained={} reader_active={}", operation,
               handle.key.participant_identity, handle.key.track_name, after != active_readers_.end(),
               after != active_readers_.end() && after->second.thread.joinable());
}

void SubscriptionThreadDispatcher::reconcileLocked(const CallbackKey& key) {
  auto it = active_readers_.find(key);
  if (it == active_readers_.end()) {
    return;
  }
  auto& slot = it->second;
  if (slot.draining) {
    // The drain owner reconciles once the previous reader is joined.
    return;
  }
  if (slot.thread.joinable()) {
    // Running, or finished and awaiting reap by the next lifecycle call.
    return;
  }
  if (!slot.track) {
    if (slot.detached.empty()) {
      LK_LOG_TRACE("Releasing subscription slot for participant={} track_name={}", key.participant_identity,
                   key.track_name);
      active_readers_.erase(it);
    }
    return;
  }
  if (slot.track_ended) {
    LK_LOG_TRACE(
        "Not restarting reader for participant={} track_name={}: its stream ended "
        "on its own; waiting for a new subscribe event or registration",
        key.participant_identity, key.track_name);
    return;
  }
  const auto track = slot.track;
  (void)startReaderLocked(key, track);
}

void SubscriptionThreadDispatcher::reapFinishedReadersLocked(std::vector<DrainHandle>& media_drains,
                                                             std::vector<std::thread>& data_threads) {
  for (auto it = active_readers_.begin(); it != active_readers_.end();) {
    auto& slot = it->second;
    slot.detached.erase(std::remove_if(slot.detached.begin(), slot.detached.end(),
                                       [](const auto& d) { return !d.first || d.first->isDone(); }),
                        slot.detached.end());
    if (!slot.draining && slot.readerFinished()) {
      // The reader exited without us closing it: FFI end-of-stream or an error.
      // Drain it like any other stop so its thread is joined and its stream
      // released, but do not auto-restart on a track whose stream just ended.
      LK_LOG_DEBUG("Reaping reader for participant={} track_name={} whose stream ended on its own",
                   it->first.participant_identity, it->first.track_name);
      slot.track_ended = true;
      auto handle = beginDrainLocked(it->first, "reap");
      if (handle.joinable()) {
        media_drains.push_back(std::move(handle));
      }
    }
    if (!slot.track && !slot.thread.joinable() && !slot.draining && slot.detached.empty()) {
      it = active_readers_.erase(it);
      continue;
    }
    ++it;
  }

  for (auto it = active_data_readers_.begin(); it != active_data_readers_.end();) {
    auto& reader = it->second;
    if (!reader || reader->finished()) {
      if (reader && reader->thread.joinable()) {
        data_threads.push_back(std::move(reader->thread));
      }
      it = active_data_readers_.erase(it);
      continue;
    }
    ++it;
  }
}

void SubscriptionThreadDispatcher::disposeReaped(std::vector<DrainHandle>&& media_drains,
                                                 std::vector<std::thread>&& data_threads, const char* operation) {
  for (auto& thread : data_threads) {
    disposeReaderThread(std::move(thread), operation);
  }
  for (auto& handle : media_drains) {
    finishDrain(std::move(handle), operation);
  }
}

int SubscriptionThreadDispatcher::runningReaderCountLocked() {
  int count = 0;
  for (auto& [key, slot] : active_readers_) {
    (void)key;
    if (slot.readerRunning()) {
      ++count;
    }
  }
  for (auto& [id, reader] : active_data_readers_) {
    (void)id;
    if (reader && reader->exit && !reader->exit->isDone()) {
      ++count;
    }
  }
  return count;
}

// -------------------------------------------------------------------
// Audio/video callback registration
// -------------------------------------------------------------------

void SubscriptionThreadDispatcher::setOnAudioFrameCallback(const std::string& participant_identity,
                                                           const std::string& track_name, AudioFrameCallback callback,
                                                           const AudioStream::Options& opts) {
  static constexpr const char* kOp = "setOnAudioFrameCallback";
  const CallbackKey key{participant_identity, track_name};
  std::vector<DrainHandle> reaped;
  std::vector<std::thread> reaped_data;
  DrainHandle own;
  std::shared_ptr<ReaderExit> wait_exit;
  std::thread::id wait_owner;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    reapFinishedReadersLocked(reaped, reaped_data);
    const bool replacing = audio_callbacks_.find(key) != audio_callbacks_.end();
    audio_callbacks_[key] = RegisteredAudioCallback{std::move(callback), opts};

    auto it = active_readers_.find(key);
    const bool subscribed = it != active_readers_.end() && it->second.track != nullptr;
    if (it != active_readers_.end()) {
      auto& slot = it->second;
      slot.track_ended = false; // explicit re-registration is a request to try again
      if (slot.draining) {
        wait_exit = slot.exit;
        wait_owner = slot.thread_id;
      } else if (slot.thread.joinable() && slotReaderKindIs(slot, TrackKind::KIND_AUDIO)) {
        own = beginDrainLocked(key, kOp);
        if (!own.joinable()) {
          reconcileLocked(key);
        }
      } else {
        reconcileLocked(key);
      }
    }
    LK_LOG_DEBUG(
        "Registered audio frame callback for participant={} track_name={} "
        "replacing_existing={} track_subscribed={} stopping_reader={} "
        "waiting_for_drain={} total_audio_callbacks={}",
        participant_identity, track_name, replacing, subscribed, own.joinable(), wait_exit != nullptr,
        audio_callbacks_.size());
  }
  // Blocking section: join the previous reader (ours or a concurrent caller's)
  // before returning so the old callback is never invoked after this call.
  disposeReaped(std::move(reaped), std::move(reaped_data), kOp);
  finishDrain(std::move(own), kOp);
  waitForReaderExit(wait_exit, wait_owner, kOp);
}

void SubscriptionThreadDispatcher::setOnVideoFrameEventCallback(const std::string& participant_identity,
                                                                const std::string& track_name,
                                                                VideoFrameEventCallback callback,
                                                                const VideoStream::Options& opts) {
  static constexpr const char* kOp = "setOnVideoFrameEventCallback";
  const CallbackKey key{participant_identity, track_name};
  std::vector<DrainHandle> reaped;
  std::vector<std::thread> reaped_data;
  DrainHandle own;
  std::shared_ptr<ReaderExit> wait_exit;
  std::thread::id wait_owner;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    reapFinishedReadersLocked(reaped, reaped_data);
    const bool replacing = video_callbacks_.find(key) != video_callbacks_.end();
    video_callbacks_[key] = RegisteredVideoCallback{
        VideoFrameCallback{},
        std::move(callback),
        opts,
    };

    auto it = active_readers_.find(key);
    const bool subscribed = it != active_readers_.end() && it->second.track != nullptr;
    if (it != active_readers_.end()) {
      auto& slot = it->second;
      slot.track_ended = false;
      if (slot.draining) {
        wait_exit = slot.exit;
        wait_owner = slot.thread_id;
      } else if (slot.thread.joinable() && slotReaderKindIs(slot, TrackKind::KIND_VIDEO)) {
        own = beginDrainLocked(key, kOp);
        if (!own.joinable()) {
          reconcileLocked(key);
        }
      } else {
        reconcileLocked(key);
      }
    }
    LK_LOG_DEBUG(
        "Registered video frame event callback for participant={} track_name={} "
        "replacing_existing={} track_subscribed={} stopping_reader={} "
        "waiting_for_drain={} total_video_callbacks={}",
        participant_identity, track_name, replacing, subscribed, own.joinable(), wait_exit != nullptr,
        video_callbacks_.size());
  }
  // Blocking section; see setOnAudioFrameCallback.
  disposeReaped(std::move(reaped), std::move(reaped_data), kOp);
  finishDrain(std::move(own), kOp);
  waitForReaderExit(wait_exit, wait_owner, kOp);
}

void SubscriptionThreadDispatcher::setOnVideoFrameCallback(const std::string& participant_identity,
                                                           const std::string& track_name, VideoFrameCallback callback,
                                                           const VideoStream::Options& opts) {
  static constexpr const char* kOp = "setOnVideoFrameCallback";
  const CallbackKey key{participant_identity, track_name};
  std::vector<DrainHandle> reaped;
  std::vector<std::thread> reaped_data;
  DrainHandle own;
  std::shared_ptr<ReaderExit> wait_exit;
  std::thread::id wait_owner;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    reapFinishedReadersLocked(reaped, reaped_data);
    const bool replacing = video_callbacks_.find(key) != video_callbacks_.end();
    video_callbacks_[key] = RegisteredVideoCallback{
        std::move(callback),
        VideoFrameEventCallback{},
        opts,
    };

    auto it = active_readers_.find(key);
    const bool subscribed = it != active_readers_.end() && it->second.track != nullptr;
    if (it != active_readers_.end()) {
      auto& slot = it->second;
      slot.track_ended = false;
      if (slot.draining) {
        wait_exit = slot.exit;
        wait_owner = slot.thread_id;
      } else if (slot.thread.joinable() && slotReaderKindIs(slot, TrackKind::KIND_VIDEO)) {
        own = beginDrainLocked(key, kOp);
        if (!own.joinable()) {
          reconcileLocked(key);
        }
      } else {
        reconcileLocked(key);
      }
    }
    LK_LOG_DEBUG(
        "Registered video frame callback for participant={} track_name={} "
        "replacing_existing={} track_subscribed={} stopping_reader={} "
        "waiting_for_drain={} total_video_callbacks={}",
        participant_identity, track_name, replacing, subscribed, own.joinable(), wait_exit != nullptr,
        video_callbacks_.size());
  }
  // Blocking section; see setOnAudioFrameCallback.
  disposeReaped(std::move(reaped), std::move(reaped_data), kOp);
  finishDrain(std::move(own), kOp);
  waitForReaderExit(wait_exit, wait_owner, kOp);
}

void SubscriptionThreadDispatcher::clearOnAudioFrameCallback(const std::string& participant_identity,
                                                             const std::string& track_name) {
  static constexpr const char* kOp = "clearOnAudioFrameCallback";
  const CallbackKey key{participant_identity, track_name};
  std::vector<DrainHandle> reaped;
  std::vector<std::thread> reaped_data;
  DrainHandle own;
  std::shared_ptr<ReaderExit> wait_exit;
  std::thread::id wait_owner;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    reapFinishedReadersLocked(reaped, reaped_data);
    const bool removed_callback = audio_callbacks_.erase(key) > 0;

    auto it = active_readers_.find(key);
    if (it != active_readers_.end()) {
      auto& slot = it->second;
      if (slot.draining) {
        wait_exit = slot.exit;
        wait_owner = slot.thread_id;
      } else if (slot.thread.joinable() && slotReaderKindIs(slot, TrackKind::KIND_AUDIO)) {
        own = beginDrainLocked(key, kOp);
        if (!own.joinable()) {
          reconcileLocked(key);
        }
      } else {
        reconcileLocked(key);
      }
    }
    LK_LOG_DEBUG(
        "Clearing audio frame callback for participant={} track_name={} "
        "removed_callback={} stopping_reader={} waiting_for_drain={} remaining_audio_callbacks={}",
        participant_identity, track_name, removed_callback, own.joinable(), wait_exit != nullptr,
        audio_callbacks_.size());
  }
  // Blocking section: the callback is not invoked again once we return.
  disposeReaped(std::move(reaped), std::move(reaped_data), kOp);
  finishDrain(std::move(own), kOp);
  waitForReaderExit(wait_exit, wait_owner, kOp);
}

void SubscriptionThreadDispatcher::clearOnVideoFrameCallback(const std::string& participant_identity,
                                                             const std::string& track_name) {
  static constexpr const char* kOp = "clearOnVideoFrameCallback";
  const CallbackKey key{participant_identity, track_name};
  std::vector<DrainHandle> reaped;
  std::vector<std::thread> reaped_data;
  DrainHandle own;
  std::shared_ptr<ReaderExit> wait_exit;
  std::thread::id wait_owner;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    reapFinishedReadersLocked(reaped, reaped_data);
    const bool removed_callback = video_callbacks_.erase(key) > 0;

    auto it = active_readers_.find(key);
    if (it != active_readers_.end()) {
      auto& slot = it->second;
      if (slot.draining) {
        wait_exit = slot.exit;
        wait_owner = slot.thread_id;
      } else if (slot.thread.joinable() && slotReaderKindIs(slot, TrackKind::KIND_VIDEO)) {
        own = beginDrainLocked(key, kOp);
        if (!own.joinable()) {
          reconcileLocked(key);
        }
      } else {
        reconcileLocked(key);
      }
    }
    LK_LOG_DEBUG(
        "Clearing video frame callback for participant={} track_name={} "
        "removed_callback={} stopping_reader={} waiting_for_drain={} remaining_video_callbacks={}",
        participant_identity, track_name, removed_callback, own.joinable(), wait_exit != nullptr,
        video_callbacks_.size());
  }
  // Blocking section: the callback is not invoked again once we return.
  disposeReaped(std::move(reaped), std::move(reaped_data), kOp);
  finishDrain(std::move(own), kOp);
  waitForReaderExit(wait_exit, wait_owner, kOp);
}

void SubscriptionThreadDispatcher::handleTrackSubscribed(const std::string& participant_identity,
                                                         const std::string& track_name,
                                                         const std::shared_ptr<Track>& track) {
  static constexpr const char* kOp = "handleTrackSubscribed";
  if (!track) {
    LK_LOG_WARN("Ignoring subscribed track dispatch for participant={} track_name={} because track is null",
                participant_identity, track_name);
    return;
  }

  LK_LOG_DEBUG("Handling subscribed track for participant={} track_name={} kind={} sid={}", participant_identity,
               track_name, trackKindName(track->kind()), track->sid());

  const CallbackKey key{participant_identity, track_name};
  std::vector<DrainHandle> reaped;
  std::vector<std::thread> reaped_data;
  DrainHandle own;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    reapFinishedReadersLocked(reaped, reaped_data);

    // Retain the subscription so a callback registered later can start a
    // reader immediately (GitHub issue #235).
    auto& slot = active_readers_[key];
    slot.track = track;
    slot.track_ended = false;

    if (slot.draining) {
      LK_LOG_DEBUG("Reader for participant={} track_name={} is draining; it restarts on the new track afterwards",
                   participant_identity, track_name);
    } else if (slot.thread.joinable()) {
      if (slot.track_sid == track->sid()) {
        // A duplicate track_subscribed for the publication this reader already
        // serves. Rebuilding the reader would only churn the stream.
        LK_LOG_DEBUG(
            "Skipping reader restart for participant={} track_name={} because a "
            "reader for sid={} is already active",
            participant_identity, track_name, track->sid());
      } else {
        // A reader for a previous publication under the same name (republish):
        // stop it and rebuild against the new track once it is joined.
        LK_LOG_DEBUG("Replacing reader for participant={} track_name={} (sid {} -> {})", participant_identity,
                     track_name, slot.track_sid, track->sid());
        own = beginDrainLocked(key, kOp);
        if (!own.joinable()) {
          reconcileLocked(key);
        }
      }
    } else {
      reconcileLocked(key);
    }
  }
  // Blocking only when a previous reader had to be replaced.
  disposeReaped(std::move(reaped), std::move(reaped_data), kOp);
  finishDrain(std::move(own), kOp);
}

void SubscriptionThreadDispatcher::handleTrackUnsubscribed(const std::string& participant_identity, TrackSource source,
                                                           const std::string& track_name) {
  static constexpr const char* kOp = "handleTrackUnsubscribed";
  const CallbackKey key{participant_identity, track_name};
  std::vector<DrainHandle> reaped;
  std::vector<std::thread> reaped_data;
  DrainHandle own;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    reapFinishedReadersLocked(reaped, reaped_data);

    auto it = active_readers_.find(key);
    const bool had_slot = it != active_readers_.end();
    bool stopping_reader = false;
    if (had_slot) {
      auto& slot = it->second;
      slot.track.reset();
      if (slot.draining) {
        // The drain owner erases the slot once the previous reader is joined.
      } else if (slot.thread.joinable()) {
        own = beginDrainLocked(key, kOp);
        stopping_reader = true;
        if (!own.joinable()) {
          reconcileLocked(key);
        }
      } else {
        reconcileLocked(key); // releases the slot
      }
    }
    LK_LOG_DEBUG(
        "Handling unsubscribed track for participant={} source={} "
        "track_name={} had_subscription={} stopping_reader={}",
        participant_identity, static_cast<int>(source), track_name, had_slot, stopping_reader);
  }
  // Blocking while the reader is joined.
  disposeReaped(std::move(reaped), std::move(reaped_data), kOp);
  finishDrain(std::move(own), kOp);
}

// -------------------------------------------------------------------
// Data track callback registration
// -------------------------------------------------------------------

DataFrameCallbackId SubscriptionThreadDispatcher::addOnDataFrameCallback(const std::string& participant_identity,
                                                                         const std::string& track_name,
                                                                         DataFrameCallback callback) {
  static constexpr const char* kOp = "addOnDataFrameCallback";
  std::vector<DrainHandle> reaped;
  std::vector<std::thread> reaped_data;
  std::thread old_thread;
  DataFrameCallbackId id;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    reapFinishedReadersLocked(reaped, reaped_data);
    id = next_data_callback_id_++;
    const DataCallbackKey key{participant_identity, track_name};
    data_callbacks_[id] = RegisteredDataCallback{key, std::move(callback)};

    auto track_it = remote_data_tracks_.find(key);
    if (track_it != remote_data_tracks_.end()) {
      old_thread = startDataReaderLocked(id, key, track_it->second, data_callbacks_[id].callback);
    }
  }
  disposeReaped(std::move(reaped), std::move(reaped_data), kOp);
  disposeReaderThread(std::move(old_thread), kOp);
  return id;
}

void SubscriptionThreadDispatcher::removeOnDataFrameCallback(DataFrameCallbackId id) {
  static constexpr const char* kOp = "removeOnDataFrameCallback";
  std::vector<DrainHandle> reaped;
  std::vector<std::thread> reaped_data;
  std::thread old_thread;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    reapFinishedReadersLocked(reaped, reaped_data);
    data_callbacks_.erase(id);
    old_thread = extractDataReaderThreadLocked(id);
  }
  // Blocking: the callback is not invoked again once we return (unless this is
  // called from inside that very callback, in which case the reader was detached).
  disposeReaped(std::move(reaped), std::move(reaped_data), kOp);
  disposeReaderThread(std::move(old_thread), kOp);
}

void SubscriptionThreadDispatcher::handleDataTrackPublished(const std::shared_ptr<RemoteDataTrack>& track) {
  static constexpr const char* kOp = "handleDataTrackPublished";
  if (!track) {
    LK_LOG_WARN("handleDataTrackPublished called with null track");
    return;
  }

  LK_LOG_INFO("Handling data track published: \"{}\" from \"{}\" (sid={})", track->info().name,
              track->publisherIdentity(), track->info().sid);

  const DataCallbackKey key{track->publisherIdentity(), track->info().name};
  const std::string sid = track->info().sid;
  std::vector<DrainHandle> reaped;
  std::vector<std::thread> reaped_data;
  std::vector<std::thread> old_threads;
  std::vector<DataFrameCallbackId> to_start;
  {
    // Phase 1: retain the track and stop readers bound to a previous
    // publication under the same name.
    const std::scoped_lock<std::mutex> lock(lock_);
    reapFinishedReadersLocked(reaped, reaped_data);
    remote_data_tracks_[key] = track;

    for (auto& [id, reg] : data_callbacks_) {
      if (!(reg.key == key)) {
        continue;
      }
      auto existing = active_data_readers_.find(id);
      if (existing != active_data_readers_.end() && existing->second) {
        auto& reader = existing->second;
        if (reader->thread.joinable() && !reader->finished() && reader->remote_track &&
            reader->remote_track->info().sid == sid) {
          LK_LOG_DEBUG("Skipping data reader start for \"{}\" track=\"{}\" id={}: already serving sid={}",
                       key.participant_identity, key.track_name, id, sid);
          continue;
        }
        auto t = extractDataReaderThreadLocked(id);
        if (t.joinable()) {
          old_threads.push_back(std::move(t));
        }
      }
      to_start.push_back(id);
    }
  }

  // Blocking: join the previous readers before their replacements start so the
  // old and new data callbacks never run concurrently.
  disposeReaped(std::move(reaped), std::move(reaped_data), kOp);
  for (auto& t : old_threads) {
    disposeReaderThread(std::move(t), kOp);
  }

  std::vector<std::thread> leftovers;
  {
    // Phase 2: start readers for registrations that are still present and not
    // already served (a concurrent addOnDataFrameCallback or a newer publish
    // may have raced us while we were joining).
    const std::scoped_lock<std::mutex> lock(lock_);
    auto track_it = remote_data_tracks_.find(key);
    if (track_it == remote_data_tracks_.end() || track_it->second != track) {
      LK_LOG_DEBUG("Data track \"{}\" from \"{}\" changed while joining previous readers; not starting readers",
                   key.track_name, key.participant_identity);
    } else {
      for (const auto id : to_start) {
        auto reg = data_callbacks_.find(id);
        if (reg == data_callbacks_.end()) {
          continue;
        }
        auto existing = active_data_readers_.find(id);
        if (existing != active_data_readers_.end() && existing->second && existing->second->thread.joinable() &&
            !existing->second->finished()) {
          continue;
        }
        auto t = startDataReaderLocked(id, key, track, reg->second.callback);
        if (t.joinable()) {
          leftovers.push_back(std::move(t));
        }
      }
    }
  }
  for (auto& t : leftovers) {
    disposeReaderThread(std::move(t), kOp);
  }
}

void SubscriptionThreadDispatcher::handleDataTrackUnpublished(const std::string& sid) {
  static constexpr const char* kOp = "handleDataTrackUnpublished";
  LK_LOG_INFO("Handling data track unpublished: sid={}", sid);

  std::vector<DrainHandle> reaped;
  std::vector<std::thread> reaped_data;
  std::vector<std::thread> old_threads;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    reapFinishedReadersLocked(reaped, reaped_data);
    for (auto it = active_data_readers_.begin(); it != active_data_readers_.end();) {
      auto& reader = it->second;
      if (!reader || !reader->remote_track || reader->remote_track->info().sid != sid) {
        ++it;
        continue;
      }
      // Mark cancelled before closing so a subscribe() still in flight aborts.
      reader->cancelled = true;
      {
        const std::scoped_lock<std::mutex> sub_guard(reader->sub_mutex);
        if (reader->stream) {
          reader->stream->close();
        }
      }
      if (reader->thread.joinable()) {
        if (isSelfThread(reader->thread_id)) {
          LK_LOG_WARN("{} reached from inside the data callback it stops (sid={}); detaching that reader", kOp, sid);
          reader->thread.detach();
          reader->detached = true;
          ++it;
          continue;
        }
        old_threads.push_back(std::move(reader->thread));
      } else if (reader->detached && !reader->finished()) {
        // Keep the entry so stopAll()/reap can still wait for the detached reader.
        ++it;
        continue;
      }
      it = active_data_readers_.erase(it);
    }
    for (auto it = remote_data_tracks_.begin(); it != remote_data_tracks_.end(); ++it) {
      if (it->second && it->second->info().sid == sid) {
        remote_data_tracks_.erase(it);
        break;
      }
    }
  }
  // Blocking while the readers are joined.
  disposeReaped(std::move(reaped), std::move(reaped_data), kOp);
  for (auto& t : old_threads) {
    disposeReaderThread(std::move(t), kOp);
  }
}

void SubscriptionThreadDispatcher::stopAll() {
  static constexpr const char* kOp = "stopAll";
  std::vector<std::thread> own;
  std::vector<std::pair<std::shared_ptr<ReaderExit>, std::thread::id>> foreign;
  {
    const std::scoped_lock<std::mutex> lock(lock_);
    LK_LOG_DEBUG(
        "Stopping all subscription readers subscription_slots={} "
        "active_data_readers={} audio_callbacks={} "
        "video_callbacks={} data_callbacks={}",
        active_readers_.size(), active_data_readers_.size(), audio_callbacks_.size(), video_callbacks_.size(),
        data_callbacks_.size());

    for (auto& [key, slot] : active_readers_) {
      (void)key;
      if (slot.thread.joinable()) {
        closeSlotStreams(slot);
        own.push_back(std::move(slot.thread));
      } else if (slot.draining && slot.exit) {
        // Another caller is joining this reader; we cannot join it too, but we
        // can wait for the reader itself to signal exit.
        foreign.emplace_back(slot.exit, slot.thread_id);
      }
      for (auto& detached : slot.detached) {
        foreign.push_back(detached);
      }
    }
    active_readers_.clear();
    audio_callbacks_.clear();
    video_callbacks_.clear();

    for (auto& [id, reader] : active_data_readers_) {
      (void)id;
      if (!reader) {
        continue;
      }
      // Mark cancelled before closing so a subscribe() still in flight aborts.
      reader->cancelled = true;
      {
        const std::scoped_lock<std::mutex> sub_guard(reader->sub_mutex);
        if (reader->stream) {
          reader->stream->close();
        }
      }
      if (reader->thread.joinable()) {
        own.push_back(std::move(reader->thread));
      } else if (reader->detached && reader->exit) {
        foreign.emplace_back(reader->exit, reader->thread_id);
      }
    }
    active_data_readers_.clear();
    data_callbacks_.clear();
    remote_data_tracks_.clear();
  }

  // Blocking: join every reader we own. A reader that reached stopAll() from
  // inside its own callback (e.g. the application called Room::disconnect()
  // from a frame callback) is detached rather than self-joined.
  for (auto& thread : own) {
    disposeReaderThread(std::move(thread), kOp);
  }
  // Then wait for readers owned by concurrent drains or previously detached, so
  // that on return no reader is still executing a callback (except a caller
  // that is itself a reader).
  for (auto& [exit, owner] : foreign) {
    waitForReaderExit(exit, owner, kOp);
  }
  LK_LOG_DEBUG("Stopped {} subscription reader threads and waited for {} readers owned elsewhere", own.size(),
               foreign.size());
}

// -------------------------------------------------------------------
// Audio/video reader helpers
// -------------------------------------------------------------------

std::thread SubscriptionThreadDispatcher::startReaderLocked(const CallbackKey& key,
                                                            const std::shared_ptr<Track>& track) {
  if (!track) {
    return {};
  }
  auto slot_it = active_readers_.find(key);
  if (slot_it != active_readers_.end() && (slot_it->second.thread.joinable() || slot_it->second.draining)) {
    // Callers stop the previous reader through the drain protocol before
    // getting here, so this indicates a lifecycle bug. Replacing the slot's
    // thread would drop a joinable std::thread, so leave the existing reader alone.
    LK_LOG_ERROR(
        "Refusing to start a reader for participant={} track_name={} because one "
        "is already active or draining; the previous reader must be stopped first",
        key.participant_identity, key.track_name);
    return {};
  }

  if (track->kind() == TrackKind::KIND_AUDIO) {
    auto it = audio_callbacks_.find(key);
    if (it == audio_callbacks_.end()) {
      LK_LOG_TRACE(
          "Skipping audio reader start for participant={} track_name={} "
          "because no audio callback is registered",
          key.participant_identity, key.track_name);
      return {};
    }
    return startAudioReaderLocked(key, track, it->second.callback, it->second.options);
  }
  if (track->kind() == TrackKind::KIND_VIDEO) {
    auto it = video_callbacks_.find(key);
    if (it == video_callbacks_.end()) {
      LK_LOG_TRACE(
          "Skipping video reader start for participant={} track_name={} "
          "because no video callback is registered",
          key.participant_identity, key.track_name);
      return {};
    }
    return startVideoReaderLocked(key, track, it->second);
  }
  if (track->kind() == TrackKind::KIND_UNKNOWN) {
    LK_LOG_WARN(
        "Skipping reader start for participant={} track_name={} because track "
        "kind is unknown",
        key.participant_identity, key.track_name);
    return {};
  }

  LK_LOG_WARN(
      "Skipping reader start for participant={} track_name={} because track kind "
      "is unsupported",
      key.participant_identity, key.track_name);
  return {};
}

std::thread SubscriptionThreadDispatcher::startAudioReaderLocked(const CallbackKey& key,
                                                                 const std::shared_ptr<Track>& track,
                                                                 const AudioFrameCallback& cb,
                                                                 const AudioStream::Options& opts) {
  LK_LOG_DEBUG("Starting audio reader for participant={} track_name={}", key.participant_identity, key.track_name);

  if (runningReaderCountLocked() >= kMaxActiveReaders) {
    LK_LOG_ERROR(
        "Cannot start audio reader for {} track_name={}: active reader limit ({}) "
        "reached",
        key.participant_identity, key.track_name, kMaxActiveReaders);
    return {};
  }

  std::shared_ptr<AudioStream> stream;
  try {
    stream = AudioStream::fromTrack(track, opts);
  } catch (const std::exception& e) {
    LK_LOG_ERROR("Failed to create AudioStream for {} track_name={}: {}", key.participant_identity, key.track_name,
                 e.what());
    return {};
  }
  if (!stream) {
    LK_LOG_ERROR("Failed to create AudioStream for {} track_name={}", key.participant_identity, key.track_name);
    return {};
  }

  auto exit = std::make_shared<ReaderExit>();
  const std::string participant_identity = key.participant_identity;
  const std::string track_name = key.track_name;
  std::thread thread;
  // NOLINTBEGIN(bugprone-lambda-function-name,bugprone-exception-escape)
  // Outer try/catch contains anything escaping the per-frame try/catch
  // (stream->read, LK_LOG formatting, etc.) so an exception in this reader
  // thread cannot std::terminate the process. clang-tidy still flags a
  // residual escape path through spdlog's own formatter; that's a logger
  // fault, not application logic -- suppressed at the lambda level.
  //
  // Deliberately captures no `this`: see beginDrainLocked.
  try {
    thread = std::thread([stream, cb = AudioFrameCallback(cb), participant_identity, track_name, exit]() mutable {
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
      // Release the stream (FFI handle + listener) and the callback copy on this
      // thread, then signal exit so waiters know both are gone.
      stream.reset();
      cb = nullptr;
      exit->signal();
    });
  } catch (const std::system_error& e) {
    LK_LOG_ERROR("Failed to start audio reader thread for {} track_name={}: {}", key.participant_identity,
                 key.track_name, e.what());
    stream->close();
    return {};
  }
  // NOLINTEND(bugprone-lambda-function-name,bugprone-exception-escape)

  auto& slot = active_readers_[key];
  if (!slot.track) {
    slot.track = track;
  }
  slot.audio_stream = stream;
  slot.video_stream.reset();
  slot.thread = std::move(thread);
  slot.thread_id = slot.thread.get_id();
  slot.exit = std::move(exit);
  slot.track_sid = track->sid();
  slot.track_ended = false;
  LK_LOG_DEBUG(
      "Started audio reader for participant={} track_name={} sid={} "
      "running_readers={}",
      key.participant_identity, key.track_name, slot.track_sid, runningReaderCountLocked());
  return {};
}

std::thread SubscriptionThreadDispatcher::startVideoReaderLocked(const CallbackKey& key,
                                                                 const std::shared_ptr<Track>& track,
                                                                 const RegisteredVideoCallback& callback) {
  LK_LOG_DEBUG("Starting video reader for participant={} track_name={}", key.participant_identity, key.track_name);

  if (runningReaderCountLocked() >= kMaxActiveReaders) {
    LK_LOG_ERROR(
        "Cannot start video reader for {} track_name={}: active reader limit ({}) "
        "reached",
        key.participant_identity, key.track_name, kMaxActiveReaders);
    return {};
  }

  std::shared_ptr<VideoStream> stream;
  try {
    stream = VideoStream::fromTrack(track, callback.options);
  } catch (const std::exception& e) {
    LK_LOG_ERROR("Failed to create VideoStream for {} track_name={}: {}", key.participant_identity, key.track_name,
                 e.what());
    return {};
  }
  if (!stream) {
    LK_LOG_ERROR("Failed to create VideoStream for {} track_name={}", key.participant_identity, key.track_name);
    return {};
  }

  auto exit = std::make_shared<ReaderExit>();
  auto legacy_cb = callback.legacy_callback;
  auto event_cb = callback.event_callback;
  const std::string participant_identity = key.participant_identity;
  const std::string track_name = key.track_name;
  std::thread thread;
  // NOLINTBEGIN(bugprone-lambda-function-name,bugprone-exception-escape)
  // Mirrors the audio reader: outer try/catch contains escapes from
  // stream->read, LK_LOG, etc. Residual diagnostic from spdlog's own
  // formatter is an unrelated logger-fault path and is suppressed.
  //
  // Deliberately captures no `this`: see beginDrainLocked.
  try {
    thread = std::thread([stream, legacy_cb, event_cb, participant_identity, track_name, exit]() mutable {
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
      // Release the stream and the callback copies on this thread, then signal
      // exit so waiters know they are gone.
      stream.reset();
      legacy_cb = nullptr;
      event_cb = nullptr;
      exit->signal();
    });
  } catch (const std::system_error& e) {
    LK_LOG_ERROR("Failed to start video reader thread for {} track_name={}: {}", key.participant_identity,
                 key.track_name, e.what());
    stream->close();
    return {};
  }
  // NOLINTEND(bugprone-lambda-function-name,bugprone-exception-escape)

  auto& slot = active_readers_[key];
  if (!slot.track) {
    slot.track = track;
  }
  slot.video_stream = stream;
  slot.audio_stream.reset();
  slot.thread = std::move(thread);
  slot.thread_id = slot.thread.get_id();
  slot.exit = std::move(exit);
  slot.track_sid = track->sid();
  slot.track_ended = false;
  LK_LOG_DEBUG(
      "Started video reader for participant={} track_name={} sid={} "
      "running_readers={}",
      key.participant_identity, key.track_name, slot.track_sid, runningReaderCountLocked());
  return {};
}

// -------------------------------------------------------------------
// Data track reader helpers
// -------------------------------------------------------------------

std::thread SubscriptionThreadDispatcher::extractDataReaderThreadLocked(DataFrameCallbackId id) {
  auto it = active_data_readers_.find(id);
  if (it == active_data_readers_.end()) {
    return {};
  }
  auto reader = it->second;
  if (!reader) {
    active_data_readers_.erase(it);
    return {};
  }
  // Mark cancelled before closing so a subscribe() still in flight aborts.
  reader->cancelled = true;
  {
    const std::scoped_lock<std::mutex> guard(reader->sub_mutex);
    if (reader->stream) {
      reader->stream->close();
    }
  }
  if (reader->thread.joinable() && isSelfThread(reader->thread_id)) {
    // Removal from inside the reader's own callback. Detach instead of
    // self-joining and keep the entry so stopAll()/reap can wait for its exit.
    // The stream is already closed, so the reader exits as soon as the
    // callback returns.
    LK_LOG_WARN(
        "Data reader id={} is being removed from inside its own callback; "
        "detaching it instead of self-joining. Removing a data callback from "
        "within itself is discouraged",
        id);
    reader->thread.detach();
    reader->detached = true;
    return {};
  }
  if (!reader->thread.joinable() && reader->detached && !reader->finished()) {
    // Previously detached and still running its last callback: keep for reaping.
    return {};
  }
  active_data_readers_.erase(it);
  return std::move(reader->thread);
}

std::thread SubscriptionThreadDispatcher::extractDataReaderThreadLocked(const DataCallbackKey& key) {
  for (const auto& [id, reader] : active_data_readers_) {
    if (reader && reader->remote_track && reader->remote_track->publisherIdentity() == key.participant_identity &&
        reader->remote_track->info().name == key.track_name) {
      // Extracting may erase the entry; we return immediately so the loop
      // never observes the mutated map.
      return extractDataReaderThreadLocked(id);
    }
  }
  return {};
}

std::thread SubscriptionThreadDispatcher::startDataReaderLocked(DataFrameCallbackId id, const DataCallbackKey& key,
                                                                const std::shared_ptr<RemoteDataTrack>& track,
                                                                const DataFrameCallback& cb) {
  std::thread old_thread;
  auto existing = active_data_readers_.find(id);
  if (existing != active_data_readers_.end() && existing->second) {
    auto& reader = existing->second;
    if (reader->thread.joinable() && !reader->finished() && reader->remote_track &&
        reader->remote_track->info().sid == track->info().sid) {
      LK_LOG_DEBUG(
          "Skipping data reader start for \"{}\" track=\"{}\" because a reader for "
          "sid={} is already active",
          key.participant_identity, key.track_name, track->info().sid);
      return {};
    }
    old_thread = extractDataReaderThreadLocked(id);
    auto kept = active_data_readers_.find(id);
    if (kept != active_data_readers_.end()) {
      // A detached reader is still finishing its last callback. Park it under a
      // fresh id so it is still waited for by stopAll() and reaped later, and
      // free this id for the new reader.
      auto parked = kept->second;
      active_data_readers_.erase(kept);
      active_data_readers_[next_data_callback_id_++] = std::move(parked);
    }
  }

  if (runningReaderCountLocked() >= kMaxActiveReaders) {
    LK_LOG_ERROR(
        "Cannot start data reader for {} track={}: active reader "
        "limit ({}) reached",
        key.participant_identity, key.track_name, kMaxActiveReaders);
    return old_thread;
  }

  LK_LOG_INFO("Starting data reader for \"{}\" track=\"{}\"", key.participant_identity, key.track_name);

  auto reader = std::make_shared<ActiveDataReader>();
  reader->remote_track = track;
  reader->exit = std::make_shared<ReaderExit>();
  auto identity = key.participant_identity;
  auto track_name = key.track_name;
  // NOLINTBEGIN(bugprone-lambda-function-name,bugprone-exception-escape)
  // Deliberately captures no `this`: the thread reports its exit through the
  // shared ActiveDataReader only, which is what allows a lifecycle call made
  // from inside the callback to detach it safely.
  try {
    reader->thread = std::thread([reader, track, cb = DataFrameCallback(cb), identity, track_name]() mutable {
      std::shared_ptr<DataTrackStream> stream;
      auto finish = [&]() {
        // Release the stream and callback copy on this thread before signaling.
        stream.reset();
        cb = nullptr;
        markDataReaderFinished(reader);
      };
      try {
        LK_LOG_INFO("Data reader thread: subscribing to \"{}\" track=\"{}\"", identity, track_name);
        auto subscribe_result = track->subscribe();
        if (!subscribe_result) {
          const auto& error = subscribe_result.error();
          LK_LOG_ERROR(
              "Failed to subscribe to data track \"{}\" from \"{}\": code={} "
              "message={}",
              track_name, identity, static_cast<std::uint32_t>(error.code), error.message);
          finish();
          return;
        }
        stream = subscribe_result.value();
        LK_LOG_INFO("Data reader thread: subscribed to \"{}\" track=\"{}\"", identity, track_name);

        bool cancelled = false;
        {
          const std::scoped_lock<std::mutex> guard(reader->sub_mutex);
          // A replacement or teardown may have cancelled this reader while the
          // subscribe was in flight. Close the fresh stream so we do not leave
          // a second live subscription behind.
          if (reader->cancelled.load()) {
            cancelled = true;
            stream->close();
          } else {
            reader->stream = stream;
          }
        }
        if (cancelled) {
          LK_LOG_INFO("Data reader thread: cancelled while subscribing to \"{}\" track=\"{}\"", identity, track_name);
          finish();
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
        LK_LOG_INFO("Data reader thread exiting for \"{}\" track=\"{}\"", identity, track_name);
      } catch (const std::exception& e) {
        LK_LOG_ERROR("Data reader thread terminating due to exception: {}", e.what());
      } catch (...) {
        LK_LOG_ERROR("Data reader thread terminating due to unknown exception");
      }
      // Whether the stream ended on its own (server EOS) or was closed by an
      // extract/teardown, record that this reader is done so a slot still
      // holding it is treated as replaceable.
      finish();
    });
  } catch (const std::system_error& e) {
    LK_LOG_ERROR("Failed to start data reader thread for \"{}\" track=\"{}\": {}", key.participant_identity,
                 key.track_name, e.what());
    return old_thread;
  }
  // NOLINTEND(bugprone-lambda-function-name,bugprone-exception-escape)
  reader->thread_id = reader->thread.get_id();
  active_data_readers_[id] = reader;
  return old_thread;
}

} // namespace livekit
