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

namespace livekit {

// Forward declarations to avoid pulling in heavy headers.
class Track;
class Participant;
class RemoteParticipant;
class LocalTrackPublication;
class RemoteTrackPublication;
class TrackPublication;

enum class VideoCodec;
enum class TrackSource;

/**
 * Overall quality of a participant's connection.
 */
enum class ConnectionQuality {
  Poor = 0,
  Good,
  Excellent,
  Lost,
};

/**
 * Current connection state of the room.
 */
enum class ConnectionState {
  Disconnected = 0,
  Connected,
  Reconnecting,
};

/**
 * Type of data packet delivery semantics.
 *
 * - Lossy: unordered, unreliable (e.g. for real-time updates).
 * - Reliable: ordered, reliable (e.g. for critical messages).
 */
enum class DataPacketKind {
  Lossy,
  Reliable,
};

/**
 * End-to-end encryption state for a participant.
 *
 * These values mirror the proto::EncryptionState enum.
 */
enum class EncryptionState {
  New = 0,
  Ok,
  EncryptionFailed,
  DecryptionFailed,
  MissingKey,
  KeyRatcheted,
  InternalError,
};

/**
 * Reason why a participant or room was disconnected.
 *
 * These values mirror the server-side DisconnectReason enum.
 */
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

/**
 * A chat message associated with the room.
 */
struct ChatMessageData {
  /** Unique ID of the message. */
  std::string id;

  /** Timestamp (ms since Unix epoch). */
  std::int64_t timestamp = 0;

  /** Message body. */
  std::string message;

  /** Optional timestamp when the message was edited (ms since Unix epoch). */
  std::optional<std::int64_t> edit_timestamp;

  /** True if the message has been deleted. */
  bool deleted = false;

  /** True if the message was generated (e.g. by an AI or system). */
  bool generated = false;
};

/**
 * Application-level user data carried in a data packet.
 */
struct UserPacketData {
  /** Raw payload bytes. */
  std::vector<std::uint8_t> data;

  /** Optional topic name associated with this payload. */
  std::optional<std::string> topic;
};

/**
 * SIP DTMF payload carried via data packets.
 */
struct SipDtmfData {
  /** DTMF code value. */
  std::uint32_t code = 0;

  /** Human-readable digit representation (e.g. "1", "#"). */
  std::optional<std::string> digit;
};

/**
 * Snapshot of core room information.
 */
struct RoomInfoData {
  /** Room SID, if known. */
  std::optional<std::string> sid;

  /** Room name. */
  std::string name;

  /** Arbitrary application metadata associated with the room. */
  std::string metadata;

  /** Low-watermark threshold for lossy data channel buffer. */
  std::uint64_t lossy_dc_buffered_amount_low_threshold = 0;

  /** Low-watermark threshold for reliable data channel buffer. */
  std::uint64_t reliable_dc_buffered_amount_low_threshold = 0;

  /** Time (seconds) to keep room open if no participants join. */
  std::uint32_t empty_timeout = 0;

  /** Time (seconds) to keep room open after last standard participant leaves.
   */
  std::uint32_t departure_timeout = 0;

  /** Maximum number of participants allowed in the room. */
  std::uint32_t max_participants = 0;

  /** Creation time of the room (ms since Unix epoch). */
  std::int64_t creation_time = 0;

  /** Approximate number of participants (eventually consistent). */
  std::uint32_t num_participants = 0;

  /** Approximate number of publishers (eventually consistent). */
  std::uint32_t num_publishers = 0;

  /** True if the room is currently being recorded. */
  bool active_recording = false;
};

/**
 * Key/value pair for participant or room attributes.
 */
struct AttributeEntry {
  /** Attribute key. */
  std::string key;

  /** Attribute value. */
  std::string value;

  AttributeEntry() = default;

  AttributeEntry(std::string k, std::string v)
      : key(std::move(k)), value(std::move(v)) {}
};

/**
 * Header information for an incoming data stream.
 * Represents proto_room.DataStream.Header in a C++-friendly form.
 */
struct DataStreamHeaderData {
  /** Unique stream identifier. */
  std::string stream_id;

  /** Timestamp (ms since Unix epoch). */
  std::int64_t timestamp = 0;

  /** MIME type of the content (e.g. "application/json"). */
  std::string mime_type;

  /** Application-defined topic name. */
  std::string topic;

  /** Optional total length in bytes, if known. */
  std::optional<std::uint64_t> total_length;

  /** Custom attributes associated with this stream. */
  std::map<std::string, std::string> attributes;

  /**
   * Content type carried by this stream.
   */
  enum class ContentType {
    None,
    Text,
    Byte,
  } content_type = ContentType::None;

