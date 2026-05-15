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
#include <livekit/local_audio_track.h>
#include <livekit/local_data_track.h>
#include <livekit/local_video_track.h>
#include <livekit/track.h>

#include <memory>
#include <string>
#include <type_traits>

namespace livekit::test {

TEST(LocalTracksTest, LocalAudioTrackFactoryIsAddressable) {
  using FactoryT = std::shared_ptr<LocalAudioTrack> (*)(const std::string&, const std::shared_ptr<AudioSource>&);
  FactoryT factory = &LocalAudioTrack::createLocalAudioTrack;
  EXPECT_NE(factory, nullptr);
}

TEST(LocalTracksTest, LocalVideoTrackFactoryIsAddressable) {
  using FactoryT = std::shared_ptr<LocalVideoTrack> (*)(const std::string&, const std::shared_ptr<VideoSource>&);
  FactoryT factory = &LocalVideoTrack::createLocalVideoTrack;
  EXPECT_NE(factory, nullptr);
}

TEST(LocalTracksTest, LocalTrackInheritsFromTrack) {
  static_assert(std::is_base_of_v<Track, LocalAudioTrack>);
  static_assert(std::is_base_of_v<Track, LocalVideoTrack>);
  SUCCEED();
}

TEST(LocalTracksTest, LocalDataTrackIsNoncopyable) {
  static_assert(!std::is_copy_constructible_v<LocalDataTrack>);
  static_assert(!std::is_copy_assignable_v<LocalDataTrack>);
  SUCCEED();
}

} // namespace livekit::test
