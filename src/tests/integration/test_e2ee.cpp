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

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../common/audio_utils.h"
#include "../common/test_common.h"

namespace livekit::test {

class E2EEIntegrationTest : public LiveKitTestBase {};

TEST_F(E2EEIntegrationTest, FrameCryptorsIncludePublishedTrack) {
  failIfNotConfigured();

  Room room;
  RoomOptions room_options;
  room_options.encryption = E2EEOptions{};
  room_options.encryption->key_provider_options.shared_key = std::vector<std::uint8_t>{'k', 'e', 'y'};
  ASSERT_TRUE(room.connect(config_.url, config_.token_a, room_options));
  auto participant = lockLocalParticipant(room);

  auto source = std::make_shared<AudioSource>(kDefaultAudioSampleRate, kDefaultAudioChannels, 0);
  auto track = participant->publishAudioTrack("e2ee-frame-cryptors", source, TrackSource::SOURCE_MICROPHONE);
  auto manager = room.e2eeManager().lock();
  ASSERT_NE(manager, nullptr);

  std::vector<E2EEManager::FrameCryptor> cryptors;
  ASSERT_NO_THROW(cryptors = manager->frameCryptors());
  const bool found = std::any_of(cryptors.begin(), cryptors.end(), [&](const auto& cryptor) {
    return cryptor.participantIdentity() == participant->identity();
  });
  EXPECT_TRUE(found) << "No frame cryptor for the local participant's track";
  participant->unpublishTrack(track->publication()->sid());
}

} // namespace livekit::test