  /**
   * Operation type for text streams.
   */
  enum class OperationType {
    Create = 0,
    Update = 1,
    Delete = 2,
    Reaction = 3,
  };

  /** Optional operation type, for text content. */
  std::optional<OperationType> operation_type;

  /** Optional version number for the text stream. */
  std::optional<int> version;

  /** Optional ID of the stream this one replies to. */
  std::optional<std::string> reply_to_stream_id;

  /** IDs of streams attached to this one. */
  std::vector<std::string> attached_stream_ids;

  /** True if this stream was generated (e.g. by AI). */
  std::optional<bool> generated;

  /** Optional filename for byte streams. */
  std::optional<std::string> name;
};

/**
 * One chunk of a data stream’s payload.
 */
struct DataStreamChunkData {
  /** Stream identifier this chunk belongs to. */
  std::string stream_id;

  /** Zero-based index of this chunk. */
  std::uint64_t chunk_index = 0;

  /** Raw chunk content. */
  std::vector<std::uint8_t> content;

  /** Optional version, mirroring header version if applicable. */
  std::optional<int> version;

  /** Optional initialization vector for encrypted payloads. */
  std::vector<std::uint8_t> iv;
};

/**
 * Trailer metadata for a data stream, sent after all chunks.
 */
struct DataStreamTrailerData {
  /** Stream identifier. */
  std::string stream_id;

  /** Reason why the stream ended (empty if normal completion). */
  std::string reason;

  /** Additional attributes describing the final state of the stream. */
  std::map<std::string, std::string> attributes;
};

/**
 * Video encoding configuration used when publishing a track.
 */
struct VideoEncodingOptions {
  /** Maximum target bitrate in bps. */
  std::uint64_t max_bitrate = 0;

  /** Maximum frame rate in frames per second. */
  double max_framerate = 0.0;
};

/**
 * Audio encoding configuration used when publishing a track.
 */
struct AudioEncodingOptions {
  /** Maximum target bitrate in bps. */
  std::uint64_t max_bitrate = 0;
};

/**
 * Options for publishing a track to the room.
 */
struct TrackPublishOptions {
  /** Optional video encoding parameters. */
  std::optional<VideoEncodingOptions> video_encoding;

  /** Optional audio encoding parameters. */
  std::optional<AudioEncodingOptions> audio_encoding;

  /** Optional video codec to use. */
  std::optional<VideoCodec> video_codec;

  /** Enable or disable discontinuous transmission (DTX). */
  std::optional<bool> dtx;

  /** Enable or disable RED (redundant encoding). */
  std::optional<bool> red;

  /** Enable or disable simulcast. */
  std::optional<bool> simulcast;

  /** Track source (camera, microphone, screen share, etc.). */
  std::optional<TrackSource> source;

  /** Optional stream label/group for this track. */
  std::optional<std::string> stream;

  /** Enable pre-connect buffering for lower startup latency. */
  std::optional<bool> preconnect_buffer;
};

/**
 * One transcription segment produced by speech recognition.
 */
struct TranscriptionSegment {
  /** Segment identifier. */
  std::string id;

  /** Transcribed text. */
  std::string text;

  /** Start time (ms) relative to the beginning of the audio source. */
  std::uint64_t start_time = 0;

  /** End time (ms) relative to the beginning of the audio source. */
  std::uint64_t end_time = 0;

  /** True if this segment is final and will not be updated further. */
  bool final = false;

  /** Language code (e.g. "en-US"). */
  std::string language;
};

// ---------------------------------------------------------
// Event structs – public representations of RoomEvent.*
// ---------------------------------------------------------

/**
 * Fired when a remote participant joins the room.
 */
struct ParticipantConnectedEvent {
  /** The newly connected remote participant (owned by Room). */
  RemoteParticipant *participant = nullptr;
};

/**
 * Fired when a remote participant leaves the room.
 */
struct ParticipantDisconnectedEvent {
  /** The participant that disconnected (owned by Room). */
  RemoteParticipant *participant = nullptr;

  /** Reason for the disconnect, if known. */
  DisconnectReason reason = DisconnectReason::Unknown;
};

/**
 * Fired when a local track is successfully published.
 */
struct LocalTrackPublishedEvent {
  /** Track publication for the local track. */
  std::shared_ptr<LocalTrackPublication> publication;

  /** The published local track. */
  std::shared_ptr<Track> track;
};

/**
 * Fired when a local track is unpublished.
 */
struct LocalTrackUnpublishedEvent {
  /** Publication that was unpublished. */
  std::shared_ptr<LocalTrackPublication> publication;
};

/**
 * Fired when a local track gets its first subscriber.
 */
struct LocalTrackSubscribedEvent {
  /** Subscribed local track. */
  std::shared_ptr<Track> track;
};

/**
 * Fired when a remote participant publishes a track.
 */
struct TrackPublishedEvent {
  /** Remote track publication. */
  std::shared_ptr<RemoteTrackPublication> publication;

