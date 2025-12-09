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

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "livekit/local_track_publication.h"
#include "livekit/remote_track_publication.h"
#include "livekit/track_publication.h"

namespace livekit {

class Room;
enum class VideoCodec;
enum class TrackSource;
class Track;
class RemoteParticipant;
class LocalTrackPublication;
class Participant;

enum class ConnectionQuality {
  Poor = 0,
  Good,
  Excellent,
  Lost,
};

enum class ConnectionState {
  Disconnected = 0,
  Connected,
  Reconnecting,
};

enum class DataPacketKind {
  Lossy,
  Reliable,
};

enum class EncryptionState {
  New = 0,
  Ok,
  EncryptionFailed,
  DecryptionFailed,
  MissingKey,
  KeyRatcheted,
  InternalError,
};

enum class DisconnectReason {
  Unknown = 0,
  ClientInitiated,
  DuplicateIdentity,
  ServerShutdown,
  ParticipantRemoved,
  RoomDeleted,
  StateMismatch,
  JoinFailure,
  Migration,
  SignalClose,
  RoomClosed,
  UserUnavailable,
  UserRejected,
  SipTrunkFailure,
  ConnectionTimeout,
  MediaFailure
};

struct ChatMessageData {
  std::string id;
  std::int64_t timestamp = 0;
  std::string message;
  std::optional<std::int64_t> edit_timestamp;
  bool deleted = false;
  bool generated = false;
};

struct UserPacketData {
  std::vector<std::uint8_t> data;
  std::optional<std::string> topic; // optional
};

struct SipDtmfData {
  std::uint32_t code = 0;
  std::optional<std::string> digit;
};

struct RoomInfoData {
  std::optional<std::string> sid;
  std::string name;
  std::string metadata;
  std::uint64_t lossy_dc_buffered_amount_low_threshold = 0;
  std::uint64_t reliable_dc_buffered_amount_low_threshold = 0;
  std::uint32_t empty_timeout = 0;
  std::uint32_t departure_timeout = 0;
  std::uint32_t max_participants = 0;
  std::int64_t creation_time = 0;
  std::uint32_t num_participants = 0;
  std::uint32_t num_publishers = 0;
  bool active_recording = false;
};

struct AttributeEntry {
  std::string key;
  std::string value;
  AttributeEntry() = default;
  AttributeEntry(std::string k, std::string v)
      : key(std::move(k)), value(std::move(v)) {}
};

struct DataStreamHeaderData {
  std::string stream_id;
  std::int64_t timestamp = 0;
  std::string mime_type;
  std::string topic;
  std::optional<std::uint64_t> total_length;
  std::map<std::string, std::string> attributes;

  // For content_header
  enum class ContentType {
    None,
    Text,
    Byte,
  } content_type = ContentType::None;

  // TextHeader fields
  enum class OperationType {
    Create = 0,
    Update = 1,
    Delete = 2,
    Reaction = 3,
  };
  std::optional<OperationType> operation_type;
  std::optional<int> version;
  std::optional<std::string> reply_to_stream_id;
  std::vector<std::string> attached_stream_ids;
  std::optional<bool> generated;

