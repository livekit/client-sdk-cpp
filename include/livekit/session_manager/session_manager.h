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

/// @file session_manager.h
/// @brief High-level SessionManager API for the LiveKit C++ SDK.

#pragma once

#include "livekit/lk_log.h"
#include "livekit/session_manager/managed_local_audio_track.h"
#include "livekit/session_manager/managed_local_video_track.h"
#include "livekit/session_manager/rpc_constants.h"

#include "livekit/local_participant.h"
#include "livekit/room.h"
#include "livekit/rpc_error.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace livekit {

class Room;
class AudioFrame;
class VideoFrame;
class AudioStream;
class VideoStream;
class Track;
enum class TrackSource;

class SessionManagerRoomDelegate;
class RpcController;

namespace test {
class CallbackKeyTest;
class SessionManagerTest;
} // namespace test

/// Callback type for incoming audio frames.
/// Called on a background reader thread.
using AudioFrameCallback = std::function<void(const livekit::AudioFrame &)>;

/// Callback type for incoming video frames.
/// Called on a background reader thread.
/// @param frame        The decoded video frame (RGBA by default).
/// @param timestamp_us Presentation timestamp in microseconds.
using VideoFrameCallback = std::function<void(const livekit::VideoFrame &frame,
                                              std::int64_t timestamp_us)>;

/**
 * High-level SessionManager to the LiveKit C++ SDK.
 *
 * Owns the full room lifecycle: initialize SDK, create Room, connect,
 * publish tracks, and manage incoming frame callbacks.
 *
 * The SessionManager retains a shared_ptr to every track it creates. On
 * disconnect(), all tracks are released (unpublished) before the room
 * is torn down, guaranteeing safe teardown order. To unpublish a track
 * mid-session, call release() on the track explicitly; dropping the
 * application's shared_ptr alone is not sufficient.
 *
 * Example:
 *
 *   SessionManager manager;
 *   livekit::RoomOptions options;
 *   options.auto_subscribe = true;
 *   manager.connect("wss://my-server.livekit.cloud", my_token, options);
 *
 *   auto mic = manager.createAudioTrack("mic", 48000, 2,
 *       livekit::TrackSource::SOURCE_MICROPHONE);
 *   auto cam = manager.createVideoTrack("cam", 1280, 720,
 *       livekit::TrackSource::SOURCE_CAMERA);
 *
 *   mic->pushFrame(pcm_data, samples_per_channel);
 *   cam->pushFrame(rgba_data, timestamp_us);
 *
 *   manager.setOnAudioFrameCallback("remote-participant",
 *       livekit::TrackSource::SOURCE_MICROPHONE,
 *       [](const livekit::AudioFrame& f) { process(f); });
 *
 *   manager.setOnVideoFrameCallback("remote-participant",
 *       livekit::TrackSource::SOURCE_CAMERA,
 *       [](const livekit::VideoFrame& f, int64_t ts) { render(f); });
 *
 *   // Unpublish a single track mid-session:
 *   mic->release();
 *
 *   // Disconnect releases all remaining tracks and tears down the room:
 *   manager.disconnect();
 */
class SessionManager {
public:
  SessionManager();
  ~SessionManager();

  // Non-copyable, non-movable (owns threads, callbacks, room)
  SessionManager(const SessionManager &) = delete;
  SessionManager &operator=(const SessionManager &) = delete;
  SessionManager(SessionManager &&) = delete;
  SessionManager &operator=(SessionManager &&) = delete;

  // ---------------------------------------------------------------
  // Connection
  // ---------------------------------------------------------------

  /**
   * Connect to a LiveKit room.
   *
   * Initializes the SDK (if not already), creates a Room, and performs
   * the WebSocket handshake. This call **blocks** until the connection
   * succeeds or fails. auto_subscribe is enabled so that remote tracks
   * are subscribed automatically.
   *
   * If the SessionManager is already connected, returns true immediately.
   * If another thread is already in the process of connecting, returns
   * false without blocking.
   *
   * @param url    WebSocket URL of the LiveKit server.
   * @param token  Access token for authentication.
   * @param options Room options.

   * @return true if connection succeeded (or was already connected).
   */
  bool connect(const std::string &url, const std::string &token,
               const livekit::RoomOptions &options);