  /** Remote participant who owns this track (owned by Room). */
  RemoteParticipant *participant = nullptr;
};

/**
 * Fired when a remote participant unpublishes a track.
 */
struct TrackUnpublishedEvent {
  /** Remote track publication that was removed. */
  std::shared_ptr<RemoteTrackPublication> publication;

  /** Remote participant who owned this track (owned by Room). */
  RemoteParticipant *participant = nullptr;
};

/**
 * Fired when a remote track is successfully subscribed.
 */
struct TrackSubscribedEvent {
  /** Subscribed remote track. */
  std::shared_ptr<Track> track;

  /** Publication associated with the track. */
  std::shared_ptr<RemoteTrackPublication> publication;

  /** Remote participant who owns the track (owned by Room). */
  RemoteParticipant *participant = nullptr;
};

/**
 * Fired when a remote track is unsubscribed.
 */
struct TrackUnsubscribedEvent {
  /** Track that was unsubscribed. */
  std::shared_ptr<Track> track;

  /** Publication associated with the track. */
  std::shared_ptr<RemoteTrackPublication> publication;

  /** Remote participant who owns the track (owned by Room). */
  RemoteParticipant *participant = nullptr;
};

/**
 * Fired when subscribing to a remote track fails.
 */
struct TrackSubscriptionFailedEvent {
  /** Remote participant for which the subscription failed (owned by Room). */
  RemoteParticipant *participant = nullptr;

  /** SID of the track that failed to subscribe. */
  std::string track_sid;

  /** Error message describing the failure. */
  std::string error;
};

/**
 * Fired when a track is muted.
 */
struct TrackMutedEvent {
  /** Local or remote participant who owns the track (owned by Room). */
  Participant *participant = nullptr;

  /** Publication that was muted. */
  std::shared_ptr<TrackPublication> publication;
};

/**
 * Fired when a track is unmuted.
 */
struct TrackUnmutedEvent {
  /** Local or remote participant who owns the track (owned by Room). */
  Participant *participant = nullptr;

  /** Publication that was unmuted. */
  std::shared_ptr<TrackPublication> publication;
};

/**
 * Fired when the list of active speakers changes.
 */
struct ActiveSpeakersChangedEvent {
  /** Participants currently considered active speakers (owned by Room). */
  std::vector<Participant *> speakers;
};

/**
 * Fired when room metadata is updated.
 */
struct RoomMetadataChangedEvent {
  /** Previous metadata value. */
  std::string old_metadata;

  /** New metadata value. */
  std::string new_metadata;
};

/**
 * Fired when the room SID changes (e.g., after migration).
 */
struct RoomSidChangedEvent {
  /** New room SID. */
  std::string sid;
};

/**
 * Fired when a participant's metadata is updated.
 */
struct ParticipantMetadataChangedEvent {
  /** Participant whose metadata changed (owned by Room). */
  Participant *participant = nullptr;

  /** Old metadata value. */
  std::string old_metadata;

  /** New metadata value. */
  std::string new_metadata;
};

/**
 * Fired when a participant's name changes.
 */
struct ParticipantNameChangedEvent {
  /** Participant whose name changed (owned by Room). */
  Participant *participant = nullptr;

  /** Previous name. */
  std::string old_name;

  /** New name. */
  std::string new_name;
};

/**
 * Fired when a participant's attributes change.
 */
struct ParticipantAttributesChangedEvent {
  /** Participant whose attributes changed (owned by Room). */
  Participant *participant = nullptr;

  /** Set of attributes that changed (key/value pairs). */
  std::vector<AttributeEntry> changed_attributes;
};

/**
 * Fired when a participant's encryption status changes.
 */
struct ParticipantEncryptionStatusChangedEvent {
  /** Participant whose encryption status changed (owned by Room). */
  Participant *participant = nullptr;

  /** True if the participant is now fully encrypted. */
  bool is_encrypted = false;
};

/**
 * Fired when a participant's connection quality estimate changes.
 */
struct ConnectionQualityChangedEvent {
  /** Participant whose connection quality changed (owned by Room). */
  Participant *participant = nullptr;

  /** New connection quality. */
  ConnectionQuality quality = ConnectionQuality::Good;
};

/**
 * Fired when a user data packet (non-SIP) is received.
 */
struct UserDataPacketEvent {
  /** Payload data. */
  std::vector<std::uint8_t> data;

  /** Delivery kind (reliable or lossy). */
  DataPacketKind kind = DataPacketKind::Reliable;

