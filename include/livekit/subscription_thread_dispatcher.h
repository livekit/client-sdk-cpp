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

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "livekit/audio_stream.h"
#include "livekit/frame_callbacks.h"
#include "livekit/video_stream.h"
#include "livekit/visibility.h"

namespace livekit {

class AudioFrame;
class DataTrackStream;
class RemoteDataTrack;
class Track;
class VideoFrame;

/// Owns subscription callback registration and per-subscription reader threads.
///
/// `SubscriptionThreadDispatcher` is the low-level companion to @ref Room's
/// remote track subscription flow. `Room` forwards user-facing callback
/// registration requests here. For remote audio and video subscriptions it
/// calls @ref handleTrackSubscribed and @ref handleTrackUnsubscribed; for
/// data tracks it calls @ref handleDataTrackPublished and
/// @ref handleDataTrackUnpublished.
///
/// For each registered audio or video `(participant identity, track name)`
/// pair, this class may create a dedicated @ref AudioStream or @ref
/// VideoStream and a matching reader thread. That thread blocks on stream
/// reads and invokes the registered callback with decoded frames.
///
/// The dispatcher retains every subscribed audio/video track from
/// @ref handleTrackSubscribed until @ref handleTrackUnsubscribed. A callback
/// may therefore be registered before or after the subscription event: it
/// starts a reader immediately when the track is already subscribed and
/// otherwise as soon as the subscription arrives. Replacing a callback stops
/// the previous reader, joins it, and starts a new one bound to the new
/// callback, so the old callback is never invoked again once the setter
/// returns.
///
/// When a stream ends on its own (the FFI layer reports end-of-stream or an
/// error) the reader thread exits and is reaped by the next lifecycle call;
/// the retained subscription is kept so a later re-subscribe or explicit
/// re-registration can start a fresh reader.
///
/// Threading contract: the lifecycle methods block while joining reader
/// threads (see the per-method notes). Frame callbacks must not block waiting
/// on room events, and lifecycle methods should not be called from inside the
/// frame callback of the same subscription; when that happens anyway the SDK
/// detaches that reader instead of self-joining and the change still takes
/// effect.
///
/// This type is intentionally independent from @ref RoomDelegate. High-level
/// room events such as `RoomDelegate::onTrackSubscribed()` remain in @ref Room,
/// while this dispatcher focuses only on callback registration, stream
/// ownership, and reader-thread lifecycle.
///
/// @deprecated Direct use of this class is deprecated. Use
/// `Room::setOnAudioFrameCallback`, `Room::setOnVideoFrameCallback`,
/// `Room::setOnVideoFrameEventCallback`, `Room::clearOnAudioFrameCallback`,
/// `Room::clearOnVideoFrameCallback`, `Room::addOnDataFrameCallback` and
/// `Room::removeOnDataFrameCallback` instead; they delegate to this class. It
/// may be removed, or its interface may change, in a future major version.
class LIVEKIT_DEPRECATED(
    "SubscriptionThreadDispatcher is deprecated for direct use; use "
    "Room::setOnAudioFrameCallback / setOnVideoFrameCallback / "
    "setOnVideoFrameEventCallback / addOnDataFrameCallback instead. It may be "
    "removed in a future major version.") LIVEKIT_API SubscriptionThreadDispatcher {
public:
  /// Constructs an empty dispatcher with no registered callbacks or readers.
  SubscriptionThreadDispatcher();

  /// Stops all active readers and clears all registered callbacks.
  ///
  /// Equivalent to @ref stopAll. Blocks until every reader thread has exited.
  ~SubscriptionThreadDispatcher();

  /// Register or replace an audio frame callback for a remote subscription.
  ///
  /// The callback is keyed by remote participant identity plus @p track_name.
  /// If the matching remote audio track is already subscribed (the dispatcher
  /// has seen @ref handleTrackSubscribed for the key) a reader starts before
  /// this call returns; otherwise it starts when the subscription arrives.
  ///
  /// @warning **Blocking.** If a reader is already running for this key it is
  ///          stopped and joined first, so this call returns only after any
  ///          in-flight invocation of the previous callback has returned and
  ///          the previous callback object has been destroyed. A callback that
  ///          never returns blocks registration indefinitely. Calling this from
  ///          inside the frame callback of the same key is discouraged: the SDK
  ///          logs a warning, detaches that reader, and still installs the
  ///          replacement, but the no-overlap guarantee does not hold for that
  ///          one in-flight invocation.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to match.
  /// @param callback             Function invoked for each decoded audio frame.
  /// @param opts                 Options used when creating the backing
  ///                             @ref AudioStream.
  void setOnAudioFrameCallback(const std::string& participant_identity, const std::string& track_name,
                               AudioFrameCallback callback, const AudioStream::Options& opts = {});

  /// Register or replace a video frame callback for a remote subscription.
  ///
  /// The callback is keyed by remote participant identity plus @p track_name.
  /// If the matching remote video track is already subscribed a reader starts
  /// before this call returns; otherwise it starts when the subscription
  /// arrives.
  ///
  /// @note This shares its registration slot with
  ///       @ref setOnVideoFrameEventCallback: registering either one replaces
  ///       the other for the same key.
  /// @warning **Blocking.** See @ref setOnAudioFrameCallback for the join and
  ///          re-entrancy contract; it applies identically here.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to match.
  /// @param callback             Function invoked for each decoded video frame.
  /// @param opts                 Options used when creating the backing
  ///                             @ref VideoStream.
  void setOnVideoFrameCallback(const std::string& participant_identity, const std::string& track_name,
                               VideoFrameCallback callback, const VideoStream::Options& opts = {});

  /// Register or replace a rich video frame event callback for a remote
  /// subscription.
  ///
  /// The callback is keyed by remote participant identity plus @p track_name.
  /// If the matching remote video track is already subscribed a reader starts
  /// before this call returns; otherwise it starts when the subscription
  /// arrives.
  ///
  /// @note This shares its registration slot with @ref setOnVideoFrameCallback:
  ///       registering either one replaces the other for the same key.
  /// @warning **Blocking.** See @ref setOnAudioFrameCallback for the join and
  ///          re-entrancy contract; it applies identically here.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to match.
  /// @param callback             Function invoked for each decoded video frame
  ///                             event, including optional metadata.
  /// @param opts                 Options used when creating the backing
  ///                             @ref VideoStream.
  void setOnVideoFrameEventCallback(const std::string& participant_identity, const std::string& track_name,
                                    VideoFrameEventCallback callback, const VideoStream::Options& opts = {});

  /// Remove an audio callback registration and stop any active reader.
  ///
  /// If an audio reader thread is active for the given key, its stream is
  /// closed and the thread is joined before this call returns. The subscription
  /// itself stays retained, so a later registration starts a new reader.
  ///
  /// @warning **Blocking.** Returns only after any in-flight invocation of the
  ///          callback has returned. Calling this from inside the frame
  ///          callback of the same key detaches that reader instead of
  ///          self-joining (a warning is logged).
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to clear.
  void clearOnAudioFrameCallback(const std::string& participant_identity, const std::string& track_name);

  /// Remove a video callback registration and stop any active reader.
  ///
  /// If a video reader thread is active for the given key, its stream is
  /// closed and the thread is joined before this call returns. The subscription
  /// itself stays retained, so a later registration starts a new reader.
  ///
  /// @warning **Blocking.** Returns only after any in-flight invocation of the
  ///          callback has returned. Calling this from inside the frame
  ///          callback of the same key detaches that reader instead of
  ///          self-joining (a warning is logged).
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to clear.
  void clearOnVideoFrameCallback(const std::string& participant_identity, const std::string& track_name);

  /// Record a newly subscribed remote audio or video track and start reader
  /// dispatch for it if a callback is registered.
  ///
  /// @ref Room calls this after it has processed a track-subscription event and
  /// updated its publication state. The dispatcher retains @p track for the
  /// `(participant, track_name)` key until @ref handleTrackUnsubscribed, so a
  /// callback registered later starts a reader immediately.
  ///
  /// A repeated event for the track SID a reader is already serving is a
  /// no-op. A different SID under the same name (a republish) stops and joins
  /// the previous reader before starting a new one on @p track.
  ///
  /// @warning **Blocking** when it has to replace a reader (see above);
  ///          otherwise it returns without joining anything.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name associated with the subscription.
  /// @param track                Subscribed remote track to read from.
  void handleTrackSubscribed(const std::string& participant_identity, const std::string& track_name,
                             const std::shared_ptr<Track>& track);

  /// Stop reader dispatch for an unsubscribed remote track and forget the
  /// retained subscription.
  ///
  /// @ref Room calls this when a remote track is unsubscribed. Any active
  /// reader for the `(participant, track_name)` key is closed and its thread is
  /// joined. Callback registration is preserved so future re-subscription can
  /// start dispatch again automatically.
  ///
  /// @warning **Blocking** while a reader is joined.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param source               Track source associated with the subscription
  ///                             (informational; only logged).
  /// @param track_name           Track name associated with the subscription.
  void handleTrackUnsubscribed(const std::string& participant_identity, TrackSource source,
                               const std::string& track_name);

  // ---------------------------------------------------------------
  // Data track callbacks
  // ---------------------------------------------------------------

  /// Add a callback for data frames from a specific remote participant's
  /// data track.
  ///
  /// Multiple callbacks may be registered for the same (participant,
  /// track_name) pair; each one creates an independent FFI subscription.
  ///
  /// The callback fires on a dedicated background thread. If the remote
  /// data track has not yet been published, the callback is stored and
  /// auto-wired when the track appears (via handleDataTrackPublished).
  ///
  /// @param participant_identity  Identity of the remote participant.
  /// @param track_name            Name of the remote data track.
  /// @param callback              Function to invoke per data frame.
  /// @return An opaque ID that can later be passed to
  ///         removeOnDataFrameCallback() to tear down this subscription.
  DataFrameCallbackId addOnDataFrameCallback(const std::string& participant_identity, const std::string& track_name,
                                             DataFrameCallback callback);

  /// Remove a data frame callback previously registered via
  /// addOnDataFrameCallback(). Stops and joins the active reader thread
  /// for this subscription.
  /// No-op if the ID is not (or no longer) registered.
  ///
  /// @warning **Blocking.** Returns only after any in-flight invocation of the
  ///          callback has returned. Calling this from inside that very data
  ///          callback detaches the reader instead of self-joining (a warning
  ///          is logged); delivery still stops once the callback returns.
  ///
  /// @param id  The identifier returned by addOnDataFrameCallback().
  void removeOnDataFrameCallback(DataFrameCallbackId id);

  /// Notify the dispatcher that a remote data track has been published.
  ///
  /// @ref Room calls this when it receives a kDataTrackPublished event.
  /// For every registered callback whose (participant, track_name) matches,
  /// a reader thread is launched. A reader already serving the same track SID
  /// is left alone; a reader for a previous publication under the same name is
  /// stopped and joined before its replacement starts.
  ///
  /// @warning **Blocking** while previous readers are joined.
  ///
  /// @param track The newly published remote data track.
  void handleDataTrackPublished(const std::shared_ptr<RemoteDataTrack>& track);

  /// Notify the dispatcher that a remote data track has been unpublished.
  ///
  /// @ref Room calls this when it receives a kDataTrackUnpublished event.
  /// Any active data reader threads for this track SID are closed and joined.
  ///
  /// @warning **Blocking** while readers are joined.
  ///
  /// @param sid The SID of the unpublished data track.
  void handleDataTrackUnpublished(const std::string& sid);

  /// Stop all readers and clear all callback registrations and retained
  /// subscriptions.
  ///
  /// This is used during room teardown or EOS handling to ensure no reader
  /// thread survives beyond the lifetime of the owning @ref Room. Readers being
  /// joined concurrently by another caller are waited for as well, so on return
  /// every reader has finished invoking its callback and released its stream.
  ///
  /// @warning **Blocking.** If called from inside a frame callback the calling
  ///          reader is detached rather than self-joined (a warning is logged).
  void stopAll();

private:
  friend class SubscriptionThreadDispatcherTest;
  friend struct RoomTestAccess;

  // ABI note: this class is exported with its state inline. The data members
  // below must not be added to, removed, reordered or retyped; all per-reader
  // state lives in the heap-allocated map value types instead, and the
  // pre-existing private helper signatures are kept. See the pimpl follow-up.

  /// Compound lookup key for audio/video callback dispatch.
  struct CallbackKey {
    std::string participant_identity;
    std::string track_name;

    bool operator==(const CallbackKey& o) const {
      return participant_identity == o.participant_identity && track_name == o.track_name;
    }
  };

  /// Hash function for @ref CallbackKey so it can be used in unordered maps.
  struct CallbackKeyHash {
    std::size_t operator()(const CallbackKey& k) const {
      auto h1 = std::hash<std::string>{}(k.participant_identity);
      auto h2 = std::hash<std::string>{}(k.track_name);
      return h1 ^ (h2 << 1);
    }
  };

  /// Heap-shared "reader thread has exited" signal.
  ///
  /// Set by the reader lambda as its very last act, after it has released its
  /// stream and callback copies, so a waiter that observes `done` knows the
  /// previous callback object is gone. Shared between the owning slot/entry,
  /// the reader lambda, and any caller waiting on it. Its identity also serves
  /// as the drain token (see @ref DrainHandle).
  struct ReaderExit {
    std::mutex m;
    std::condition_variable cv;
    bool done{false};

    void signal() {
      {
        const std::scoped_lock<std::mutex> lock(m);
        done = true;
      }
      cv.notify_all();
    }
    void wait() {
      std::unique_lock<std::mutex> lock(m);
      cv.wait(lock, [this] { return done; });
    }
    bool isDone() {
      const std::scoped_lock<std::mutex> lock(m);
      return done;
    }
  };

  /// Per-key subscription slot for one audio/video `(participant, track_name)`.
  ///
  /// A slot exists from @ref handleTrackSubscribed until
  /// @ref handleTrackUnsubscribed, whether or not a reader thread is running.
  /// States, all guarded by @ref lock_:
  ///  - idle:      `track` set, `thread` not joinable.
  ///  - running:   `thread` joinable and `exit` not done.
  ///  - finished:  `thread` joinable and `exit` done (ended on its own; reaped
  ///               by the next lifecycle call).
  ///  - draining:  `draining == true`; `thread` was moved out to exactly one
  ///               caller that is joining it outside the lock and will call
  ///               @ref finishDrain.
  struct ActiveReader {
    std::shared_ptr<AudioStream> audio_stream;
    std::shared_ptr<VideoStream> video_stream;
    std::thread thread;
    /// Retained subscribed track; null once unsubscribed.
    std::shared_ptr<Track> track;
    /// SID the current/last reader was started against (duplicate-event dedup).
    std::string track_sid;
    /// ID of `thread`, captured at start. Used to detect self-joins.
    std::thread::id thread_id;
    /// Exit signal of the current/last reader; also the drain token.
    std::shared_ptr<ReaderExit> exit;
    /// True while one caller owns the moved-out thread and is joining it.
    bool draining{false};
    /// The last reader ended on its own (EOS/error). Suppresses automatic
    /// restart until a new subscribe event or an explicit re-registration.
    bool track_ended{false};
    /// Readers that were detached because a lifecycle call was made from
    /// inside their own callback. Waited for by @ref stopAll, pruned once done.
    std::vector<std::pair<std::shared_ptr<ReaderExit>, std::thread::id>> detached;

    bool readerRunning() { return thread.joinable() && exit && !exit->isDone(); }
    bool readerFinished() { return thread.joinable() && exit && exit->isDone(); }
  };

  /// A reader thread extracted from its slot by @ref beginDrainLocked, to be
  /// joined outside @ref lock_ and then completed with @ref finishDrain.
  struct DrainHandle {
    CallbackKey key;
    std::thread thread;
    /// The slot's `exit` at extraction time. @ref finishDrain only clears the
    /// drain if the slot still carries this token, which makes it safe across
    /// a concurrent @ref stopAll plus re-subscribe creating a fresh slot.
    std::shared_ptr<ReaderExit> token;

    bool joinable() const { return thread.joinable(); }
  };

  /// Compound lookup key for a remote participant identity and data track name.
  struct DataCallbackKey {
    std::string participant_identity;
    std::string track_name;

    bool operator==(const DataCallbackKey& o) const {
      return participant_identity == o.participant_identity && track_name == o.track_name;
    }
  };

  /// Hash function for @ref DataCallbackKey.
  struct DataCallbackKeyHash {
    std::size_t operator()(const DataCallbackKey& k) const {
      auto h1 = std::hash<std::string>{}(k.participant_identity);
      auto h2 = std::hash<std::string>{}(k.track_name);
      return h1 ^ (h2 << 1);
    }
  };

  /// Stored data callback registration.
  struct RegisteredDataCallback {
    DataCallbackKey key;
    DataFrameCallback callback;
  };

  /// Active read-side resources for one data track stream subscription.
  struct ActiveDataReader {
    std::shared_ptr<RemoteDataTrack> remote_track;
    std::mutex sub_mutex;
    std::shared_ptr<DataTrackStream> stream; // guarded by sub_mutex
    std::thread thread;
    /// Set (before the stream is closed) when this reader is being replaced or
    /// torn down, so a `subscribe()` still in flight closes its fresh stream
    /// instead of entering the read loop.
    std::atomic<bool> cancelled{false};
    /// Signaled by the reader thread when it exits for any reason.
    std::shared_ptr<ReaderExit> exit;
    /// ID of `thread`, captured at start. Used to detect self-joins.
    std::thread::id thread_id;
    /// The thread was detached because it removed itself from inside its own
    /// callback; the entry is kept until `exit` is done so @ref stopAll can
    /// wait for it. Guarded by @ref lock_.
    bool detached{false};

    bool finished() { return exit && exit->isDone(); }
  };

  /// Stored audio callback registration plus stream-construction options.
  struct RegisteredAudioCallback {
    AudioFrameCallback callback;
    AudioStream::Options options;
  };

  /// Stored video callback registration plus stream-construction options.
  struct RegisteredVideoCallback {
    VideoFrameCallback legacy_callback;
    VideoFrameEventCallback event_callback;
    VideoStream::Options options;
  };

  // -------------------------------------------------------------------
  // Pre-existing private helpers (signatures kept for ABI stability)
  // -------------------------------------------------------------------

  /// Close the streams of the slot for @p key, move its reader thread out and
  /// mark the slot draining. Returns the thread (empty if there was no reader
  /// or the slot is already draining). Does not erase the slot.
  ///
  /// Must be called with @ref lock_ held. Prefer @ref beginDrainLocked, which
  /// also handles the self-thread case and captures the drain token.
  std::thread extractReaderThreadLocked(const CallbackKey& key);

  /// Start the reader appropriate for @p track's kind if a matching callback
  /// is registered for @p key. Precondition: no reader thread for @p key.
  ///
  /// Must be called with @ref lock_ held. Always returns an empty thread.
  std::thread startReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track);

