/*
 * Copyright 2026 LiveKit
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

#ifndef LIVEKIT_SUBSCRIPTION_THREAD_DISPATCHER_H
#define LIVEKIT_SUBSCRIPTION_THREAD_DISPATCHER_H

#include "livekit/audio_stream.h"
#include "livekit/video_stream.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace livekit {

class AudioFrame;
class DataTrackSubscription;
class RemoteDataTrack;
class Track;
class VideoFrame;

/// Callback type for incoming audio frames.
/// Invoked on a dedicated reader thread per (participant, track_name) pair.
using AudioFrameCallback = std::function<void(const AudioFrame &)>;

/// Callback type for incoming video frames.
/// Invoked on a dedicated reader thread per (participant, track_name) pair.
using VideoFrameCallback =
    std::function<void(const VideoFrame &frame, std::int64_t timestamp_us)>;

/// Callback type for incoming data track frames.
/// Invoked on a dedicated reader thread per subscription.
/// @param payload        Raw binary data received.
/// @param user_timestamp Optional application-defined timestamp from sender.
using DataFrameCallback =
    std::function<void(const std::vector<std::uint8_t> &payload,
                       std::optional<std::uint64_t> user_timestamp)>;

/// Opaque identifier returned by addOnDataFrameCallback, used to remove an
/// individual subscription via removeOnDataFrameCallback.
using DataFrameCallbackId = std::uint64_t;

/**
 * Owns subscription callback registration and per-subscription reader threads.
 *
 * `SubscriptionThreadDispatcher` is the low-level companion to \ref Room's
 * remote track subscription flow. `Room` forwards user-facing callback
 * registration requests here, and then calls \ref handleTrackSubscribed and
 * \ref handleTrackUnsubscribed as room events arrive.
 *
 * For each registered `(participant identity, track_name)` pair, this class
 * may create a dedicated \ref AudioStream or \ref VideoStream and a matching
 * reader thread. That thread blocks on stream reads and invokes the
 * registered callback with decoded frames.
 *
 * This type is intentionally independent from \ref RoomDelegate. High-level
 * room events such as `RoomDelegate::onTrackSubscribed()` remain in \ref Room,
 * while this dispatcher focuses only on callback registration, stream
 * ownership, and reader-thread lifecycle.
 *
 * The design keeps track-type-specific startup isolated so additional track
 * kinds can be added later without pushing more thread state back into
 * \ref Room.
 */
class SubscriptionThreadDispatcher {
public:
  /// Constructs an empty dispatcher with no registered callbacks or readers.
  SubscriptionThreadDispatcher();

  /// Stops all active readers and clears all registered callbacks.
  ~SubscriptionThreadDispatcher();

  /**
   * Register or replace an audio frame callback for a remote subscription.
   *
   * The callback is keyed by remote participant identity plus \p track_name.
   * If the matching remote audio track is already subscribed, \ref Room may
   * immediately call \ref handleTrackSubscribed to start a reader.
   *
   * @param participant_identity Identity of the remote participant.
   * @param track_name           Track name to match.
   * @param callback             Function invoked for each decoded audio frame.
   * @param opts                 Options used when creating the backing
   *                             \ref AudioStream.
   */
  void setOnAudioFrameCallback(const std::string &participant_identity,
                               const std::string &track_name,
                               AudioFrameCallback callback,
                               AudioStream::Options opts = {});

  /**
   * Register or replace a video frame callback for a remote subscription.
   *
   * The callback is keyed by remote participant identity plus \p track_name.
   * If the matching remote video track is already subscribed, \ref Room may
   * immediately call \ref handleTrackSubscribed to start a reader.
   *
   * @param participant_identity Identity of the remote participant.
   * @param track_name           Track name to match.
   * @param callback             Function invoked for each decoded video frame.
   * @param opts                 Options used when creating the backing
   *                             \ref VideoStream.
   */
  void setOnVideoFrameCallback(const std::string &participant_identity,
                               const std::string &track_name,
                               VideoFrameCallback callback,
                               VideoStream::Options opts = {});

  /**
   * Remove an audio callback registration and stop any active reader.
   *
   * If an audio reader thread is active for the given key, its stream is
   * closed and the thread is joined before this call returns.
   *
   * @param participant_identity Identity of the remote participant.
   * @param track_name           Track name to clear.
   */
  void clearOnAudioFrameCallback(const std::string &participant_identity,
                                 const std::string &track_name);