  /** Remote participant that sent this packet, or nullptr if server (owned by
   * Room). */
  RemoteParticipant *participant = nullptr;

  /** Optional topic associated with this data (may be empty). */
  std::string topic;
};

/**
 * Fired when a SIP DTMF packet is received.
 */
struct SipDtmfReceivedEvent {
  /** DTMF code. */
  int code = 0;

  /** Human-readable DTMF digit. */
  std::string digit;

  /** Remote participant that sent the DTMF (owned by Room). */
  RemoteParticipant *participant = nullptr;
};

/**
 * One transcription unit with optional participant/track linkage.
 */
struct Transcription {
  /** Optional identity of the participant who spoke. */
  std::optional<std::string> participant_identity;

  /** Optional SID of the track associated with this transcription. */
  std::optional<std::string> track_sid;

  /** Ordered segments that make up the transcription. */
  std::vector<TranscriptionSegment> segments;
};

/**
 * Fired when a transcription result is received.
 */
struct TranscriptionReceivedEvent {
  /** Transcription segments for this update. */
  std::vector<TranscriptionSegment> segments;

  /** Local or remote participant associated with these segments (owned by
   * Room). */
  Participant *participant = nullptr;

  /** Publication of the track used for transcription, if available. */
  std::shared_ptr<TrackPublication> publication;
};

/**
 * Fired when the room's connection state changes.
 */
struct ConnectionStateChangedEvent {
  /** New connection state. */
  ConnectionState state = ConnectionState::Disconnected;
};

/**
 * Fired when the room is disconnected.
 */
struct DisconnectedEvent {
  /** Reason for disconnect, if known. */
  DisconnectReason reason = DisconnectReason::Unknown;
};

/**
 * Fired just before attempting to reconnect.
 */
struct ReconnectingEvent {};

/**
 * Fired after successfully reconnecting.
 */
struct ReconnectedEvent {};

/**
 * Fired when the room has reached end-of-stream (no more events).
 */
struct RoomEosEvent {};

/**
 * Fired when a data stream header is received.
 */
struct DataStreamHeaderReceivedEvent {
  /** Identity of the participant that sent the stream. */
  std::string participant_identity;

  /** Parsed header data. */
  DataStreamHeaderData header;
};

/**
 * Fired when a data stream chunk is received.
 */
struct DataStreamChunkReceivedEvent {
  /** Identity of the participant that sent the stream. */
  std::string participant_identity;

  /** Chunk payload and metadata. */
  DataStreamChunkData chunk;
};

/**
 * Fired when a data stream trailer is received.
 */
struct DataStreamTrailerReceivedEvent {
  /** Identity of the participant that sent the stream. */
  std::string participant_identity;

  /** Trailer metadata describing the stream termination. */
  DataStreamTrailerData trailer;
};

/**
 * Fired when a data channel's buffered amount falls below its low threshold.
 */
struct DataChannelBufferedAmountLowThresholdChangedEvent {
  /** Data channel kind (reliable or lossy). */
  DataPacketKind kind = DataPacketKind::Reliable;

  /** New threshold value in bytes. */
  std::uint64_t threshold = 0;
};

/**
 * Fired when a high-level byte stream reader is opened.
 */
struct ByteStreamOpenedEvent {
  /** Handle to the underlying byte stream reader. */
  std::uint64_t reader_handle = 0;

  /** Identity of the participant that opened the stream. */
  std::string participant_identity;
};

/**
 * Fired when a high-level text stream reader is opened.
 */
struct TextStreamOpenedEvent {
  /** Handle to the underlying text stream reader. */
  std::uint64_t reader_handle = 0;

  /** Identity of the participant that opened the stream. */
  std::string participant_identity;
};

/**
 * Fired when the room's info is updated.
 */
struct RoomUpdatedEvent {
  /** New room info snapshot. */
  RoomInfoData info;
};

/**
 * Fired when the participant has been moved to another room.
 */
struct RoomMovedEvent {
  /** Info about the new room. */
  RoomInfoData info;
};

/**
 * Fired when a batch of participants has been updated.
 */
struct ParticipantsUpdatedEvent {
  /** Participants updated in this event (owned by Room). */
  std::vector<Participant *> participants;
};

/**
 * Fired when a participant's E2EE state changes.
 */
struct E2eeStateChangedEvent {
  /** Local or remote participant whose state changed (owned by Room). */
  Participant *participant = nullptr;

  /** New encryption state. */
  EncryptionState state = EncryptionState::New;
};

/**
 * Fired when a chat message is received.
 */
struct ChatMessageReceivedEvent {
  /** Chat message payload. */
  ChatMessageData message;

  /** Identity of the participant who sent the message. */
  std::string participant_identity;
};

} // namespace livekit