  /**
   * Disconnect from the room and release all resources.
   *
   * All published tracks are unpublished, all reader threads are joined,
   * and the SDK is shut down. Safe to call multiple times.
   */
  void disconnect();

  /// Whether the SessionManager is currently connected to a room.
  bool isConnected() const;

  /**
   * Get the underlying Room for direct base SDK access.
   *
   * Use this to access room_info(), remoteParticipants(),
   * registerTextStreamHandler, registerByteStreamHandler, e2eeManager(), etc.
   *
   * @return Non-null pointer to the Room while connected; nullptr otherwise.
   *
   * @note The pointer is valid only while isConnected() is true. Do not store
   *       the pointer across disconnect/reconnect cycles.
   *
   * @warning Do NOT call setDelegate() on the returned Room — SessionManager
   *          manages the delegate for room lifecycle and track events.
   *
   * @note For publishing audio/video tracks, prefer createAudioTrack() and
   *       createVideoTrack() so SessionManager can manage track lifecycle on
   *       disconnect. Direct publishTrack() via localParticipant() bypasses
   *       that management.
   */
  livekit::Room *getRoom() const;

  // ---------------------------------------------------------------
  // Track creation (publishing)
  // ---------------------------------------------------------------

  /**
   * Create and publish a local audio track.
   *
   * The SessionManager retains a reference to the track internally. To
   * unpublish mid-session, call release() on the returned track. All surviving
   * tracks are automatically released on disconnect().
   *
   * @pre The SessionManager must be connected (via connect()). Calling this on
   * a disconnected SessionManager is a programming error.
   *
   * @param name         Human-readable track name.
   * @param sample_rate  Sample rate in Hz (e.g. 48000).
   * @param num_channels Number of audio channels (1 = mono, 2 = stereo).
   * @param source       Track source type (e.g. SOURCE_MICROPHONE). Use a
   *                     different source (e.g. SOURCE_SCREENSHARE_AUDIO) to
   *                     publish multiple audio tracks from the same
   *                     participant that can be independently subscribed to.
   * @return Shared pointer to the published audio track handle (never null).
   * @throws std::runtime_error if the SessionManager is not connected.
   */
  std::shared_ptr<ManagedLocalAudioTrack>
  createAudioTrack(const std::string &name, int sample_rate, int num_channels,
                   livekit::TrackSource source);

  /**
   * Create and publish a local video track.
   *
   * The SessionManager retains a reference to the track internally. To
   * unpublish mid-session, call release() on the returned track. All surviving
   * tracks are automatically released on disconnect().
   *
   * @pre The SessionManager must be connected (via connect()). Calling this on
   * a disconnected SessionManager is a programming error.
   *
   * @param name   Human-readable track name.
   * @param width  Video width in pixels.
   * @param height Video height in pixels.
   * @param source Track source type (default: SOURCE_CAMERA). Use a
   *               different source (e.g. SOURCE_SCREENSHARE) to publish
   *               multiple video tracks from the same participant that
   *               can be independently subscribed to.
   * @return Shared pointer to the published video track handle (never null).
   * @throws std::runtime_error if the SessionManager is not connected.
   */
  std::shared_ptr<ManagedLocalVideoTrack>
  createVideoTrack(const std::string &name, int width, int height,
                   livekit::TrackSource source);

  // ---------------------------------------------------------------
  // Incoming frame callbacks
  // ---------------------------------------------------------------

