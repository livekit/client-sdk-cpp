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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <livekit/livekit.h>
#if LIVEKIT_CAPTURE_ENABLED
#include <livekit/capture/capture_source.h>
#endif

#include <chrono>
#include <exception>
#include <iostream>
#include <string_view>

#if LIVEKIT_CAPTURE_ENABLED
namespace {

// Omitted backends must reject a valid configuration with the feature-disabled error.
bool checkSourceError(std::string_view name, std::string_view message, bool expected,
                      livekit::capture::CaptureSourceKind kind) {
  constexpr std::string_view disabled = "capture source is not enabled in this build";
  if (!expected && message.find(disabled) != std::string_view::npos) {
    return true;
  }
  if (expected && kind == livekit::capture::CaptureSourceKind::Pixel && !LIVEKIT_VERIFY_PIXEL_SOURCES &&
      message.find("no compatible GPU adapter") != std::string_view::npos) {
    std::cout << name << ": creation not exercised (no GPU adapter); backend availability verified\n";
    return true;
  }
  std::cerr << name << ": " << message << '\n';
  return false;
}

template <typename Config>
bool checkSource(std::string_view name, const Config& config, bool expected, livekit::capture::CaptureSourceKind kind) {
  try {
    auto future = livekit::capture::CaptureSource::create(config);
    if (future.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
      std::cerr << name << ": source creation timed out\n";
      return false;
    }
    auto created = future.get();
    if (!created) {
      return checkSourceError(name, created.error().message, expected, kind);
    }
    const auto& source = created.value();
    if (!expected || !source || source->kind() != kind || source->width() != 320 || source->height() != 240 ||
        !source->videoSource() ||
        (kind == livekit::capture::CaptureSourceKind::Encoded && source->codec() != livekit::VideoCodec::VP8)) {
      std::cerr << name << ": unexpected source availability or metadata\n";
      return false;
    }
    return true;
  } catch (const std::exception& error) {
    return checkSourceError(name, error.what(), expected, kind);
  }
}

bool checkSources() {
  livekit::capture::GstreamerVideoSourceConfig gstreamer;
  gstreamer.pipeline =
      "videotestsrc num-buffers=4 ! video/x-raw,width=320,height=240,framerate=30/1 ! "
      "vp8enc deadline=1 ! appsink name=lk_appsink";
  bool success =
      checkSource("gstreamer", gstreamer, LIVEKIT_EXPECT_GSTREAMER != 0, livekit::capture::CaptureSourceKind::Encoded);

  livekit::capture::PatternVideoSourceConfig pattern;
  pattern.resolution = {320, 240};
  pattern.framerate_fps = 30;
  success = checkSource("pattern", pattern, LIVEKIT_EXPECT_PATTERN != 0, livekit::capture::CaptureSourceKind::Pixel) &&
            success;

  livekit::capture::ClockVideoSourceConfig clock;
  clock.resolution = {320, 240};
  clock.framerate_fps = 30;
  success =
      checkSource("clock", clock, LIVEKIT_EXPECT_CLOCK != 0, livekit::capture::CaptureSourceKind::Pixel) && success;
  return success;
}

} // namespace
#endif

int main() {
#if LIVEKIT_CAPTURE_ENABLED
  livekit::initialize(livekit::LogLevel::Warn);
  const bool success = checkSources();
  livekit::shutdown();
  return success ? 0 : 1;
#else
  return 0;
#endif
}
