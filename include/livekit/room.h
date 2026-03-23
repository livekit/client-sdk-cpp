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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LIVEKIT_ROOM_H
#define LIVEKIT_ROOM_H

#include "livekit/audio_stream.h"
#include "livekit/data_stream.h"
#include "livekit/e2ee.h"
#include "livekit/ffi_handle.h"
#include "livekit/room_event_types.h"
#include "livekit/video_stream.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace livekit {

class AudioFrame;
class VideoFrame;
class RoomDelegate;
struct RoomInfoData;
namespace proto {
class FfiEvent;
}

/// Callback type for incoming audio frames.
/// Invoked on a dedicated reader thread per (participant, source) pair.
using AudioFrameCallback = std::function<void(const AudioFrame &)>;

/// Callback type for incoming video frames.
/// Invoked on a dedicated reader thread per (participant, source) pair.
using VideoFrameCallback =
    std::function<void(const VideoFrame &frame, std::int64_t timestamp_us)>;

struct E2EEOptions;
class E2EEManager;
class LocalParticipant;
class RemoteParticipant;

// Represents a single ICE server configuration.
struct IceServer {
  // TURN/STUN server URL (e.g. "stun:stun.l.google.com:19302").
  std::string url;

  // Optional username for TURN authentication.
  std::string username;

  // Optional credential (password) for TURN authentication.
  std::string credential;
};

// WebRTC configuration (ICE, transport, etc.).
struct RtcConfig {
  // ICE transport type (e.g., ALL, RELAY). Maps to proto::IceTransportType.
  int ice_transport_type = 0;

  // Continuous or single ICE gathering. Maps to
  // proto::ContinualGatheringPolicy.
  int continual_gathering_policy = 0;

  // List of STUN/TURN servers for ICE candidate generation.
  std::vector<IceServer> ice_servers;
};

// Top-level room connection options.
struct RoomOptions {
  // If true (default), automatically subscribe to all remote tracks.
  // This is CRITICAL. Without auto_subscribe, you will never receive:
  //   - `track_subscribed` events
  //   - remote audio/video frames
  bool auto_subscribe = true;

  // Enable dynacast (server sends optimal layers depending on subscribers).
  bool dynacast = false;

  // Enable single peer connection mode. When true, uses one RTCPeerConnection
  // for both publishing and subscribing instead of two separate connections.
  // Falls back to dual peer connection if the server doesn't support single PC.
  bool single_peer_connection = false;

  // Optional WebRTC configuration (ICE policy, servers, etc.)
  std::optional<RtcConfig> rtc_config;

  // Optional end-to-end encryption settings.
  std::optional<E2EEOptions> encryption;
};

/// Represents a LiveKit room session.
/// A Room manages:
///   - the connection to the LiveKit server
///   - participant list (local + remote)
///   - track publications
///   - server events forwarded to a RoomDelegate
class Room {
public:
  Room();
  ~Room();

  /* Assign a RoomDelegate that receives room lifecycle callbacks.
   *
   * The delegate must remain valid for the lifetime of the Room or until a
   * different delegate is assigned. The Room does not take ownership.
   * Typical usage:
   *     class MyDelegate : public RoomDelegate { ... };
   *     MyDelegate del;
   *     Room room;
   *     room.setDelegate(&del);
   */
  void setDelegate(RoomDelegate *delegate);

  /* Connect to a LiveKit room using the given URL and token,  applying the
   * supplied connection options.
   *
   * Parameters:
   *   url      — WebSocket URL of the LiveKit server.
   *   token    — Access token for authentication.
   *   options  — Connection options controlling auto-subscribe,
   *               dynacast, E2EE, and WebRTC configuration.
   * Behavior:
   *   - Registers an FFI event listener *before* sending the connect request.
   *   - Sends a proto::FfiRequest::Connect with the URL, token,
   *     and the provided RoomOptions.
   *   - Blocks until the FFI connect response arrives.
   *   - Initializes local participant and remote participants.
   *   - Emits room/participant/track events to the delegate.
   * IMPORTANT:
   *   RoomOptions defaults auto_subscribe = true.
   *   Without auto_subscribe enabled, remote tracks will NOT be subscribed
   *   automatically, and no remote audio/video will ever arrive.
   */
  bool Connect(const std::string &url, const std::string &token,
               const RoomOptions &options);

  // Accessors