  /**
   * Remove a video callback registration and stop any active reader.
   *
   * If a video reader thread is active for the given key, its stream is
   * closed and the thread is joined before this call returns.
   *
   * @param participant_identity Identity of the remote participant.
   * @param track_name           Track name to clear.
   */
  void clearOnVideoFrameCallback(const std::string &participant_identity,
                                 const std::string &track_name);

  /**
   * Start or restart reader dispatch for a newly subscribed remote track.
   *
   * \ref Room calls this after it has processed a track-subscription event and
   * updated its publication state. If a matching callback registration exists,
   * the dispatcher creates the appropriate stream type and launches a reader
   * thread for the `(participant, track_name)` key.
   *
   * If no matching callback is registered, this is a no-op.
   *
   * @param participant_identity Identity of the remote participant.
   * @param track_name           Track name associated with the subscription.
   * @param track                Subscribed remote track to read from.
   */
  void handleTrackSubscribed(const std::string &participant_identity,
                             const std::string &track_name,
                             const std::shared_ptr<Track> &track);

  /**
   * Stop reader dispatch for an unsubscribed remote track.
   *
   * \ref Room calls this when a remote track is unsubscribed. Any active
   * reader stream for the given `(participant, track_name)` key is closed and
   * its
   * thread is joined. Callback registration is preserved so future
   * re-subscription can start dispatch again automatically.
   *
   * @param participant_identity Identity of the remote participant.
   * @param track_name           Track name associated with the subscription.
   */
  void handleTrackUnsubscribed(const std::string &participant_identity,
                               const std::string &track_name);

  // ---------------------------------------------------------------
  // Data track callbacks
  // ---------------------------------------------------------------

  /**
   * Add a callback for data frames from a specific remote participant's
   * data track.
   *
   * Multiple callbacks may be registered for the same (participant,
   * track_name) pair; each one creates an independent FFI subscription.
   *
   * The callback fires on a dedicated background thread. If the remote
   * data track has not yet been published, the callback is stored and
   * auto-wired when the track appears (via handleDataTrackPublished).
   *
   * @param participant_identity  Identity of the remote participant.
   * @param track_name            Name of the remote data track.
   * @param callback              Function to invoke per data frame.
   * @return An opaque ID that can later be passed to
   *         removeOnDataFrameCallback() to tear down this subscription.
   */
  DataFrameCallbackId
  addOnDataFrameCallback(const std::string &participant_identity,
                         const std::string &track_name,
                         DataFrameCallback callback);

  /**
   * Remove a data frame callback previously registered via
   * addOnDataFrameCallback(). Stops and joins the active reader thread
   * for this subscription.
   * No-op if the ID is not (or no longer) registered.
   *
   * @param id  The identifier returned by addOnDataFrameCallback().
   */
  void removeOnDataFrameCallback(DataFrameCallbackId id);

  /**
   * Notify the dispatcher that a remote data track has been published.
   *
   * \ref Room calls this when it receives a kDataTrackPublished event.
   * For every registered callback whose (participant, track_name) matches,
   * a reader thread is launched.
   *
   * @param track The newly published remote data track.
   */
  void handleDataTrackPublished(const std::shared_ptr<RemoteDataTrack> &track);

  /**
   * Notify the dispatcher that a remote data track has been unpublished.
   *
   * \ref Room calls this when it receives a kDataTrackUnpublished event.
   * Any active data reader threads for this track SID are closed and joined.
   *
   * @param sid The SID of the unpublished data track.
   */
  void handleDataTrackUnpublished(const std::string &sid);

  /**
   * Stop all readers and clear all callback registrations.
   *
   * This is used during room teardown or EOS handling to ensure no reader
   * thread survives beyond the lifetime of the owning \ref Room.
   */
  void stopAll();

private:
  friend class SubscriptionThreadDispatcherTest;

  /// Compound lookup key for a remote participant identity and track name.
  struct CallbackKey {
    std::string participant_identity;
    std::string track_name;

    bool operator==(const CallbackKey &o) const {
      return participant_identity == o.participant_identity &&
             track_name == o.track_name;
    }
  };

  /// Hash function for \ref CallbackKey so it can be used in unordered maps.
  struct CallbackKeyHash {
    std::size_t operator()(const CallbackKey &k) const {
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
  };

  /// Compound lookup key for a remote participant identity and data track name.
  struct DataCallbackKey {
    std::string participant_identity;
    std::string track_name;

