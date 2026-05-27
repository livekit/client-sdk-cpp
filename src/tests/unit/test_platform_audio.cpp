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
#include <livekit/livekit.h>
#include <livekit/local_audio_track.h>
#include <livekit/platform_audio.h>

#include <memory>

namespace livekit::test {

class PlatformAudioTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogLevel::Info); }
  void TearDown() override { livekit::shutdown(); }
};

TEST_F(PlatformAudioTest, DefaultOptionsEnableVoiceProcessing) {
  PlatformAudioOptions options;
  EXPECT_TRUE(options.echo_cancellation);
  EXPECT_TRUE(options.noise_suppression);
  EXPECT_TRUE(options.auto_gain_control);
  EXPECT_FALSE(options.prefer_hardware);
}

TEST_F(PlatformAudioTest, DeviceInfoStoresStableId) {
  AudioDeviceInfo device;
  device.index = 1;
  device.name = "Microphone";
  device.id = "device-guid";

  EXPECT_EQ(device.index, 1u);
  EXPECT_EQ(device.name, "Microphone");
  EXPECT_EQ(device.id, "device-guid");
}

TEST_F(PlatformAudioTest, CreateSourceAndTrackWhenAvailable) {
  std::unique_ptr<PlatformAudio> platform_audio;
  try {
    platform_audio = std::make_unique<PlatformAudio>();
  } catch (const PlatformAudioError& error) {
    GTEST_SKIP() << "PlatformAudio unavailable: " << error.what();
  }

  const auto source = platform_audio->createAudioSource();
  ASSERT_NE(source, nullptr);
  EXPECT_NE(source->ffiHandleId(), 0u);

  const auto track = LocalAudioTrack::createLocalAudioTrack("platform-mic", source);
  ASSERT_NE(track, nullptr);
  EXPECT_EQ(track->name(), "platform-mic");
  EXPECT_EQ(track->kind(), TrackKind::KIND_AUDIO);
}

TEST_F(PlatformAudioTest, MovedFromManagerThrowsOnUseButCountsAreSafe) {
  std::unique_ptr<PlatformAudio> platform_audio;
  try {
    platform_audio = std::make_unique<PlatformAudio>();
  } catch (const PlatformAudioError& error) {
    GTEST_SKIP() << "PlatformAudio unavailable: " << error.what();
  }

  PlatformAudio moved_to = std::move(*platform_audio);
  PlatformAudio& moved_from = *platform_audio;

  // The moved-to manager keeps the FFI handle and remains usable.
  EXPECT_NO_THROW({ (void)moved_to.recordingDevices(); });

  // The noexcept count accessors fall back to 0 on the emptied state.
  EXPECT_EQ(moved_from.recordingDeviceCount(), 0);
  EXPECT_EQ(moved_from.playoutDeviceCount(), 0);

  // Device operations on the emptied state must surface a clear error rather
  // than dereferencing a null handle.
  EXPECT_THROW((void)moved_from.recordingDevices(), PlatformAudioError);
  EXPECT_THROW((void)moved_from.playoutDevices(), PlatformAudioError);
  EXPECT_THROW(moved_from.setRecordingDevice("device-id"), PlatformAudioError);
  EXPECT_THROW(moved_from.setPlayoutDevice("device-id"), PlatformAudioError);
}

TEST_F(PlatformAudioTest, CopySharesHandleStateAndOutlivesOriginal) {
  std::unique_ptr<PlatformAudio> platform_audio;
  try {
    platform_audio = std::make_unique<PlatformAudio>();
  } catch (const PlatformAudioError& error) {
    GTEST_SKIP() << "PlatformAudio unavailable: " << error.what();
  }

  // A copy shares the underlying handle, so the cached counts agree.
  PlatformAudio copy = *platform_audio;
  EXPECT_EQ(copy.recordingDeviceCount(), platform_audio->recordingDeviceCount());
  EXPECT_EQ(copy.playoutDeviceCount(), platform_audio->playoutDeviceCount());

  // A source created from the copy keeps the shared state alive after the
  // original manager is destroyed.
  const auto source = copy.createAudioSource();
  ASSERT_NE(source, nullptr);
  EXPECT_NE(source->ffiHandleId(), 0u);

  platform_audio.reset();
  EXPECT_NE(source->ffiHandleId(), 0u);
}

TEST_F(PlatformAudioTest, CreateSourceWithCustomOptions) {
  std::unique_ptr<PlatformAudio> platform_audio;
  try {
    platform_audio = std::make_unique<PlatformAudio>();
  } catch (const PlatformAudioError& error) {
    GTEST_SKIP() << "PlatformAudio unavailable: " << error.what();
  }

  PlatformAudioOptions options;
  options.echo_cancellation = false;
  options.noise_suppression = false;
  options.auto_gain_control = false;
  options.prefer_hardware = true;

  const auto source = platform_audio->createAudioSource(options);
  ASSERT_NE(source, nullptr);
  EXPECT_NE(source->ffiHandleId(), 0u);
}

TEST_F(PlatformAudioTest, EnumerateDevicesAndSelectWhenAvailable) {
  std::unique_ptr<PlatformAudio> platform_audio;
  try {
    platform_audio = std::make_unique<PlatformAudio>();
  } catch (const PlatformAudioError& error) {
    GTEST_SKIP() << "PlatformAudio unavailable: " << error.what();
  }

  // Enumeration must succeed even on headless runners (it may return empty).
  std::vector<AudioDeviceInfo> recording_devices;
  std::vector<AudioDeviceInfo> playout_devices;
  EXPECT_NO_THROW({ recording_devices = platform_audio->recordingDevices(); });
  EXPECT_NO_THROW({ playout_devices = platform_audio->playoutDevices(); });

  // Selecting a real device by its stable id must not throw. Headless runners
  // usually report no devices, so guard the assertion behind availability.
  bool selected_any = false;
  for (const auto& device : recording_devices) {
    if (!device.id.empty()) {
      EXPECT_NO_THROW(platform_audio->setRecordingDevice(device.id));
      selected_any = true;
      break;
    }
  }
  for (const auto& device : playout_devices) {
    if (!device.id.empty()) {
      EXPECT_NO_THROW(platform_audio->setPlayoutDevice(device.id));
      selected_any = true;
      break;
    }
  }

  if (!selected_any) {
    GTEST_SKIP() << "No audio devices with stable ids available to select";
  }
}

} // namespace livekit::test