  // ByteHeader fields
  std::optional<std::string> name;
};

struct DataStreamChunkData {
  std::string stream_id;
  std::uint64_t chunk_index = 0;
  std::vector<std::uint8_t> content;
  std::optional<int> version;
  std::vector<std::uint8_t> iv;
};

struct DataStreamTrailerData {
  std::string stream_id;
  std::string reason;
  std::map<std::string, std::string> attributes;
};

// ------------- rooom.proto options ------------------------

struct VideoEncodingOptions {
  std::uint64_t max_bitrate = 0;
  double max_framerate = 0.0;
};

struct AudioEncodingOptions {
  std::uint64_t max_bitrate = 0;
};

struct TrackPublishOptions {
  std::optional<VideoEncodingOptions> video_encoding;
  std::optional<AudioEncodingOptions> audio_encoding;
  std::optional<VideoCodec> video_codec;
  std::optional<bool> dtx;
  std::optional<bool> red;
  std::optional<bool> simulcast;
  std::optional<TrackSource> source;
  std::optional<std::string> stream;
  std::optional<bool> preconnect_buffer;
};

// ------------- rooom.proto Transcription ------------------------

struct TranscriptionSegment {
  std::string id;
  std::string text;
  std::uint64_t start_time = 0;
  std::uint64_t end_time = 0;
  bool final = false;
  std::string language;
};

// ---------------------------------------------------------
// Event structs – “public” representations of RoomEvent.*
// ---------------------------------------------------------

struct ParticipantConnectedEvent {
  RemoteParticipant *participant = nullptr; // Owned by room
};

struct ParticipantDisconnectedEvent {
  RemoteParticipant *participant = nullptr; // Owned by room
  DisconnectReason reason = DisconnectReason::Unknown;
};

struct LocalTrackPublishedEvent {
  std::shared_ptr<LocalTrackPublication> publication;
  std::shared_ptr<Track> track;
};

struct LocalTrackUnpublishedEvent {
  std::shared_ptr<LocalTrackPublication> publication;
};

struct LocalTrackSubscribedEvent {
  std::shared_ptr<Track> track;
};

struct TrackPublishedEvent {
  std::shared_ptr<RemoteTrackPublication> publication;
  RemoteParticipant *participant = nullptr; // Owned by room
};

struct TrackUnpublishedEvent {
  std::shared_ptr<RemoteTrackPublication> publication;
  RemoteParticipant *participant = nullptr;
};

struct TrackSubscribedEvent {
  std::shared_ptr<Track> track;
  std::shared_ptr<RemoteTrackPublication> publication;
  RemoteParticipant *participant = nullptr; // Owned by room
};

struct TrackUnsubscribedEvent {
  std::shared_ptr<Track> track;
  std::shared_ptr<RemoteTrackPublication> publication;
  RemoteParticipant *participant = nullptr; // Owned by room
};

struct TrackSubscriptionFailedEvent {
  RemoteParticipant *participant = nullptr; // Owned by room
  std::string track_sid;
  std::string error;
};

struct TrackMutedEvent {
  Participant *participant = nullptr; // Local or Remote, owned by room
  std::shared_ptr<TrackPublication> publication;
};

struct TrackUnmutedEvent {
  Participant *participant = nullptr; // Local or Remote, owned by room
  std::shared_ptr<TrackPublication> publication;
};

struct ActiveSpeakersChangedEvent {
  std::vector<Participant *> speakers;
};

struct RoomMetadataChangedEvent {
  std::string old_metadata;
  std::string new_metadata;
};

struct RoomSidChangedEvent {
  std::string sid;
};

struct ParticipantMetadataChangedEvent {
  Participant *participant = nullptr; // Local or Remote, owned by room
  std::string old_metadata;
  std::string new_metadata;
};

struct ParticipantNameChangedEvent {
  Participant *participant = nullptr; // Local or Remote, owned by room
  std::string old_name;
  std::string new_name;
};

struct ParticipantAttributesChangedEvent {
  Participant *participant = nullptr; // Local or Remote, owned by room
  std::vector<AttributeEntry> changed_attributes;
};

struct ParticipantEncryptionStatusChangedEvent {
  Participant *participant = nullptr; // Local or Remote, owned by room
  bool is_encrypted = false;
};

struct ConnectionQualityChangedEvent {
  Participant *participant = nullptr; // Local or Remote, owned by room
  ConnectionQuality quality = ConnectionQuality::Good;
};

struct UserDataPacketEvent {
  std::vector<std::uint8_t> data;
  DataPacketKind kind = DataPacketKind::Reliable;
  RemoteParticipant *participant = nullptr; // may be null, owned by room
  std::string topic;
};

struct SipDtmfReceivedEvent {
  int code = 0;
  std::string digit;
  RemoteParticipant *participant = nullptr; // owned by room
};

struct Transcription {
  std::optional<std::string> participant_identity;
  std::optional<std::string> track_sid;
  std::vector<TranscriptionSegment> segments;
};

struct TranscriptionReceivedEvent {
  std::vector<TranscriptionSegment> segments;
  Participant *participant = nullptr; // Local or Remote, owned by room
  std::shared_ptr<TrackPublication> publication;
};

struct ConnectionStateChangedEvent {
  ConnectionState state = ConnectionState::Disconnected;
};

struct DisconnectedEvent {
  DisconnectReason reason = DisconnectReason::Unknown;
};

struct ReconnectingEvent {};
struct ReconnectedEvent {};

struct RoomEosEvent {};

struct DataStreamHeaderReceivedEvent {
  std::string participant_identity;
  DataStreamHeaderData header;
};

struct DataStreamChunkReceivedEvent {
  std::string participant_identity;
  DataStreamChunkData chunk;
};

struct DataStreamTrailerReceivedEvent {
  std::string participant_identity;
  DataStreamTrailerData trailer;
};

struct DataChannelBufferedAmountLowThresholdChangedEvent {
  DataPacketKind kind = DataPacketKind::Reliable;
  std::uint64_t threshold = 0;
};

struct ByteStreamOpenedEvent {
  std::uint64_t reader_handle = 0; // from OwnedByteStreamReader.handle
  std::string participant_identity;
};

struct TextStreamOpenedEvent {
  std::uint64_t reader_handle = 0; // from OwnedTextStreamReader.handle
  std::string participant_identity;
};

struct RoomUpdatedEvent {
  RoomInfoData info;
};

struct RoomMovedEvent {
  RoomInfoData info;
};

struct ParticipantsUpdatedEvent {
  std::vector<Participant *> participants;
};

struct E2eeStateChangedEvent {
  Participant *participant = nullptr; // local or remote, owned by room
  EncryptionState state = EncryptionState::New;
};

struct ChatMessageReceivedEvent {
  ChatMessageData message;
  std::string participant_identity;
};

// ---------------------------------------------------------
// RoomDelegate interface – NO protobuf dependency
// ---------------------------------------------------------

class RoomDelegate {
public:
  virtual ~RoomDelegate() = default;

