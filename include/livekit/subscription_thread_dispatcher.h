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

#ifndef LIVEKIT_SUBSCRIPTION_THREAD_DISPATCHER_H
#define LIVEKIT_SUBSCRIPTION_THREAD_DISPATCHER_H

#include "livekit/audio_stream.h"
#include "livekit/video_stream.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace livekit {

class AudioFrame;
class Track;
class VideoFrame;

/// Callback type for incoming audio frames.
/// Invoked on a dedicated reader thread per (participant, source) pair.
using AudioFrameCallback = std::function<void(const AudioFrame &)>;

/// Callback type for incoming video frames.
/// Invoked on a dedicated reader thread per (participant, source) pair.
using VideoFrameCallback =
    std::function<void(const VideoFrame &frame, std::int64_t timestamp_us)>;

/**
 * Owns subscription callback registration and per-subscription reader threads.
 *
 * `SubscriptionThreadDispatcher` is the low-level companion to \ref Room's
 * remote track subscription flow. `Room` forwards user-facing callback
 * registration requests here, and then calls \ref handleTrackSubscribed and
 * \ref handleTrackUnsubscribed as room events arrive.
 *
 * For each registered `(participant identity, TrackSource)` pair, this class
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
   * The callback is keyed by remote participant identity plus \p source.
   * If the matching remote audio track is already subscribed, \ref Room may
   * immediately call \ref handleTrackSubscribed to start a reader.
   *
   * @param participant_identity Identity of the remote participant.
   * @param source               Track source to match.
   * @param callback             Function invoked for each decoded audio frame.
   * @param opts                 Options used when creating the backing
   *                             \ref AudioStream.
   */
  void setOnAudioFrameCallback(const std::string &participant_identity,
                               TrackSource source, AudioFrameCallback callback,
                               AudioStream::Options opts = {});

  /**
   * Register or replace a video frame callback for a remote subscription.
   *
   * The callback is keyed by remote participant identity plus \p source.
   * If the matching remote video track is already subscribed, \ref Room may
   * immediately call \ref handleTrackSubscribed to start a reader.
   *
   * @param participant_identity Identity of the remote participant.
   * @param source               Track source to match.
   * @param callback             Function invoked for each decoded video frame.
   * @param opts                 Options used when creating the backing
   *                             \ref VideoStream.
   */
  void setOnVideoFrameCallback(const std::string &participant_identity,
                               TrackSource source, VideoFrameCallback callback,
                               VideoStream::Options opts = {});

  /**
   * Remove an audio callback registration and stop any active reader.
   *
   * If an audio reader thread is active for the given key, its stream is
   * closed and the thread is joined before this call returns.
   *
   * @param participant_identity Identity of the remote participant.
   * @param source               Track source to clear.
   */
  void clearOnAudioFrameCallback(const std::string &participant_identity,
                                 TrackSource source);

  /**
   * Remove a video callback registration and stop any active reader.
   *
   * If a video reader thread is active for the given key, its stream is
   * closed and the thread is joined before this call returns.
   *
   * @param participant_identity Identity of the remote participant.
   * @param source               Track source to clear.
   */
  void clearOnVideoFrameCallback(const std::string &participant_identity,
                                 TrackSource source);

  /**
   * Start or restart reader dispatch for a newly subscribed remote track.
   *
   * \ref Room calls this after it has processed a track-subscription event and
   * updated its publication state. If a matching callback registration exists,
   * the dispatcher creates the appropriate stream type and launches a reader
   * thread for the `(participant, source)` key.
   *
   * If no matching callback is registered, this is a no-op.
   *
   * @param participant_identity Identity of the remote participant.
   * @param source               Track source associated with the subscription.
   * @param track                Subscribed remote track to read from.
   */
  void handleTrackSubscribed(const std::string &participant_identity,
                             TrackSource source,
                             const std::shared_ptr<Track> &track);

  /**
   * Stop reader dispatch for an unsubscribed remote track.
   *
   * \ref Room calls this when a remote track is unsubscribed. Any active
   * reader stream for the given `(participant, source)` key is closed and its
   * thread is joined. Callback registration is preserved so future
   * re-subscription can start dispatch again automatically.
   *
   * @param participant_identity Identity of the remote participant.
   * @param source               Track source associated with the subscription.
   */
  void handleTrackUnsubscribed(const std::string &participant_identity,
                               TrackSource source);

  /**
   * Stop all readers and clear all callback registrations.
   *
   * This is used during room teardown or EOS handling to ensure no reader
   * thread survives beyond the lifetime of the owning \ref Room.
   */
  void stopAll();

private:
  friend class SubscriptionThreadDispatcherTest;

  /// Compound lookup key for a remote participant identity and track source.
  struct CallbackKey {
    std::string participant_identity;
    TrackSource source;

    bool operator==(const CallbackKey &o) const {
      return participant_identity == o.participant_identity &&
             source == o.source;
    }
  };

  /// Hash function for \ref CallbackKey so it can be used in unordered maps.
  struct CallbackKeyHash {
    std::size_t operator()(const CallbackKey &k) const {
      auto h1 = std::hash<std::string>{}(k.participant_identity);
      auto h2 = std::hash<int>{}(static_cast<int>(k.source));
      return h1 ^ (h2 << 1);
    }
  };

  /// Active read-side resources for one subscription dispatch slot.
  struct ActiveReader {
    std::shared_ptr<AudioStream> audio_stream;
    std::shared_ptr<VideoStream> video_stream;
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

  /// Protects callback registration maps and active reader state.
  mutable std::mutex lock_;

  /// Registered audio frame callbacks keyed by `(participant, source)`.
  std::unordered_map<CallbackKey, RegisteredAudioCallback, CallbackKeyHash>
      audio_callbacks_;

  /// Registered video frame callbacks keyed by `(participant, source)`.
  std::unordered_map<CallbackKey, RegisteredVideoCallback, CallbackKeyHash>
      video_callbacks_;

  /// Active stream/thread state keyed by `(participant, source)`.
  std::unordered_map<CallbackKey, ActiveReader, CallbackKeyHash>
      active_readers_;

  /// Hard limit on concurrently active per-subscription reader threads.
  static constexpr int kMaxActiveReaders = 20;
};

} // namespace livekit

#endif /* LIVEKIT_SUBSCRIPTION_THREAD_DISPATCHER_H */