  /**
   * Set the callback for audio frames from a specific remote participant
   * and track source.
   *
   * The callback fires on a background thread whenever a new audio frame
   * is received. If the remote participant has not yet connected, the
   * callback is stored and auto-wired when the participant's track is
   * subscribed.
   *
   * @note Only **one** callback may be registered per (participant, source)
   *       pair. Calling this again with the same identity and source will
   *       silently replace the previous callback.
   *
   * @param participant_identity  Identity of the remote participant.
   * @param source                Track source (e.g. SOURCE_MICROPHONE).
   * @param callback              Function to invoke per audio frame.
   */
  void setOnAudioFrameCallback(const std::string &participant_identity,
                               livekit::TrackSource source,
                               AudioFrameCallback callback);

  /**
   * Register a callback for video frames from a specific remote participant
   * and track source.
   *
   * @note Only **one** callback may be registered per (participant, source)
   *       pair. Calling this again with the same identity and source will
   *       silently replace the previous callback.
   *
   * @param participant_identity  Identity of the remote participant.
   * @param source                Track source (e.g. SOURCE_CAMERA).
   * @param callback              Function to invoke per video frame.
   */
  void setOnVideoFrameCallback(const std::string &participant_identity,
                               livekit::TrackSource source,
                               VideoFrameCallback callback);

  /**
   * Clear the audio frame callback for a specific remote participant + track
   * source.
   *
   * If a reader thread is active for this (identity, source), it is
   * stopped and joined.
   */
  void clearOnAudioFrameCallback(const std::string &participant_identity,
                                 livekit::TrackSource source);

  /**
   * Clear the video frame callback for a specific remote participant + track
   * source.
   *
   * If a reader thread is active for this (identity, source), it is
   * stopped and joined.
   */
  void clearOnVideoFrameCallback(const std::string &participant_identity,
                                 livekit::TrackSource source);

  // ---------------------------------------------------------------
  // RPC (Remote Procedure Call)
  // ---------------------------------------------------------------

  /**
   * Initiate a blocking RPC call to a remote participant.
   *
   * Sends a request to the participant identified by
   * @p destination_identity and blocks until a response is received
   * or the call times out.
   *
   * @param destination_identity  Identity of the remote participant.
   * @param method                Name of the RPC method to invoke.
   * @param payload               Request payload string.
   * @param response_timeout      Optional timeout in seconds. If not set,
   *                              the server default (15 s) is used.
   * @return The response payload returned by the remote handler. nullptr if the
   * RPC call fails, or the SessionManager is not connected.
   */
  std::optional<std::string>
  performRpc(const std::string &destination_identity, const std::string &method,
             const std::string &payload,
             const std::optional<double> &response_timeout = std::nullopt);

  /**
   * Register a handler for incoming RPC method invocations.
   *
   * When a remote participant calls the given @p method_name on this
   * participant, the SessionManager invokes @p handler. The handler may return
   * an optional response payload or throw a @c livekit::RpcError to
   * signal failure to the caller.
   *
   * If a handler is already registered for @p method_name, it is
   * silently replaced.
   *
   * @param method_name  Name of the RPC method to handle.
   * @param handler      Callback invoked on each incoming invocation.
   * @return true if the RPC method was registered successfully.
   */
  bool registerRpcMethod(const std::string &method_name,
                         livekit::LocalParticipant::RpcHandler handler);

  /**
   * Unregister a previously registered RPC method handler.
   *
   * After this call, invocations for @p method_name result in an
   * "unsupported method" error being returned to the remote caller.
   * If no handler is registered for this name, the call is a no-op.
   *
   * @param method_name  Name of the RPC method to unregister.
   * @return true if the RPC method was unregistered successfully.
   */
  bool unregisterRpcMethod(const std::string &method_name);

  // ---------------------------------------------------------------
  // Remote Track Control (via RPC)
  // ---------------------------------------------------------------

  /**
   * Request a remote participant to mute a published track.
   *
   * The remote participant must be a SessionManager instance (which
   * automatically registers the built-in track-control RPC handler).
   *
   * @param destination_identity  Identity of the remote participant.
   * @param track_name            Name of the track to mute.
   * @return true if the track was muted successfully.
   */
  bool requestRemoteTrackMute(const std::string &destination_identity,
                              const std::string &track_name);

