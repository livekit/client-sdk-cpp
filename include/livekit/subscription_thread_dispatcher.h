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
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "livekit/audio_stream.h"
#include "livekit/video_stream.h"
#include "livekit/visibility.h"

namespace livekit {

class AudioFrame;
class DataTrackStream;
class RemoteDataTrack;
class Track;
class VideoFrame;

/// Callback type for incoming audio frames.
/// Invoked on a dedicated reader thread per (participant, track_name) pair.
using AudioFrameCallback = std::function<void(const AudioFrame&)>;

/// Callback type for incoming video frames.
/// Invoked on a dedicated reader thread per (participant, track_name) pair.
using VideoFrameCallback = std::function<void(const VideoFrame& frame, std::int64_t timestamp_us)>;

/// Callback type for incoming video frame events.
/// Invoked on a dedicated reader thread per (participant, track_name) pair.
using VideoFrameEventCallback = std::function<void(const VideoFrameEvent&)>;

/// Callback type for incoming data track frames.
/// Invoked on a dedicated reader thread per subscription.
/// @param payload        Raw binary data received.
/// @param user_timestamp Optional application-defined timestamp from sender.
using DataFrameCallback =
    std::function<void(const std::vector<std::uint8_t>& payload, std::optional<std::uint64_t> user_timestamp)>;

/// Opaque identifier returned by addOnDataFrameCallback, used to remove an
/// individual subscription via removeOnDataFrameCallback.
using DataFrameCallbackId = std::uint64_t;

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
class LIVEKIT_API SubscriptionThreadDispatcher {
public:
  /// Constructs an empty dispatcher with no registered callbacks or readers.
  SubscriptionThreadDispatcher();

  /// Stops all active readers and clears all registered callbacks.
  ~SubscriptionThreadDispatcher();

  /// Register an audio frame callback for a remote subscription.
  ///
  /// The callback is keyed by remote participant identity plus @p track_name.
  /// If the matching remote audio track is already subscribed, @ref Room may
  /// immediately call @ref handleTrackSubscribed to start a reader.
  ///
  /// Registration only succeeds when no reader is currently active for the
  /// key. To replace a callback whose reader is already running, call
  /// @ref clearOnAudioFrameCallback first, then register again.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to match.
  /// @param callback             Function invoked for each decoded audio frame.
  /// @param opts                 Options used when creating the backing
  ///                             @ref AudioStream.
  /// @return @c true if the callback was registered; @c false if a reader is
  ///         already active for the key (the registration is left unchanged).
  [[nodiscard]] bool trySetOnAudioFrameCallback(const std::string& participant_identity, const std::string& track_name,
                                                AudioFrameCallback callback, const AudioStream::Options& opts = {});

  /// Register a video frame callback for a remote subscription.
  ///
  /// The callback is keyed by remote participant identity plus @p track_name.
  /// If the matching remote video track is already subscribed, @ref Room may
  /// immediately call @ref handleTrackSubscribed to start a reader.
  ///
  /// Registration only succeeds when no reader is currently active for the
  /// key. To replace a callback whose reader is already running, call
  /// @ref clearOnVideoFrameCallback first, then register again.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to match.
  /// @param callback             Function invoked for each decoded video frame.
  /// @param opts                 Options used when creating the backing
  ///                             @ref VideoStream.
  /// @return @c true if the callback was registered; @c false if a reader is
  ///         already active for the key (the registration is left unchanged).
  [[nodiscard]] bool trySetOnVideoFrameCallback(const std::string& participant_identity, const std::string& track_name,
                                                VideoFrameCallback callback, const VideoStream::Options& opts = {});

  /// Register a rich video frame event callback for a remote subscription.
  ///
  /// The callback is keyed by remote participant identity plus @p track_name.
  /// If the matching remote video track is already subscribed, @ref Room may
  /// immediately call @ref handleTrackSubscribed to start a reader.
  ///
  /// Registration only succeeds when no reader is currently active for the
  /// key. To replace a callback whose reader is already running, call
  /// @ref clearOnVideoFrameCallback first, then register again.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to match.
  /// @param callback             Function invoked for each decoded video frame
  ///                             event, including optional metadata.
  /// @param opts                 Options used when creating the backing
  ///                             @ref VideoStream.
  /// @return @c true if the callback was registered; @c false if a reader is
  ///         already active for the key (the registration is left unchanged).
  [[nodiscard]] bool trySetOnVideoFrameEventCallback(const std::string& participant_identity,
                                                     const std::string& track_name, VideoFrameEventCallback callback,
                                                     const VideoStream::Options& opts = {});

