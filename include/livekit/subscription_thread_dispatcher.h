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
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

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
/// This type is intentionally independent from @ref RoomDelegate. High-level
/// room events such as `RoomDelegate::onTrackSubscribed()` remain in @ref Room,
/// while this dispatcher focuses only on callback registration, stream
/// ownership, and reader-thread lifecycle.
///
/// The design keeps track-type-specific startup isolated so additional track
/// kinds can be added later without pushing more thread state back into
/// @ref Room.
///
/// @deprecated Prefer @ref Room's `setOn*FrameCallback` / `clearOn*FrameCallback`
/// / `addOnDataFrameCallback` / `removeOnDataFrameCallback` methods, which
/// delegate to this class internally. Direct use of this class is deprecated
/// and it may be removed, or its API may change, in a future major version.
class LIVEKIT_DEPRECATED(
    "SubscriptionThreadDispatcher is deprecated; use Room::setOnAudioFrameCallback / "
    "setOnVideoFrameCallback / setOnVideoFrameEventCallback / addOnDataFrameCallback instead. "
    "It may be removed in a future major version.") LIVEKIT_API SubscriptionThreadDispatcher {
public:
  /// Constructs an empty dispatcher with no registered callbacks or readers.
  SubscriptionThreadDispatcher();

  /// Stops all active readers and clears all registered callbacks.
  ~SubscriptionThreadDispatcher();

  /// Register or replace an audio frame callback for a remote subscription.
  ///
  /// @warning This call blocks until any in-flight invocation of the previous
  ///          callback returns. Calling this from inside a frame callback for the same key is not supported.
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
  /// @warning This call blocks until any in-flight invocation of the previous
  ///          callback returns. Calling this from inside a frame callback for the same key is not supported.
  /// @note this shares its registration slot with @ref setOnVideoFrameEventCallback -- registering either one
  // replaces the other for the same key.
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
  /// @warning This call blocks until any in-flight invocation of the previous
  ///          callback returns. Calling this from inside a frame callback for the same key is not supported.
  /// @note this shares its registration slot with @ref setOnVideoFrameCallback -- registering either one replaces the
  // other for the same key.
  //
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
  /// closed and the thread is joined before this call returns.
  ///
  /// @warning Calling this from inside a frame callback for the same key is not supported.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to clear.
  void clearOnAudioFrameCallback(const std::string& participant_identity, const std::string& track_name);

  /// Remove a video callback registration and stop any active reader.
  ///
  /// If a video reader thread is active for the given key, its stream is
  /// closed and the thread is joined before this call returns.
  ///
  /// @warning Calling this from inside a frame callback for the same key is not supported.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to clear.
  void clearOnVideoFrameCallback(const std::string& participant_identity, const std::string& track_name);

  /// Start or restart reader dispatch for a newly subscribed remote audio or
  /// video track.
  ///
  /// A repeated event for the track SID a reader is already serving is a no-op;
  //  A different SID (a republish) stops and joins the previous reader before starting the new one.
  ///
  /// The dispatcher retains the subscription until it receives
  /// @ref handleTrackUnsubscribed. This lets a callback registered after this
  /// method returns start a reader immediately. A reader only starts for  audio and video tracks.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name associated with the subscription.
  /// @param track                Subscribed remote audio or video track to read from.
  void handleTrackSubscribed(const std::string& participant_identity, const std::string& track_name,
                             const std::shared_ptr<Track>& track);

  /// Stop reader dispatch for an unsubscribed remote track.
  ///
  /// @ref Room calls this when a remote track is unsubscribed. Any active
  /// reader stream for the given `(participant, track_name)` key is closed and its
  /// thread is joined. Callback registration is preserved so future
  /// re-subscription can start dispatch again automatically.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param source               Track source associated with the subscription.
  /// @param track_name           Track name associated with the subscription.
  void handleTrackUnsubscribed(const std::string& participant_identity, TrackSource source,
                               const std::string& track_name);

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
  /// @warning This call blocks until any in-flight invocation of the previous
  ///          callback returns. Calling this from inside a frame callback for the same key is not supported.
  ///
  /// @param id  The identifier returned by addOnDataFrameCallback().
  void removeOnDataFrameCallback(DataFrameCallbackId id);

  /// Notify the dispatcher that a remote data track has been published.
  ///
  /// @ref Room calls this when it receives a kDataTrackPublished event.
  /// For every registered callback whose (participant, track_name) matches,
  /// a reader thread is launched.
  ///
  /// @param track The newly published remote data track.
  void handleDataTrackPublished(const std::shared_ptr<RemoteDataTrack>& track);

  /// Notify the dispatcher that a remote data track has been unpublished.
  ///
  /// @ref Room calls this when it receives a kDataTrackUnpublished event.
  /// Any active data reader threads for this track SID are closed and joined.
  ///
  /// @param sid The SID of the unpublished data track.
  void handleDataTrackUnpublished(const std::string& sid);

  /// Stop all readers and clear all callback registrations.
  ///
  /// This is used during room teardown or EOS handling to ensure no reader thread survives beyond the lifetime of the
  /// owning @ref Room If called from inside a frame callback the calling reader is detached rather than self-joined.
  void stopAll();

private:
  friend class SubscriptionThreadDispatcherTest;
  friend struct RoomTestAccess;

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

  /// Active read-side resources for one audio/video subscription dispatch slot.
  struct ActiveReader {
    std::shared_ptr<AudioStream> audio_stream;
    std::shared_ptr<VideoStream> video_stream;
    std::thread thread;
    /// SID of the subscribed track backing this reader
    std::string track_sid;
    /// ID of @ref thread, captured at construction. Used to block a self-join.
    std::thread::id thread_id;
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
    /// Set true when this reader is being replaced or torn down.
    std::atomic<bool> cancelled{false};
    /// Set true by the reader thread itself when it exits (failed, cancelled, or terminal subscription). Only
    /// dispatcher lifecycle paths erase the slot and join or detach the thread.
    std::atomic<bool> finished{false};
    std::mutex sub_mutex;
    std::shared_ptr<DataTrackStream> stream; // guarded by sub_mutex
    std::thread thread;
    /// ID of @ref thread, captured at construction. Used to block a self-join.
    std::thread::id thread_id;
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

  /// Remove and close the active reader for @p key, returning its thread.
  ///
  /// Must be called with @ref lock_ held. The returned thread, if joinable,
  /// must be disposed of after releasing the lock.
  std::thread extractReaderThreadLocked(const CallbackKey& key);

  /// Wrapper around @ref extractReaderThreadLocked. If extractReaderThreadLocked returns a thread the key is marked as
  /// draining.
  ///
  /// Must be called with @ref lock_ held.
  std::thread extractReaderForDrainLocked(const CallbackKey& key);

  /// Dispose of the old reader thread, clear from the drain, and start a new reader.
  ///
  /// Must be called with @ref lock_ released.
  void finishReaderDrainAndRestart(const CallbackKey& key, std::thread old_thread, const char* operation);

  /// True when @p id identifies the calling thread, i.e. joining that thread
  /// would be a self-join.
  static bool isSelfThread(std::thread::id id) { return id == std::this_thread::get_id(); }

  /// Dispose of an extracted reader thread (audio, video, or data).
  /// @param thread The thread to dispose of. If this is a self thread, detach and return.
  /// @param operation for logging
  /// Must be called with @ref lock_ released.
  void disposeReaderThread(std::thread&& thread, const char* operation);

  /// Starts the respective media reader thread for @p track.
  ///
  /// This is a no-op if: no callback is registered for the track's kind, the track is not audio or video, or a read is
  /// active for the key Must be called with @ref lock_ held.
  void startReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track);

  /// Start a reader for @p key if one should be running: a subscribed track is
  /// retained for the key, no reader is active, and the key is not draining.
  ///
  /// Must be called with @ref lock_ held.
  void startReaderForSubscribedTrackLocked(const CallbackKey& key);

  /// Start an audio reader thread for @p key using @p track.
  ///
  /// Must be called with @ref lock_ held and with no reader active for @p key.
  void startAudioReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track, const AudioFrameCallback& cb,
                              const AudioStream::Options& opts);

  /// Start a video reader thread for @p key using @p track.
  ///
  /// Must be called with @ref lock_ held and with no reader active for @p key.
  void startVideoReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track,
                              const RegisteredVideoCallback& callback);

  /// Extract and close the data reader for a given callback ID, returning its
  /// thread.  Marks the reader cancelled so a subscription still in flight is
  /// aborted.  Must be called with @ref lock_ held; the returned thread must be
  /// passed to @ref disposeReaderThread after releasing the lock.
  std::thread extractDataReaderThreadLocked(DataFrameCallbackId id);

  /// Start a data reader thread for the given callback ID, key, and track.
  /// Must be called with @ref lock_ held.
  std::thread startDataReaderLocked(DataFrameCallbackId id, const DataCallbackKey& key,
                                    const std::shared_ptr<RemoteDataTrack>& track, const DataFrameCallback& cb);

  /// Mark @p reader finished and release its stream.
  /// Reader threads must not self join.
  /// @param reader The reader to mark as finished.
  static void markDataReaderFinished(const std::shared_ptr<ActiveDataReader>& reader);

  /// Protects callback registration maps and active reader state.
  mutable std::mutex lock_;

  /// Registered audio frame callbacks keyed by @ref CallbackKey.
  std::unordered_map<CallbackKey, RegisteredAudioCallback, CallbackKeyHash> audio_callbacks_;

  /// Registered video frame callbacks keyed by @ref CallbackKey.
  std::unordered_map<CallbackKey, RegisteredVideoCallback, CallbackKeyHash> video_callbacks_;

  /// Active stream/thread state keyed by @ref CallbackKey.
  std::unordered_map<CallbackKey, ActiveReader, CallbackKeyHash> active_readers_;

  /// Currently subscribed remote audio/video tracks keyed by @ref CallbackKey.
  std::unordered_map<CallbackKey, std::shared_ptr<Track>, CallbackKeyHash> subscribed_tracks_;

  /// Keys whose previous reader has been extracted but not yet joined. A reader is not started for a key while
  /// it has an entry here. See @ref extractReaderForDrainLocked.
  std::unordered_map<CallbackKey, int, CallbackKeyHash> draining_readers_;

  /// Next auto-increment ID for data frame callbacks.
  DataFrameCallbackId next_data_callback_id_{0};

  /// Registered data frame callbacks keyed by opaque callback ID.
  std::unordered_map<DataFrameCallbackId, RegisteredDataCallback> data_callbacks_;

  /// Active data reader threads keyed by callback ID.
  std::unordered_map<DataFrameCallbackId, std::shared_ptr<ActiveDataReader>> active_data_readers_;

  /// Currently published remote data tracks, keyed by (participant, name).
  std::unordered_map<DataCallbackKey, std::shared_ptr<RemoteDataTrack>, DataCallbackKeyHash> remote_data_tracks_;

  /// Hard limit on concurrently active per-subscription reader threads.
  static constexpr int kMaxActiveReaders = 20;
};

} // namespace livekit
