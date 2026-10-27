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

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../common/audio_utils.h"
#include "../common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

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

  // The track's cryptor may be created after publishing returns.
  bool found = false;
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!found && std::chrono::steady_clock::now() < deadline) {
    std::vector<E2EEManager::FrameCryptor> cryptors;
    ASSERT_NO_THROW(cryptors = manager->frameCryptors());
    for (const auto& cryptor : cryptors) {
      found = found || cryptor.participantIdentity() == participant->identity();
    }
    if (!found) {
      std::this_thread::sleep_for(50ms);
    }
  }
  EXPECT_TRUE(found) << "No frame cryptor for the local participant's track";
  participant->unpublishTrack(track->publication()->sid());
}

} // namespace livekit::test