  /// @deprecated Use trySetOnAudioFrameCallback() instead.
  ///
  /// Forwards to @ref trySetOnAudioFrameCallback and discards the result.
  /// Replacing an active callback is not supported through this overload; call
  /// @ref clearOnAudioFrameCallback first, then @ref trySetOnAudioFrameCallback.
  [[deprecated(
      "SubscriptionThreadDispatcher::setOnAudioFrameCallback is deprecated; use trySetOnAudioFrameCallback instead")]]
  void setOnAudioFrameCallback(const std::string& participant_identity, const std::string& track_name,
                               AudioFrameCallback callback, const AudioStream::Options& opts = {});

  /// @deprecated Use trySetOnVideoFrameCallback() instead.
  ///
  /// Forwards to @ref trySetOnVideoFrameCallback and discards the result.
  /// Replacing an active callback is not supported through this overload; call
  /// @ref clearOnVideoFrameCallback first, then @ref trySetOnVideoFrameCallback.
  [[deprecated(
      "SubscriptionThreadDispatcher::setOnVideoFrameCallback is deprecated; use trySetOnVideoFrameCallback instead")]]
  void setOnVideoFrameCallback(const std::string& participant_identity, const std::string& track_name,
                               VideoFrameCallback callback, const VideoStream::Options& opts = {});

  /// @deprecated Use trySetOnVideoFrameEventCallback() instead.
  ///
  /// Forwards to @ref trySetOnVideoFrameEventCallback and discards the result.
  /// Replacing an active callback is not supported through this overload; call
  /// @ref clearOnVideoFrameCallback first, then
  /// @ref trySetOnVideoFrameEventCallback.
  [[deprecated(
      "SubscriptionThreadDispatcher::setOnVideoFrameEventCallback is deprecated; use "
      "trySetOnVideoFrameEventCallback instead")]]
  void setOnVideoFrameEventCallback(const std::string& participant_identity, const std::string& track_name,
                                    VideoFrameEventCallback callback, const VideoStream::Options& opts = {});

  /// Remove an audio callback registration and stop any active reader.
  ///
  /// If an audio reader thread is active for the given key, its stream is
  /// closed and the thread is joined before this call returns. Call this
  /// before @ref trySetOnAudioFrameCallback to replace an active callback.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to clear.
  void clearOnAudioFrameCallback(const std::string& participant_identity, const std::string& track_name);

  /// Remove a video callback registration and stop any active reader.
  ///
  /// If a video reader thread is active for the given key, its stream is
  /// closed and the thread is joined before this call returns. Call this
  /// before @ref trySetOnVideoFrameCallback (or
  /// @ref trySetOnVideoFrameEventCallback) to replace an active callback.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name to clear.
  void clearOnVideoFrameCallback(const std::string& participant_identity, const std::string& track_name);

  /// Start or restart reader dispatch for a newly subscribed remote audio or
  /// video track.
  ///
  /// @ref Room calls this after it has processed a track-subscription event and
  /// updated its publication state. If a matching audio or video callback
  /// registration exists, the dispatcher creates the appropriate @ref
  /// AudioStream or @ref VideoStream and launches a reader thread for the
  /// `(participant, track_name)` key.
  ///
  /// Remote data tracks are handled separately via @ref handleDataTrackPublished.
  /// If @p track is not audio or video, or no matching callback is registered,
  /// this is a no-op.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param track_name           Track name associated with the subscription.
  /// @param track                Subscribed remote audio or video track to read from.
  void handleTrackSubscribed(const std::string& participant_identity, const std::string& track_name,
                             const std::shared_ptr<Track>& track);

