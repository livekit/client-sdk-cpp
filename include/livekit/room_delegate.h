#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace livekit {

class Room;

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
  // mirror your proto DisconnectReason values as needed
  Unknown,
  ClientInitiated,
  ServerInitiated,
};

// ---------------------------------------------------------
// Basic data types corresponding to proto messages
// ---------------------------------------------------------

struct TranscriptionSegmentData {
  std::string id;
  std::string text;
  std::uint64_t start_time = 0;
  std::uint64_t end_time = 0;
  bool is_final = false;
  std::string language;
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
  std::optional<std::string> topic;  // optional
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

// ---------------------------------------------------------
// Event structs – “public” representations of RoomEvent.*
// ---------------------------------------------------------

struct ParticipantConnectedEvent {
  // Typically you’d also attach a handle / participant object
  std::string identity;           // from OwnedParticipant / ParticipantInfo
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
  std::string participant_identity;  // may be empty
  std::optional<UserPacketData> user;
  std::optional<SipDtmfData> sip_dtmf;
};

struct TranscriptionReceivedEvent {
  std::optional<std::string> participant_identity;
  std::optional<std::string> track_sid;
  std::vector<TranscriptionSegmentData> segments;
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
  std::uint64_t reader_handle = 0;  // from OwnedByteStreamReader.handle
  std::string participant_identity;
};

struct TextStreamOpenedEvent {
  std::uint64_t reader_handle = 0;  // from OwnedTextStreamReader.handle
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
  virtual void onRoomEvent(Room& /*room*/) {}

  // Per-event callbacks. All default no-op so you can add more later
  // without breaking existing user code.

  // Participant lifecycle
  virtual void onParticipantConnected(Room& room,
                                      const ParticipantConnectedEvent& ev) {}
  virtual void onParticipantDisconnected(Room& room,
                                         const ParticipantDisconnectedEvent& ev) {}

  // Local track publication
  virtual void onLocalTrackPublished(Room& room,
                                     const LocalTrackPublishedEvent& ev) {}
  virtual void onLocalTrackUnpublished(Room& room,
                                       const LocalTrackUnpublishedEvent& ev) {}
  virtual void onLocalTrackSubscribed(Room& room,
                                      const LocalTrackSubscribedEvent& ev) {}

  // Remote track publication/subscription
  virtual void onTrackPublished(Room& room,
                                const TrackPublishedEvent& ev) {}
  virtual void onTrackUnpublished(Room& room,
                                  const TrackUnpublishedEvent& ev) {}
  virtual void onTrackSubscribed(Room& room,
                                 const TrackSubscribedEvent& ev) {}
  virtual void onTrackUnsubscribed(Room& room,
                                   const TrackUnsubscribedEvent& ev) {}
  virtual void onTrackSubscriptionFailed(Room& room,
                                         const TrackSubscriptionFailedEvent& ev) {}
  virtual void onTrackMuted(Room& room,
                            const TrackMutedEvent& ev) {}
  virtual void onTrackUnmuted(Room& room,
                              const TrackUnmutedEvent& ev) {}

  // Active speakers
  virtual void onActiveSpeakersChanged(Room& room,
                                       const ActiveSpeakersChangedEvent& ev) {}

  // Room info / metadata
  virtual void onRoomMetadataChanged(Room& room,
                                     const RoomMetadataChangedEvent& ev) {}
  virtual void onRoomSidChanged(Room& room,
                                const RoomSidChangedEvent& ev) {}
  virtual void onRoomUpdated(Room& room,
                             const RoomUpdatedEvent& ev) {}
  virtual void onRoomMoved(Room& room,
                           const RoomMovedEvent& ev) {}

  // Participant info changes
  virtual void onParticipantMetadataChanged(Room& room,
                                            const ParticipantMetadataChangedEvent& ev) {}
  virtual void onParticipantNameChanged(Room& room,
                                        const ParticipantNameChangedEvent& ev) {}
  virtual void onParticipantAttributesChanged(Room& room,
                                              const ParticipantAttributesChangedEvent& ev) {}
  virtual void onParticipantEncryptionStatusChanged(Room& room,
                                                    const ParticipantEncryptionStatusChangedEvent& ev) {}

  // Connection quality / state
  virtual void onConnectionQualityChanged(Room& room,
                                          const ConnectionQualityChangedEvent& ev) {}
  virtual void onConnectionStateChanged(Room& room,
                                        const ConnectionStateChangedEvent& ev) {}
  virtual void onDisconnected(Room& room,
                              const DisconnectedEvent& ev) {}
  virtual void onReconnecting(Room& room,
                              const ReconnectingEvent& ev) {}
  virtual void onReconnected(Room& room,
                             const ReconnectedEvent& ev) {}

  // E2EE
  virtual void onE2eeStateChanged(Room& room,
                                  const E2eeStateChangedEvent& ev) {}

  // EOS
  virtual void onRoomEos(Room& room,
                         const RoomEosEvent& ev) {}

  // Data / transcription / chat
  virtual void onDataPacketReceived(Room& room,
                                    const DataPacketReceivedEvent& ev) {}
  virtual void onTranscriptionReceived(Room& room,
                                       const TranscriptionReceivedEvent& ev) {}
  virtual void onChatMessageReceived(Room& room,
                                     const ChatMessageReceivedEvent& ev) {}

  // Data streams
  virtual void onDataStreamHeaderReceived(Room& room,
                                          const DataStreamHeaderReceivedEvent& ev) {}
  virtual void onDataStreamChunkReceived(Room& room,
                                         const DataStreamChunkReceivedEvent& ev) {}
  virtual void onDataStreamTrailerReceived(Room& room,
                                           const DataStreamTrailerReceivedEvent& ev) {}
  virtual void onDataChannelBufferedAmountLowThresholdChanged(
      Room& room,
      const DataChannelBufferedAmountLowThresholdChangedEvent& ev) {}

  // High-level byte/text streams
  virtual void onByteStreamOpened(Room& room,
                                  const ByteStreamOpenedEvent& ev) {}
  virtual void onTextStreamOpened(Room& room,
                                  const TextStreamOpenedEvent& ev) {}

  // Participants snapshot
  virtual void onParticipantsUpdated(Room& room,
                                     const ParticipantsUpdatedEvent& ev) {}
};

}  // namespace livekit
