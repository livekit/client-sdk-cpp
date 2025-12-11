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

#include "livekit/room_event_types.h"

namespace livekit {

class Room;

/**
 * Interface for receiving room-level events.
 *
 * Implement this class and pass an instance to Room::setDelegate()
 * to be notified about participants, tracks, data, and connection changes.
 *
 * All methods provide default no-op implementations so you can override
 * only the callbacks you care about.
 */
class RoomDelegate {
public:
  virtual ~RoomDelegate() = default;

  // ------------------------------------------------------------------
  // Participant lifecycle
  // ------------------------------------------------------------------

  /**
   * Called when a new remote participant joins the room.
   */
  virtual void onParticipantConnected(Room &,
                                      const ParticipantConnectedEvent &) {}

  /**
   * Called when a remote participant leaves the room.
   */
  virtual void onParticipantDisconnected(Room &,
                                         const ParticipantDisconnectedEvent &) {
  }

  // ------------------------------------------------------------------
  // Local track publication events
  // ------------------------------------------------------------------

  /**
   * Called when a local track is successfully published.
   */
  virtual void onLocalTrackPublished(Room &, const LocalTrackPublishedEvent &) {
  }

  /**
   * Called when a local track is unpublished.
   */
  virtual void onLocalTrackUnpublished(Room &,
                                       const LocalTrackUnpublishedEvent &) {}

  /**
   * Called when a local track gains its first subscriber.
   */
  virtual void onLocalTrackSubscribed(Room &,
                                      const LocalTrackSubscribedEvent &) {}

  // ------------------------------------------------------------------
  // Remote track publication/subscription
  // ------------------------------------------------------------------

  /**
   * Called when a remote participant publishes a track.
   */
  virtual void onTrackPublished(Room &, const TrackPublishedEvent &) {}

  /**
   * Called when a remote participant unpublishes a track.
   */
  virtual void onTrackUnpublished(Room &, const TrackUnpublishedEvent &) {}

  /**
   * Called when a remote track is successfully subscribed.
   */
  virtual void onTrackSubscribed(Room &, const TrackSubscribedEvent &) {}

  /**
   * Called when a remote track is unsubscribed.
   */
  virtual void onTrackUnsubscribed(Room &, const TrackUnsubscribedEvent &) {}

  /**
   * Called when subscribing to a remote track fails.
   */
  virtual void onTrackSubscriptionFailed(Room &,
                                         const TrackSubscriptionFailedEvent &) {
  }

  /**
   * Called when a track is muted.
   */
  virtual void onTrackMuted(Room &, const TrackMutedEvent &) {}

  /**
   * Called when a track is unmuted.
   */
  virtual void onTrackUnmuted(Room &, const TrackUnmutedEvent &) {}

  // ------------------------------------------------------------------
  // Active speakers
  // ------------------------------------------------------------------

  /**
   * Called when the list of active speakers changes.
   */
  virtual void onActiveSpeakersChanged(Room &,
                                       const ActiveSpeakersChangedEvent &) {}

  // ------------------------------------------------------------------
  // Room info / metadata
  // ------------------------------------------------------------------

  /**
   * Called when the room's metadata changes.
   */
  virtual void onRoomMetadataChanged(Room &, const RoomMetadataChangedEvent &) {
  }

  /**
   * Called when the room SID changes (e.g., after migration).
   */
  virtual void onRoomSidChanged(Room &, const RoomSidChangedEvent &) {}

  /**
   * Called when any room info is updated.
   */
  virtual void onRoomUpdated(Room &, const RoomUpdatedEvent &) {}

  /**
   * Called when the participant is moved to another room.
   */
  virtual void onRoomMoved(Room &, const RoomMovedEvent &) {}

  // ------------------------------------------------------------------
  // Participant info changes
  // ------------------------------------------------------------------

  /**
   * Called when a participant's metadata is updated.
   */
  virtual void
  onParticipantMetadataChanged(Room &,
                               const ParticipantMetadataChangedEvent &) {}