  /// Stop reader dispatch for an unsubscribed remote audio or video track.
  ///
  /// @ref Room calls this when a remote audio or video track is unsubscribed.
  /// Any active reader stream for the given `(participant, track_name)` key is
  /// closed and its thread is joined. Callback registration is preserved so
  /// future re-subscription can start dispatch again automatically.
  ///
  /// Remote data tracks are handled separately via @ref handleDataTrackUnpublished.
  ///
  /// @param participant_identity Identity of the remote participant.
  /// @param source               Track source associated with the subscription.
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
  /// This is used during room teardown or EOS handling to ensure no reader
  /// thread survives beyond the lifetime of the owning @ref Room.
  void stopAll();

private:
  friend class SubscriptionThreadDispatcherTest;

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
    /// SID of the subscribed track backing this reader, used to skip redundant
    /// reader restarts when the same publication is re-subscribed.
    std::string track_sid;
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
    /// Set true when this reader is being replaced or torn down so the reader
    /// thread can abort a subscription that is still in flight.
    std::atomic<bool> cancelled{false};
    /// Guarded by lock_. Reader threads may mark themselves finished, but only
    /// dispatcher lifecycle paths erase the slot and join the thread.
    bool finished = false;
    std::mutex sub_mutex;
    std::shared_ptr<DataTrackStream> stream; // guarded by sub_mutex
    std::thread thread;
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
  /// must be joined after releasing the lock.
  std::thread extractReaderThreadLocked(const CallbackKey& key);

  /// Select the appropriate reader startup path for @p media track.
  ///
  /// This is called by @ref Room when a remote track is subscribed. If a reader for the same track SID is already
  /// active, startup is skipped and a default-constructed thread is returned; otherwise any previous reader is
  /// extracted and returned to the caller for joining outside the lock.
  ///
  /// Must be called with @ref lock_ held.
  std::thread startReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track);

  /// Start an audio reader thread for @p key using @p track.
  ///
  /// Must be called with @ref lock_ held. Any previous reader for the same key
  /// is extracted and returned to the caller for joining outside the lock.
  std::thread startAudioReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track,
                                     const AudioFrameCallback& cb, const AudioStream::Options& opts);

  /// Start a video reader thread for @p key using @p track.
  ///
  /// Must be called with @ref lock_ held. Any previous reader for the same key
  /// is extracted and returned to the caller for joining outside the lock.
  std::thread startVideoReaderLocked(const CallbackKey& key, const std::shared_ptr<Track>& track,
                                     const RegisteredVideoCallback& callback);

  /// Extract and close the data reader for a given callback ID, returning its
  /// thread.  Marks the reader cancelled so a subscription still in flight is
  /// aborted.  Must be called with @ref lock_ held.
  std::thread extractDataReaderThreadLocked(DataFrameCallbackId id);

  /// Start a data reader thread for the given callback ID, key, and track.
  /// Must be called with @ref lock_ held.
  std::thread startDataReaderLocked(DataFrameCallbackId id, const DataCallbackKey& key,
                                    const std::shared_ptr<RemoteDataTrack>& track, const DataFrameCallback& cb);

  /// Mark @p reader finished if the slot for @p id still refers to it.
  /// Called by the reader thread itself when it exits after a failed,
  /// cancelled, or terminal subscription.  Acquires @ref lock_. Reader threads
  /// must not erase, detach, or join their own @ref std::thread.
  void markDataReaderFinishedIfCurrent(DataFrameCallbackId id, const std::shared_ptr<ActiveDataReader>& reader);

  /// Protects callback registration maps and active reader state.
  mutable std::mutex lock_;

  /// Registered audio frame callbacks keyed by @ref CallbackKey.
  std::unordered_map<CallbackKey, RegisteredAudioCallback, CallbackKeyHash> audio_callbacks_;

  /// Registered video frame callbacks keyed by @ref CallbackKey.
  std::unordered_map<CallbackKey, RegisteredVideoCallback, CallbackKeyHash> video_callbacks_;

  /// Active stream/thread state keyed by @ref CallbackKey.
  std::unordered_map<CallbackKey, ActiveReader, CallbackKeyHash> active_readers_;

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
