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
 *   bridge.connect("wss://my-server.livekit.cloud", my_token);
 *
 *   auto mic = bridge.createAudioTrack("mic", 48000, 2);
 *   auto cam = bridge.createVideoTrack("cam", 1280, 720);
 *
 *   mic->pushFrame(pcm_data, samples_per_channel);
 *   cam->pushFrame(rgba_data, timestamp_us);
 *
 *   bridge.registerOnAudioFrame("remote-participant",
 *       livekit::TrackSource::SOURCE_MICROPHONE,
 *       [](const livekit::AudioFrame& f) { process(f); });
 *
 *   bridge.registerOnVideoFrame("remote-participant",
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
   * Initializes the SDK (if not already), creates a Room, and connects.
   * auto_subscribe is enabled so that remote tracks are subscribed
   * automatically.
   *
   * @param url    WebSocket URL of the LiveKit server.
   * @param token  Access token for authentication.
   * @return true if connection succeeded.
   */
  bool connect(const std::string &url, const std::string &token);

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
   * @param name         Human-readable track name.
   * @param sample_rate  Sample rate in Hz (e.g. 48000).
   * @param num_channels Number of audio channels (1 = mono, 2 = stereo).
   * @return Shared pointer to the published audio track handle.
   * @throws std::runtime_error on failure.
   */
  std::shared_ptr<BridgeAudioTrack>
  createAudioTrack(const std::string &name, int sample_rate, int num_channels);

  /**
   * Create and publish a local video track.
   *
   * The returned handle is RAII-managed: dropping the shared_ptr
   * automatically unpublishes the track.
   *
   * @param name   Human-readable track name.
   * @param width  Video width in pixels.
   * @param height Video height in pixels.
   * @return Shared pointer to the published video track handle.
   * @throws std::runtime_error on failure.
   */
  std::shared_ptr<BridgeVideoTrack> createVideoTrack(const std::string &name,
                                                     int width, int height);

  // ---------------------------------------------------------------
  // Incoming frame callbacks
  // ---------------------------------------------------------------

  /**
   * Register a callback for audio frames from a specific remote participant
   * and track source.
   *
   * The callback fires on a background thread whenever a new audio frame
   * is received. If the remote participant has not yet connected, the
   * callback is stored and auto-wired when the participant's track is
   * subscribed.
   *
   * @param participant_identity  Identity of the remote participant.
   * @param source                Track source (e.g. SOURCE_MICROPHONE).
   * @param callback              Function to invoke per audio frame.
   */
  void registerOnAudioFrame(const std::string &participant_identity,
                            livekit::TrackSource source,
                            AudioFrameCallback callback);

  /**
   * Register a callback for video frames from a specific remote participant
   * and track source.
   *
   * @param participant_identity  Identity of the remote participant.
   * @param source                Track source (e.g. SOURCE_CAMERA).
   * @param callback              Function to invoke per video frame.
   */
  void registerOnVideoFrame(const std::string &participant_identity,
                            livekit::TrackSource source,
                            VideoFrameCallback callback);

  /**
   * Unregister a previously registered audio frame callback.
   *
   * If a reader thread is active for this (identity, source), it is
   * stopped and joined.
   */
  void unregisterOnAudioFrame(const std::string &participant_identity,
                              livekit::TrackSource source);

  /**
   * Unregister a previously registered video frame callback.
   */
  void unregisterOnVideoFrame(const std::string &participant_identity,
                              livekit::TrackSource source);

private:
  friend class BridgeRoomDelegate;
  friend class test::CallbackKeyTest;
  friend class test::LiveKitBridgeTest;

  // Composite key for the callback map: (participant_identity, source)
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

  // Close the stream and detach the thread (caller must hold mutex_)
  void stopReader(const CallbackKey &key);

  // Start a reader thread for a subscribed track
  void startAudioReader(const CallbackKey &key,
                        const std::shared_ptr<livekit::Track> &track,
                        AudioFrameCallback cb);
  void startVideoReader(const CallbackKey &key,
                        const std::shared_ptr<livekit::Track> &track,
                        VideoFrameCallback cb);

  mutable std::mutex mutex_;
  bool connected_ = false;
  bool connecting_ = false; // guards against concurrent connect() calls
  bool sdk_initialized_ = false;

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
