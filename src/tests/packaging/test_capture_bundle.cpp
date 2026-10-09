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

#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  if (argc != 2 ||
      (std::string_view(argv[1]) != "--expect-capture=ON" && std::string_view(argv[1]) != "--expect-capture=OFF")) {
    std::cerr << "Usage: livekit_capture_package_test --expect-capture=ON|OFF\n";
    return 2;
  }
  const bool expect_capture = std::string_view(argv[1]) == "--expect-capture=ON";
  if ((LIVEKIT_CAPTURE_ENABLED != 0) != expect_capture) {
    std::cerr << "Installed SDK capture metadata does not match the requested expectation\n";
    return 1;
  }

  livekit::initialize(livekit::LogLevel::Warn);
  int status = 0;
  try {
    livekit::GstreamerVideoSourceConfig config;
    config.pipeline =
        "videotestsrc num-buffers=4 ! video/x-raw,width=320,height=240,framerate=30/1 ! "
        "vp8enc deadline=1 ! appsink name=lk_appsink";
    auto source = livekit::CaptureSource::create(config).get();
    if (!expect_capture || !source || source->kind() != livekit::CaptureSourceKind::Encoded || source->width() != 320 ||
        source->height() != 240 || source->codec() != livekit::VideoCodec::VP8 || !source->videoSource()) {
      std::cerr << "Installed capture SDK returned unexpected source metadata\n";
      status = 1;
    }
  } catch (const livekit::CaptureSourceError& error) {
    if (expect_capture) {
      std::cerr << "Installed capture SDK failed: " << error.what() << '\n';
      status = 1;
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    status = 1;
  }
  livekit::shutdown();
  return status;
}
