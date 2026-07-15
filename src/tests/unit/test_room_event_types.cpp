/*
 * Copyright 2026 LiveKit
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

#include <gtest/gtest.h>
#include <livekit/room_event_types.h>

#include <string>

namespace livekit::test {

TEST(RoomEventTypesTest, EnumValuesAreReachable) {
  EXPECT_NE(ConnectionState::Connected, ConnectionState::Disconnected);
  EXPECT_NE(DataPacketKind::Reliable, DataPacketKind::Lossy);
  EXPECT_NE(EncryptionState::New, EncryptionState::Ok);
  EXPECT_NE(DisconnectReason::Unknown, DisconnectReason::ClientInitiated);
  EXPECT_NE(DisconnectReason::MediaFailure, DisconnectReason::AgentError);
  EXPECT_NE(ConnectionQuality::Poor, ConnectionQuality::Excellent);
}

TEST(RoomEventTypesTest, RoomInfoDataDefaults) {
  RoomInfoData info;
  EXPECT_TRUE(info.name.empty());
  EXPECT_TRUE(info.metadata.empty());
  EXPECT_FALSE(info.active_recording);
  EXPECT_EQ(info.creation_time, 0);
}

TEST(RoomEventTypesTest, AttributeEntryConstruction) {
  AttributeEntry entry("k", "v");
  EXPECT_EQ(entry.key, "k");
  EXPECT_EQ(entry.value, "v");
}

TEST(RoomEventTypesTest, TrackPublishOptionsDefaults) {
  TrackPublishOptions options;
  EXPECT_FALSE(options.frame_metadata_features.has_value());
  EXPECT_FALSE(options.packet_trailer_features.user_timestamp);
  EXPECT_FALSE(options.packet_trailer_features.frame_id);
  EXPECT_FALSE(options.packet_trailer_features.user_data);
  EXPECT_FALSE(options.degradation_preference.has_value());
}

TEST(RoomEventTypesTest, UserPacketDataDefaults) {
  UserPacketData packet;
  EXPECT_TRUE(packet.data.empty());
  EXPECT_FALSE(packet.topic.has_value());
}

TEST(RoomEventTypesTest, TokenRefreshedEventDefaults) {
  TokenRefreshedEvent event;
  EXPECT_TRUE(event.token.empty());
}

} // namespace livekit::test