  /**
   * Called when a participant's name is changed.
   */
  virtual void onParticipantNameChanged(Room &,
                                        const ParticipantNameChangedEvent &) {}

  /**
   * Called when a participant's attributes are updated.
   */
  virtual void
  onParticipantAttributesChanged(Room &,
                                 const ParticipantAttributesChangedEvent &) {}

  /**
   * Called when a participant's encryption status changes.
   */
  virtual void onParticipantEncryptionStatusChanged(
      Room &, const ParticipantEncryptionStatusChangedEvent &) {}

  // ------------------------------------------------------------------
  // Connection quality / state
  // ------------------------------------------------------------------

  /**
   * Called when a participant's connection quality changes.
   */
  virtual void
  onConnectionQualityChanged(Room &, const ConnectionQualityChangedEvent &) {}

  /**
   * Called when the room's connection state changes.
   */
  virtual void onConnectionStateChanged(Room &,
                                        const ConnectionStateChangedEvent &) {}

  /**
   * Called when the room is disconnected.
   */
  virtual void onDisconnected(Room &, const DisconnectedEvent &) {}

  /**
   * Called before the SDK attempts to reconnect.
   */
  virtual void onReconnecting(Room &, const ReconnectingEvent &) {}

  /**
   * Called after the SDK successfully reconnects.
   */
  virtual void onReconnected(Room &, const ReconnectedEvent &) {}

  // ------------------------------------------------------------------
  // E2EE
  // ------------------------------------------------------------------

  /**
   * Called when a participant's end-to-end encryption state changes.
   */
  virtual void onE2eeStateChanged(Room &, const E2eeStateChangedEvent &) {}

  // ------------------------------------------------------------------
  // EOS
  // ------------------------------------------------------------------

  /**
   * Called when the room reaches end-of-stream and will not emit further
   * events.
   */
  virtual void onRoomEos(Room &, const RoomEosEvent &) {}

  // ------------------------------------------------------------------
  // Data / transcription / chat
  // ------------------------------------------------------------------

  /**
   * Called when a user data packet (non-SIP) is received.
   */
  virtual void onUserPacketReceived(Room &, const UserDataPacketEvent &) {}

  /**
   * Called when a SIP DTMF packet is received.
   */
  virtual void onSipDtmfReceived(Room &, const SipDtmfReceivedEvent &) {}

  // ------------------------------------------------------------------
  // Data streams
  // ------------------------------------------------------------------

  /**
   * Called when a data stream header is received.
   */
  virtual void
  onDataStreamHeaderReceived(Room &, const DataStreamHeaderReceivedEvent &) {}

  /**
   * Called when a data stream chunk is received.
   */
  virtual void onDataStreamChunkReceived(Room &,
                                         const DataStreamChunkReceivedEvent &) {
  }

  /**
   * Called when a data stream trailer is received.
   */
  virtual void
  onDataStreamTrailerReceived(Room &, const DataStreamTrailerReceivedEvent &) {}

  /**
   * Called when a data channel's buffered amount falls below its low threshold.
   */
  virtual void onDataChannelBufferedAmountLowThresholdChanged(
      Room &, const DataChannelBufferedAmountLowThresholdChangedEvent &) {}

  // ------------------------------------------------------------------
  // High-level byte/text streams
  // ------------------------------------------------------------------

  /**
   * Called when a high-level byte stream reader is opened.
   */
  virtual void onByteStreamOpened(Room &, const ByteStreamOpenedEvent &) {}

  /**
   * Called when a high-level text stream reader is opened.
   */
  virtual void onTextStreamOpened(Room &, const TextStreamOpenedEvent &) {}

  // ------------------------------------------------------------------
  // Participants snapshot
  // ------------------------------------------------------------------

  /**
   * Called when a snapshot of participants has been updated.
   */
  virtual void onParticipantsUpdated(Room &, const ParticipantsUpdatedEvent &) {
  }
};

} // namespace livekit