  // Optional: generic hook with no payload
  virtual void onRoomEvent(Room & /*room*/) {}

  // Per-event callbacks. All default no-op so you can add more later
  // without breaking existing user code.

  // Participant lifecycle
  virtual void onParticipantConnected(Room &,
                                      const ParticipantConnectedEvent &) {}
  virtual void onParticipantDisconnected(Room &,
                                         const ParticipantDisconnectedEvent &) {
  }

  // Local track publication
  virtual void onLocalTrackPublished(Room &, const LocalTrackPublishedEvent &) {
  }
  virtual void onLocalTrackUnpublished(Room &,
                                       const LocalTrackUnpublishedEvent &) {}
  virtual void onLocalTrackSubscribed(Room &,
                                      const LocalTrackSubscribedEvent &) {}

  // Remote track publication/subscription
  virtual void onTrackPublished(Room &, const TrackPublishedEvent &) {}
  virtual void onTrackUnpublished(Room &, const TrackUnpublishedEvent &) {}
  virtual void onTrackSubscribed(Room &, const TrackSubscribedEvent &) {}
  virtual void onTrackUnsubscribed(Room &, const TrackUnsubscribedEvent &) {}
  virtual void onTrackSubscriptionFailed(Room &,
                                         const TrackSubscriptionFailedEvent &) {
  }
  virtual void onTrackMuted(Room &, const TrackMutedEvent &) {}
  virtual void onTrackUnmuted(Room &, const TrackUnmutedEvent &) {}

  // Active speakers
  virtual void onActiveSpeakersChanged(Room &,
                                       const ActiveSpeakersChangedEvent &) {}

  // Room info / metadata
  virtual void onRoomMetadataChanged(Room &, const RoomMetadataChangedEvent &) {
  }
  virtual void onRoomSidChanged(Room &, const RoomSidChangedEvent &) {}
  virtual void onRoomUpdated(Room &, const RoomUpdatedEvent &) {}
  virtual void onRoomMoved(Room &, const RoomMovedEvent &) {}

  // Participant info changes
  virtual void
  onParticipantMetadataChanged(Room &,
                               const ParticipantMetadataChangedEvent &) {}
  virtual void onParticipantNameChanged(Room &,
                                        const ParticipantNameChangedEvent &) {}
  virtual void
  onParticipantAttributesChanged(Room &,
                                 const ParticipantAttributesChangedEvent &) {}
  virtual void onParticipantEncryptionStatusChanged(
      Room &, const ParticipantEncryptionStatusChangedEvent &) {}

  // Connection quality / state
  virtual void
  onConnectionQualityChanged(Room &, const ConnectionQualityChangedEvent &) {}
  virtual void onConnectionStateChanged(Room &,
                                        const ConnectionStateChangedEvent &) {}
  virtual void onDisconnected(Room &, const DisconnectedEvent &) {}
  virtual void onReconnecting(Room &, const ReconnectingEvent &) {}
  virtual void onReconnected(Room &, const ReconnectedEvent &) {}

  // E2EE
  virtual void onE2eeStateChanged(Room &, const E2eeStateChangedEvent &) {}

  // EOS
  virtual void onRoomEos(Room &, const RoomEosEvent &) {}

  // Data / transcription / chat
  virtual void onUserPacketReceived(Room &, const UserDataPacketEvent &) {}
  virtual void onSipDtmfReceived(Room &, const SipDtmfReceivedEvent &) {}
  virtual void onTranscriptionReceived(Room &,
                                       const TranscriptionReceivedEvent &) {}
  virtual void onChatMessageReceived(Room &, const ChatMessageReceivedEvent &) {
  }

  // Data streams
  virtual void
  onDataStreamHeaderReceived(Room &, const DataStreamHeaderReceivedEvent &) {}
  virtual void onDataStreamChunkReceived(Room &,
                                         const DataStreamChunkReceivedEvent &) {
  }
  virtual void
  onDataStreamTrailerReceived(Room &, const DataStreamTrailerReceivedEvent &) {}
  virtual void onDataChannelBufferedAmountLowThresholdChanged(
      Room &, const DataChannelBufferedAmountLowThresholdChangedEvent &) {}

  // High-level byte/text streams
  virtual void onByteStreamOpened(Room &, const ByteStreamOpenedEvent &) {}
  virtual void onTextStreamOpened(Room &, const TextStreamOpenedEvent &) {}

  // Participants snapshot
  virtual void onParticipantsUpdated(Room &, const ParticipantsUpdatedEvent &) {
  }
};

} // namespace livekit
