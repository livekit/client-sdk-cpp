/*
 * Copyright 2023 LiveKit
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
#include <optional>
#include <string>
#include <vector>

namespace livekit {

class Room;
enum class VideoCodec;
enum class TrackSource;

enum class ConnectionQuality {
  Poor,
  Good,
  Excellent,
  Lost,
};

enum class ConnectionState {
  Disconnected,
  Connected,
  Reconnecting,
};

enum class DataPacketKind {
  Lossy,
  Reliable,
};

enum class EncryptionState {
  // mirror your proto enum values as needed
  Unknown,
  On,
  Off,
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

// ---------------------------------------------------------
// Basic data types corresponding to proto messages
// ---------------------------------------------------------

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
  // Typically you’d also attach a handle / participant object
  std::string identity; // from OwnedParticipant / ParticipantInfo
  std::string metadata;
  std::string name;
};

struct ParticipantDisconnectedEvent {
  std::string participant_identity;
  DisconnectReason reason = DisconnectReason::Unknown;
};

struct LocalTrackPublishedEvent {
  std::string track_sid;
};

struct LocalTrackUnpublishedEvent {
  std::string publication_sid;
};

struct LocalTrackSubscribedEvent {
  std::string track_sid;
};

struct TrackPublishedEvent {
  std::string participant_identity;
  std::string publication_sid;
  std::string track_name;
  std::string track_kind;   // or an enum if you have one
  std::string track_source; // or enum
};

struct TrackUnpublishedEvent {
  std::string participant_identity;
  std::string publication_sid;
};

struct TrackSubscribedEvent {
  std::string participant_identity;
  std::string track_sid;
  std::string track_name;
  std::string track_kind;   // or enum
  std::string track_source; // or enum
};

struct TrackUnsubscribedEvent {
  std::string participant_identity;
  std::string track_sid;
};

struct TrackSubscriptionFailedEvent {
  std::string participant_identity;
  std::string track_sid;
  std::string error;
};

struct TrackMutedEvent {
  std::string participant_identity;
  std::string track_sid;
};

struct TrackUnmutedEvent {
  std::string participant_identity;
  std::string track_sid;
};

struct ActiveSpeakersChangedEvent {
  std::vector<std::string> participant_identities;
};

struct RoomMetadataChangedEvent {
  std::string metadata;
};

struct RoomSidChangedEvent {
  std::string sid;
};

struct ParticipantMetadataChangedEvent {
  std::string participant_identity;
  std::string metadata;
};

struct ParticipantNameChangedEvent {
  std::string participant_identity;
  std::string name;
};

struct ParticipantAttributesChangedEvent {
  std::string participant_identity;
  std::vector<AttributeEntry> attributes;
  std::vector<AttributeEntry> changed_attributes;
};

struct ParticipantEncryptionStatusChangedEvent {
  std::string participant_identity;
  bool is_encrypted = false;
};

struct ConnectionQualityChangedEvent {
  std::string participant_identity;
  ConnectionQuality quality = ConnectionQuality::Good;
};

struct DataPacketReceivedEvent {
  DataPacketKind kind = DataPacketKind::Reliable;
  std::string participant_identity; // may be empty
  std::optional<UserPacketData> user;
  std::optional<SipDtmfData> sip_dtmf;
};

struct Transcription {
  std::optional<std::string> participant_identity;
  std::optional<std::string> track_sid;
  std::vector<TranscriptionSegment> segments;
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
  // You can expand this into a richer participant struct later
  std::vector<std::string> participant_identities;
};

struct E2eeStateChangedEvent {
  std::string participant_identity;
  EncryptionState state = EncryptionState::Unknown;
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
  virtual void onDataPacketReceived(Room &, const DataPacketReceivedEvent &) {}
  virtual void onTranscriptionReceived(Room &, const Transcription &) {}
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