  /* Retrieve static metadata about the room.
   * This contains fields such as:
   *   - SID
   *   - room name
   *   - metadata
   *   - participant counts
   *   - creation timestamp
   */
  RoomInfoData room_info() const;

  /* Get the local participant.
   *
   * This object represents the current user, including:
   *   - published tracks (audio/video/screen)
   *   - identity, SID, metadata
   *   - publishing/unpublishing operations
   * Return value:
   *   Non-null pointer after successful Connect().
   */
  LocalParticipant *localParticipant() const;

  /* Look up a remote participant by identity.
   *
   * Parameters:
   *   identity — The participant’s identity string (not SID)
   * Return value:
   *   Pointer to RemoteParticipant if present, otherwise nullptr.
   * RemoteParticipant contains:
   *   - identity/name/metadata
   *   - track publications
   *  - callbacks for track subscribed/unsubscribed, muted/unmuted
   */
  RemoteParticipant *remoteParticipant(const std::string &identity) const;

  /// Returns a snapshot of all current remote participants.
  std::vector<std::shared_ptr<RemoteParticipant>> remoteParticipants() const;

  /* Register a handler for incoming text streams on a specific topic.
   *
   * When a remote participant opens a text stream with the given topic,
   * the handler is invoked with:
   *   - a shared_ptr<TextStreamReader> for consuming the stream
   *   - the identity of the participant who sent the stream
   *
   * Notes:
   *   - Only one handler may be registered per topic.
   *   - If no handler is registered for a topic, incoming streams with that
   *     topic are ignored.
   *   - The handler is invoked on the Room event thread. The handler must
   *     not block; spawn a background thread if synchronous reading is
   *     required.
   *
   * Throws:
   *   std::runtime_error if a handler is already registered for the topic.
   */
  void registerTextStreamHandler(const std::string &topic,
                                 TextStreamHandler handler);

  /* Unregister the text stream handler for the given topic.
   *
   * If no handler exists for the topic, this function is a no-op.
   */
  void unregisterTextStreamHandler(const std::string &topic);

  /* Register a handler for incoming byte streams on a specific topic.
   *
   * When a remote participant opens a byte stream with the given topic,
   * the handler is invoked with:
   *   - a shared_ptr<ByteStreamReader> for consuming the stream
   *   - the identity of the participant who sent the stream
   *
   * Notes:
   *   - Only one handler may be registered per topic.
   *   - If no handler is registered for a topic, incoming streams with that
   *     topic are ignored.
   *   - The ByteStreamReader remains valid as long as the shared_ptr is held,
   *     preventing lifetime-related crashes when reading asynchronously.
   *
   * Throws:
   *   std::runtime_error if a handler is already registered for the topic.
   */
  void registerByteStreamHandler(const std::string &topic,
                                 ByteStreamHandler handler);

  /* Unregister the byte stream handler for the given topic.
   *
   * If no handler exists for the topic, this function is a no-op.
   */
  void unregisterByteStreamHandler(const std::string &topic);

  /**
   * Returns the room's E2EE manager, or nullptr if E2EE was not enabled at
   * connect time.
   *
   * Notes:
   * - The manager is created after a successful Connect().
   * - If E2EE was not configured in RoomOptions, this will return nullptr.
   */
  E2EEManager *e2eeManager() const;

  // ---------------------------------------------------------------
  // Frame callbacks
  // ---------------------------------------------------------------

  /**
   * Set a callback for audio frames from a specific remote participant and
   * track source.
   *
   * A dedicated reader thread is spawned for each (participant, source) pair
   * when the track is subscribed. If the track is already subscribed, the
   * reader starts immediately. If not, it starts when the track arrives.
   *
   * Only one callback may exist per (participant, source) pair. Re-calling
   * with the same pair replaces the previous callback.
   *
   * @param participant_identity Identity of the remote participant.
   * @param source               Track source (e.g. SOURCE_MICROPHONE).
   * @param callback             Function invoked per audio frame.
   * @param opts                 AudioStream options (capacity, noise
   * cancellation).
   */
  void setOnAudioFrameCallback(const std::string &participant_identity,
                               TrackSource source, AudioFrameCallback callback,
                               AudioStream::Options opts = {});