  /**
   * Request a remote participant to unmute a published track.
   *
   * The remote participant must be a SessionManager instance (which
   * automatically registers the built-in track-control RPC handler).
   *
   * @param destination_identity  Identity of the remote participant.
   * @param track_name            Name of the track to unmute.
   * @return true if the track was unmuted successfully.
   */
  bool requestRemoteTrackUnmute(const std::string &destination_identity,
                                const std::string &track_name);

private:
  friend class SessionManagerRoomDelegate;
  friend class test::CallbackKeyTest;
  friend class test::SessionManagerTest;

  /// Composite key for the callback map: (participant_identity, source).
  /// Only one callback can exist per key -- re-registering overwrites.
  struct CallbackKey {
    std::string identity;
    livekit::TrackSource source;

    bool operator==(const CallbackKey &o) const;
  };

  struct CallbackKeyHash {
    std::size_t operator()(const CallbackKey &k) const;
  };

  /// Active reader thread + stream for an incoming track.
  struct ActiveReader {
    std::shared_ptr<livekit::AudioStream> audio_stream;
    std::shared_ptr<livekit::VideoStream> video_stream;
    std::thread thread;
    bool is_audio = false;
  };

  /// Called by SessionManagerRoomDelegate when a remote track is subscribed.
  void onTrackSubscribed(const std::string &participant_identity,
                         livekit::TrackSource source,
                         const std::shared_ptr<livekit::Track> &track);

  /// Called by SessionManagerRoomDelegate when a remote track is unsubscribed.
  void onTrackUnsubscribed(const std::string &participant_identity,
                           livekit::TrackSource source);

  /// Extract the thread for the given callback key.
  /// @pre Caller must hold @c mutex_.
  std::thread extractReaderThread(const CallbackKey &key);

  /// Start a reader thread for a subscribed track.
  /// @return The reader thread for this track.
  /// @pre Caller must hold @c mutex_.
  std::thread startAudioReader(const CallbackKey &key,
                               const std::shared_ptr<livekit::Track> &track,
                               AudioFrameCallback cb);
  /// @copydoc startAudioReader
  std::thread startVideoReader(const CallbackKey &key,
                               const std::shared_ptr<livekit::Track> &track,
                               VideoFrameCallback cb);

  /// Execute a track action (mute/unmute) by track name.
  /// Used as the TrackActionFn callback for RpcController.
  /// Throws livekit::RpcError if the track is not found.
  /// @pre Caller does NOT hold mutex_ (acquires it internally).
  void executeTrackAction(const rpc::track_control::Action &action,
                          const std::string &track_name);

  mutable std::mutex mutex_;
  bool connected_;
  bool connecting_; // guards against concurrent connect() calls
  bool sdk_initialized_;

  static constexpr int kMaxActiveReaders = 20;

  std::unique_ptr<livekit::Room> room_;
  std::unique_ptr<SessionManagerRoomDelegate> delegate_;
  std::unique_ptr<RpcController> rpc_controller_;

  /// Registered callbacks (may be registered before tracks are subscribed).
  std::unordered_map<CallbackKey, AudioFrameCallback, CallbackKeyHash>
      audio_callbacks_;
  /// @copydoc audio_callbacks_
  std::unordered_map<CallbackKey, VideoFrameCallback, CallbackKeyHash>
      video_callbacks_;

  /// Active reader threads for subscribed tracks.
  std::unordered_map<CallbackKey, ActiveReader, CallbackKeyHash>
      active_readers_;

  /// All tracks created by this SessionManager. The SessionManager retains a
  /// shared_ptr so it can force-release every track on disconnect() before the
  /// room is destroyed, preventing dangling @c participant_ pointers.
  std::vector<std::shared_ptr<ManagedLocalAudioTrack>> published_audio_tracks_;
  /// @copydoc published_audio_tracks_
  std::vector<std::shared_ptr<ManagedLocalVideoTrack>> published_video_tracks_;
};

} // namespace livekit