  /// Start an audio reader thread for @p key using @p track.
  ///
  /// Must be called with @ref lock_ held and with no reader thread for @p key.
  /// Stream or thread creation failures are logged and leave the slot idle.
  /// Always returns an empty thread.
  std::thread startAudioReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track,
                                     const AudioFrameCallback& cb, const AudioStream::Options& opts);

  /// Start a video reader thread for @p key using @p track.
  ///
  /// Must be called with @ref lock_ held and with no reader thread for @p key.
  /// Stream or thread creation failures are logged and leave the slot idle.
  /// Always returns an empty thread.
  std::thread startVideoReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track,
                                     const RegisteredVideoCallback& callback);

  /// Cancel and close the data reader for a given callback ID, erase its entry
  /// and return its thread for joining outside the lock. If the caller is that
  /// reader's own thread, the thread is detached, the entry is kept (marked
  /// `detached`) for later reaping, and an empty thread is returned.
  /// Must be called with @ref lock_ held.
  std::thread extractDataReaderThreadLocked(DataFrameCallbackId id);

  /// Same as above for the first data reader matching a (participant,
  /// track_name) key. Must be called with @ref lock_ held.
  std::thread extractDataReaderThreadLocked(const DataCallbackKey& key);

  /// Start a data reader thread for the given callback ID, key, and track.
  /// A live reader already serving the same track SID is left alone; a
  /// finished one, or one for a different SID, is extracted first and its
  /// thread returned for disposal outside the lock.
  /// Must be called with @ref lock_ held.
  std::thread startDataReaderLocked(DataFrameCallbackId id, const DataCallbackKey& key,
                                    const std::shared_ptr<RemoteDataTrack>& track, const DataFrameCallback& cb);

  // -------------------------------------------------------------------
  // Lifecycle helpers
  // -------------------------------------------------------------------

  /// True when @p id identifies the calling thread, i.e. joining that thread
  /// would be a self-join.
  static bool isSelfThread(std::thread::id id) noexcept { return id == std::this_thread::get_id(); }

  /// Join an extracted reader thread. Must be called with @ref lock_ released.
  /// Defensive: if the thread is the calling thread it is detached with a
  /// warning instead (callers normally handle that case in
  /// @ref beginDrainLocked).
  static void disposeReaderThread(std::thread&& thread, const char* operation);

  /// Block until @p exit is signaled, unless @p owner is the calling thread
  /// (which would deadlock; a warning is logged instead). Must be called with
  /// @ref lock_ released.
  static void waitForReaderExit(const std::shared_ptr<ReaderExit>& exit, std::thread::id owner, const char* operation);

  /// Close and release both stream pointers of @p slot.
  static void closeSlotStreams(ActiveReader& slot);

  /// True when @p slot's retained track is of @p kind, i.e. any reader running
  /// in the slot is a reader of that kind.
  static bool slotReaderKindIs(const ActiveReader& slot, TrackKind kind);

  /// Mark @p reader finished: release its stream and signal its exit.
  /// Called by the data reader thread itself; never joins.
  static void markDataReaderFinished(const std::shared_ptr<ActiveDataReader>& reader);

  /// Begin draining the reader of @p key: close its streams, move the thread
  /// out and mark the slot draining. Returns a handle to complete with
  /// @ref finishDrain after releasing @ref lock_. Returns an empty handle when
  /// there is no reader, the slot is already draining, or the caller is the
  /// reader itself (in which case the reader is detached and recorded in
  /// `detached`, and the caller should reconcile immediately).
  ///
  /// Must be called with @ref lock_ held.
  DrainHandle beginDrainLocked(const CallbackKey& key, const char* operation);

  /// Join the drained thread, then re-acquire @ref lock_ and, if the slot still
  /// carries the handle's token, clear `draining` and @ref reconcileLocked.
  ///
  /// Must be called with @ref lock_ released. Blocks on the join.
  void finishDrain(DrainHandle&& handle, const char* operation);

  /// Bring the slot for @p key in line with its registration: start a reader if
  /// the track is retained, no reader exists, the last one did not end on its
  /// own, and a callback is registered for the track's kind; erase the slot if
  /// nothing remains. No-op while the slot is draining or has a reader thread.
  ///
  /// Must be called with @ref lock_ held.
  void reconcileLocked(const CallbackKey& key);

  /// Sweep readers that ended on their own: media slots whose reader finished
  /// are marked `track_ended` and drained into @p media_drains; finished data
  /// readers are erased and their threads appended to @p data_threads; detached
  /// entries whose exit is done are pruned. Callers must dispose the results
  /// outside the lock via @ref disposeReaped.
  ///
  /// Must be called with @ref lock_ held.
  void reapFinishedReadersLocked(std::vector<DrainHandle>& media_drains, std::vector<std::thread>& data_threads);

  /// Join everything collected by @ref reapFinishedReadersLocked and complete
  /// the media drains. Must be called with @ref lock_ released.
  void disposeReaped(std::vector<DrainHandle>&& media_drains, std::vector<std::thread>&& data_threads,
                     const char* operation);

  /// Number of reader threads currently running (media slots with a live
  /// reader plus data readers that have not finished). Threads mid-drain are
  /// not counted. Must be called with @ref lock_ held.
  int runningReaderCountLocked();

  // -------------------------------------------------------------------
  // State (layout frozen; see ABI note above)
  // -------------------------------------------------------------------

  /// Protects callback registration maps and active reader state.
  mutable std::mutex lock_;

  /// Registered audio frame callbacks keyed by @ref CallbackKey.
  std::unordered_map<CallbackKey, RegisteredAudioCallback, CallbackKeyHash> audio_callbacks_;

  /// Registered video frame callbacks keyed by @ref CallbackKey.
  std::unordered_map<CallbackKey, RegisteredVideoCallback, CallbackKeyHash> video_callbacks_;

  /// Subscription slots (retained track plus reader state) keyed by
  /// @ref CallbackKey. One entry per subscribed audio/video track.
  std::unordered_map<CallbackKey, ActiveReader, CallbackKeyHash> active_readers_;

  /// Next auto-increment ID for data frame callbacks.
  DataFrameCallbackId next_data_callback_id_{0};

  /// Registered data frame callbacks keyed by opaque callback ID.
  std::unordered_map<DataFrameCallbackId, RegisteredDataCallback> data_callbacks_;

  /// Active data reader threads keyed by callback ID.
  std::unordered_map<DataFrameCallbackId, std::shared_ptr<ActiveDataReader>> active_data_readers_;

  /// Currently published remote data tracks, keyed by (participant, name).
  std::unordered_map<DataCallbackKey, std::shared_ptr<RemoteDataTrack>, DataCallbackKeyHash> remote_data_tracks_;

  /// Hard limit on concurrently running reader threads, media and data
  /// combined. Threads that are being joined are not counted, so the limit can
  /// be exceeded transiently by the number of concurrent replacements.
  static constexpr int kMaxActiveReaders = 20;
};

} // namespace livekit