  /**
   * Set a callback for video frames from a specific remote participant and
   * track source.
   *
   * @see setOnAudioFrameCallback for threading and lifecycle semantics.
   *
   * @param participant_identity Identity of the remote participant.
   * @param source               Track source (e.g. SOURCE_CAMERA).
   * @param callback             Function invoked per video frame.
   * @param opts                 VideoStream options (capacity, pixel format).
   */
  void setOnVideoFrameCallback(const std::string &participant_identity,
                               TrackSource source, VideoFrameCallback callback,
                               VideoStream::Options opts = {});

  /**
   * Clear the audio frame callback for a specific (participant, source) pair.
   * Stops and joins any active reader thread.
   * No-op if no callback is registered for this key.
   * @param participant_identity Identity of the remote participant.
   * @param source               Track source (e.g. SOURCE_MICROPHONE).
   */
  void clearOnAudioFrameCallback(const std::string &participant_identity,
                                 TrackSource source);

  /**
   * Clear the video frame callback for a specific (participant, source) pair.
   * Stops and joins any active reader thread.
   * No-op if no callback is registered for this key.
   * @param participant_identity Identity of the remote participant.
   * @param source               Track source (e.g. SOURCE_CAMERA).
   */
  void clearOnVideoFrameCallback(const std::string &participant_identity,
                                 TrackSource source);

private:
  mutable std::mutex lock_;
  ConnectionState connection_state_ = ConnectionState::Disconnected;
  RoomDelegate *delegate_ = nullptr; // Not owned
  RoomInfoData room_info_;
  std::shared_ptr<FfiHandle> room_handle_;
  std::unique_ptr<LocalParticipant> local_participant_;
  std::unordered_map<std::string, std::shared_ptr<RemoteParticipant>>
      remote_participants_;
  // Data stream
  std::unordered_map<std::string, TextStreamHandler> text_stream_handlers_;
  std::unordered_map<std::string, ByteStreamHandler> byte_stream_handlers_;
  std::unordered_map<std::string, std::shared_ptr<TextStreamReader>>
      text_stream_readers_;
  std::unordered_map<std::string, std::shared_ptr<ByteStreamReader>>
      byte_stream_readers_;
  // E2EE
  std::unique_ptr<E2EEManager> e2ee_manager_;

  // FfiClient listener ID (0 means no listener registered)
  int listener_id_{0};

  void OnEvent(const proto::FfiEvent &event);

  // -------------------------------------------------------------------
  // Frame callback internals
  // -------------------------------------------------------------------

  struct CallbackKey {
    std::string participant_identity;
    TrackSource source;
    bool operator==(const CallbackKey &o) const {
      return participant_identity == o.participant_identity &&
             source == o.source;
    }
  };

  struct CallbackKeyHash {
    std::size_t operator()(const CallbackKey &k) const {
      auto h1 = std::hash<std::string>{}(k.participant_identity);
      auto h2 = std::hash<int>{}(static_cast<int>(k.source));
      return h1 ^ (h2 << 1);
    }
  };

  struct ActiveReader {
    std::shared_ptr<AudioStream> audio_stream;
    std::shared_ptr<VideoStream> video_stream;
    std::thread thread;
  };

  struct RegisteredAudioCallback {
    AudioFrameCallback callback;
    AudioStream::Options options;
  };

  struct RegisteredVideoCallback {
    VideoFrameCallback callback;
    VideoStream::Options options;
  };

  std::unordered_map<CallbackKey, RegisteredAudioCallback, CallbackKeyHash>
      audio_callbacks_;
  std::unordered_map<CallbackKey, RegisteredVideoCallback, CallbackKeyHash>
      video_callbacks_;
  std::unordered_map<CallbackKey, ActiveReader, CallbackKeyHash>
      active_readers_;

  // Must be called with lock_ held. Closes the stream for the given key and
  // returns the old reader thread (which the caller must join outside the
  // lock).
  std::thread extractReaderThread(const CallbackKey &key);

  // Must be called with lock_ held. Returns old reader thread if one existed.
  std::thread startAudioReader(const CallbackKey &key,
                               const std::shared_ptr<Track> &track,
                               AudioFrameCallback cb,
                               const AudioStream::Options &opts);
  std::thread startVideoReader(const CallbackKey &key,
                               const std::shared_ptr<Track> &track,
                               VideoFrameCallback cb,
                               const VideoStream::Options &opts);

  // Stops all readers (closes streams, joins threads). Must NOT hold lock_.
  void stopAllReaders();
};
} // namespace livekit

#endif /* LIVEKIT_ROOM_H */
