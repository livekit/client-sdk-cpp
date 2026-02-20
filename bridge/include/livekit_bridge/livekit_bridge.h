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

#include "livekit_bridge/bridge_audio_track.h"
#include "livekit_bridge/bridge_video_track.h"

#include "livekit/room.h"

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
} // namespace livekit

namespace livekit_bridge {

class BridgeRoomDelegate;

namespace test {
class CallbackKeyTest;
class LiveKitBridgeTest;
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
 * High-level bridge to the LiveKit C++ SDK.
 *
 * Owns the full room lifecycle: initialize SDK, create Room, connect,
 * publish tracks, and manage incoming frame callbacks.
 *
 * Example:
 *
 *   LiveKitBridge bridge;
 *   livekit::RoomOptions options;
 *   options.auto_subscribe = true;
 *   bridge.connect("wss://my-server.livekit.cloud", my_token, options);
 *
 *   auto mic = bridge.createAudioTrack("mic", 48000, 2,
 *       livekit::TrackSource::SOURCE_MICROPHONE);
 *   auto cam = bridge.createVideoTrack("cam", 1280, 720,
 *       livekit::TrackSource::SOURCE_CAMERA);
 *
 *   mic->pushFrame(pcm_data, samples_per_channel);
 *   cam->pushFrame(rgba_data, timestamp_us);
 *
 *   bridge.setOnAudioFrameCallback("remote-participant",
 *       livekit::TrackSource::SOURCE_MICROPHONE,
 *       [](const livekit::AudioFrame& f) { process(f); });
 *
 *   bridge.setOnVideoFrameCallback("remote-participant",
 *       livekit::TrackSource::SOURCE_CAMERA,
 *       [](const livekit::VideoFrame& f, int64_t ts) { render(f); });
 *
 *   // Cleanup is automatic via RAII, or explicit:
 *   mic.reset();
 *   bridge.disconnect();
 */
class LiveKitBridge {
public:
  LiveKitBridge();
  ~LiveKitBridge();

  // Non-copyable, non-movable (owns threads, callbacks, room)
  LiveKitBridge(const LiveKitBridge &) = delete;
  LiveKitBridge &operator=(const LiveKitBridge &) = delete;
  LiveKitBridge(LiveKitBridge &&) = delete;
  LiveKitBridge &operator=(LiveKitBridge &&) = delete;

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
   * If the bridge is already connected, returns true immediately.
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

  /// Whether the bridge is currently connected to a room.
  bool isConnected() const;

  // ---------------------------------------------------------------
  // Track creation (publishing)
  // ---------------------------------------------------------------

  /**
   * Create and publish a local audio track.
   *
   * The returned handle is RAII-managed: dropping the shared_ptr
   * automatically unpublishes the track.
   *
   * @pre The bridge must be connected (via connect()). Calling this on a
   *      disconnected bridge is a programming error.
   *
   * @param name         Human-readable track name.
   * @param sample_rate  Sample rate in Hz (e.g. 48000).
   * @param num_channels Number of audio channels (1 = mono, 2 = stereo).
   * @param source       Track source type (e.g. SOURCE_MICROPHONE). Use a
   *                     different source (e.g. SOURCE_SCREENSHARE_AUDIO) to
   *                     publish multiple audio tracks from the same
   *                     participant that can be independently subscribed to.
   * @return Shared pointer to the published audio track handle (never null).
   * @throws std::runtime_error if the bridge is not connected.
   */
  std::shared_ptr<BridgeAudioTrack>
  createAudioTrack(const std::string &name, int sample_rate, int num_channels,
                   livekit::TrackSource source);

  /**
   * Create and publish a local video track.
   *
   * The returned handle is RAII-managed: dropping the shared_ptr
   * automatically unpublishes the track.
   *
   * @pre The bridge must be connected (via connect()). Calling this on a
   *      disconnected bridge is a programming error.
   *
   * @param name   Human-readable track name.
   * @param width  Video width in pixels.
   * @param height Video height in pixels.
   * @param source Track source type (default: SOURCE_CAMERA). Use a
   *               different source (e.g. SOURCE_SCREENSHARE) to publish
   *               multiple video tracks from the same participant that
   *               can be independently subscribed to.
   * @return Shared pointer to the published video track handle (never null).
   * @throws std::runtime_error if the bridge is not connected.
   */
  std::shared_ptr<BridgeVideoTrack>
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

private:
  friend class BridgeRoomDelegate;
  friend class test::CallbackKeyTest;
  friend class test::LiveKitBridgeTest;

  // Composite key for the callback map: (participant_identity, source).
  // Only one callback can exist per key -- re-registering overwrites.
  struct CallbackKey {
    std::string identity;
    livekit::TrackSource source;

    bool operator==(const CallbackKey &o) const;
  };

  struct CallbackKeyHash {
    std::size_t operator()(const CallbackKey &k) const;
  };

  // Active reader thread + stream for an incoming track
  struct ActiveReader {
    std::shared_ptr<livekit::AudioStream> audio_stream;
    std::shared_ptr<livekit::VideoStream> video_stream;
    std::thread thread;
    bool is_audio = false;
  };

  // Called by BridgeRoomDelegate when a remote track is subscribed
  void onTrackSubscribed(const std::string &participant_identity,
                         livekit::TrackSource source,
                         const std::shared_ptr<livekit::Track> &track);

  // Called by BridgeRoomDelegate when a remote track is unsubscribed
  void onTrackUnsubscribed(const std::string &participant_identity,
                           livekit::TrackSource source);

  // Close the stream and extract the thread for the caller to join
  // (caller must hold mutex_)
  std::thread extractReaderThread(const CallbackKey &key);

  // Start a reader thread for a subscribed track.
  // Returns the old reader thread (if any) for the caller to join outside
  // the lock. (caller must hold mutex_)
  std::thread startAudioReader(const CallbackKey &key,
                               const std::shared_ptr<livekit::Track> &track,
                               AudioFrameCallback cb);
  std::thread startVideoReader(const CallbackKey &key,
                               const std::shared_ptr<livekit::Track> &track,
                               VideoFrameCallback cb);

  mutable std::mutex mutex_;
  bool connected_;
  bool connecting_; // guards against concurrent connect() calls
  bool sdk_initialized_;

  static constexpr int kMaxActiveReaders = 20;

  std::unique_ptr<livekit::Room> room_;
  std::unique_ptr<BridgeRoomDelegate> delegate_;

  // Registered callbacks (may be registered before tracks are subscribed)
  std::unordered_map<CallbackKey, AudioFrameCallback, CallbackKeyHash>
      audio_callbacks_;
  std::unordered_map<CallbackKey, VideoFrameCallback, CallbackKeyHash>
      video_callbacks_;

  // Active reader threads for subscribed tracks
  std::unordered_map<CallbackKey, ActiveReader, CallbackKeyHash>
      active_readers_;
};

} // namespace livekit_bridge