    bool operator==(const DataCallbackKey &o) const {
      return participant_identity == o.participant_identity &&
             track_name == o.track_name;
    }
  };

  /// Hash function for \ref DataCallbackKey.
  struct DataCallbackKeyHash {
    std::size_t operator()(const DataCallbackKey &k) const {
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

  /// Active read-side resources for one data track subscription.
  struct ActiveDataReader {
    std::shared_ptr<RemoteDataTrack> remote_track;
    std::mutex sub_mutex;
    std::shared_ptr<DataTrackSubscription> subscription; // guarded by sub_mutex
    std::thread thread;
  };

  /// Stored audio callback registration plus stream-construction options.
  struct RegisteredAudioCallback {
    AudioFrameCallback callback;
    AudioStream::Options options;
  };

  /// Stored video callback registration plus stream-construction options.
  struct RegisteredVideoCallback {
    VideoFrameCallback callback;
    VideoStream::Options options;
  };

  /// Remove and close the active reader for \p key, returning its thread.
  ///
  /// Must be called with \ref lock_ held. The returned thread, if joinable,
  /// must be joined after releasing the lock.
  std::thread extractReaderThreadLocked(const CallbackKey &key);

  /// Select the appropriate reader startup path for \p track.
  ///
  /// Must be called with \ref lock_ held.
  std::thread startReaderLocked(const CallbackKey &key,
                                const std::shared_ptr<Track> &track);

  /// Start an audio reader thread for \p key using \p track.
  ///
  /// Must be called with \ref lock_ held. Any previous reader for the same key
  /// is extracted and returned to the caller for joining outside the lock.
  std::thread startAudioReaderLocked(const CallbackKey &key,
                                     const std::shared_ptr<Track> &track,
                                     AudioFrameCallback cb,
                                     const AudioStream::Options &opts);

  /// Start a video reader thread for \p key using \p track.
  ///
  /// Must be called with \ref lock_ held. Any previous reader for the same key
  /// is extracted and returned to the caller for joining outside the lock.
  std::thread startVideoReaderLocked(const CallbackKey &key,
                                     const std::shared_ptr<Track> &track,
                                     VideoFrameCallback cb,
                                     const VideoStream::Options &opts);

  /// Extract and close the data reader for a given callback ID, returning its
  /// thread.  Must be called with \ref lock_ held.
  std::thread extractDataReaderThreadLocked(DataFrameCallbackId id);

  /// Extract and close the data reader for a given (participant, track_name)
  /// key, returning its thread.  Must be called with \ref lock_ held.
  std::thread extractDataReaderThreadLocked(const DataCallbackKey &key);

  /// Start a data reader thread for the given callback ID, key, and track.
  /// Must be called with \ref lock_ held.
  std::thread
  startDataReaderLocked(DataFrameCallbackId id, const DataCallbackKey &key,
                        const std::shared_ptr<RemoteDataTrack> &track,
                        DataFrameCallback cb);

  /// Protects callback registration maps and active reader state.
  mutable std::mutex lock_;

  /// Registered audio frame callbacks keyed by `(participant, track_name)`.
  std::unordered_map<CallbackKey, RegisteredAudioCallback, CallbackKeyHash>
      audio_callbacks_;

  /// Registered video frame callbacks keyed by `(participant, track_name)`.
  std::unordered_map<CallbackKey, RegisteredVideoCallback, CallbackKeyHash>
      video_callbacks_;

  /// Active stream/thread state keyed by `(participant, track_name)`.
  std::unordered_map<CallbackKey, ActiveReader, CallbackKeyHash>
      active_readers_;

  /// Next auto-increment ID for data frame callbacks.
  DataFrameCallbackId next_data_callback_id_;

  /// Registered data frame callbacks keyed by opaque callback ID.
  std::unordered_map<DataFrameCallbackId, RegisteredDataCallback>
      data_callbacks_;

  /// Active data reader threads keyed by callback ID.
  std::unordered_map<DataFrameCallbackId, std::shared_ptr<ActiveDataReader>>
      active_data_readers_;

  /// Currently published remote data tracks, keyed by (participant, name).
  std::unordered_map<DataCallbackKey, std::shared_ptr<RemoteDataTrack>,
                     DataCallbackKeyHash>
      remote_data_tracks_;

  /// Hard limit on concurrently active per-subscription reader threads.
  static constexpr int kMaxActiveReaders = 20;
};

} // namespace livekit

#endif /* LIVEKIT_SUBSCRIPTION_THREAD_DISPATCHER_H */
