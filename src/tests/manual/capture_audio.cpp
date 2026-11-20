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

#include <livekit/capture/capture_source.h>
#include <livekit/livekit.h>

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace std::chrono_literals;

volatile std::sig_atomic_t interrupted = 0;

void onSignal(int /*signal*/) { interrupted = 1; }

struct Options {
  int seconds = 10;
  bool publish = false;
  std::string pipeline =
      "osxaudiosrc ! audioconvert ! audioresample ! "
      "audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=1 ! "
      "appsink name=lk_audio_appsink";
};

void printUsage() {
  std::cout << "Usage: capture_audio [--seconds 1..600] [--publish] [--pipeline DESCRIPTION]\n"
               "Captures the default macOS microphone at 48 kHz, mono, S16LE.\n"
               "Stops after 10 seconds by default, or on Ctrl+C.\n"
               "--publish uses LIVEKIT_URL and LIVEKIT_TOKEN (or LIVEKIT_TOKEN_A).\n"
               "--pipeline overrides the microphone pipeline, e.g. for audiotestsrc.\n";
}

Options parseOptions(int argc, char* argv[]) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--publish") {
      options.publish = true;
    } else if ((argument == "--seconds" || argument == "--pipeline") && index + 1 < argc) {
      const std::string_view value(argv[++index]);
      if (argument == "--pipeline") {
        if (value.empty()) {
          throw std::invalid_argument("--pipeline must not be empty");
        }
        options.pipeline = value;
      } else {
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), options.seconds);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || options.seconds < 1 ||
            options.seconds > 600) {
          throw std::invalid_argument("--seconds must be an integer from 1 to 600");
        }
      }
    } else {
      throw std::invalid_argument("Unknown option or missing value: " + std::string(argument));
    }
  }
  return options;
}

void publish(livekit::Room& room, const std::shared_ptr<livekit::LocalAudioTrack>& track) {
  const char* url = std::getenv("LIVEKIT_URL");
  const char* token = std::getenv("LIVEKIT_TOKEN");
  if (token == nullptr || *token == '\0') {
    token = std::getenv("LIVEKIT_TOKEN_A");
  }
  if (url == nullptr || *url == '\0' || token == nullptr || *token == '\0') {
    throw std::runtime_error("--publish requires LIVEKIT_URL and LIVEKIT_TOKEN (or LIVEKIT_TOKEN_A)");
  }
  if (!room.connect(url, token, {})) {
    throw std::runtime_error("Could not connect to the LiveKit room");
  }
  const auto participant = room.localParticipant().lock();
  if (!participant) {
    throw std::runtime_error("Connected room has no local participant");
  }
  livekit::TrackPublishOptions options;
  options.source = livekit::TrackSource::SOURCE_MICROPHONE;
  participant->publishTrack(track, options);
  std::cout << "Published capture-audio; join the same room with another participant to listen.\n";
}

int capture(const Options& options) {
  livekit::capture::GstreamerAudioSourceConfig config;
  config.pipeline = options.pipeline;
  std::cout << "Opening audio pipeline...\n";
  auto pending = livekit::capture::GstreamerAudioCaptureSource::create(config);
  if (pending.wait_for(10s) != std::future_status::ready) {
    throw std::runtime_error("Audio source creation timed out");
  }
  const auto created = pending.get();
  if (!created) {
    throw std::runtime_error(created.error().message);
  }
  const auto& source = created.value();
  const auto track = livekit::LocalAudioTrack::createLocalAudioTrack("capture-audio", source->audioSource());
  livekit::Room room;
  if (options.publish) {
    publish(room, track);
  }
  const auto format = source->format();
  std::cout << "Capturing " << format.sample_rate << " Hz, " << format.num_channels << " channel(s), "
            << format.samples_per_channel << " samples/frame for up to " << options.seconds
            << " seconds. Press Ctrl+C to stop.\n";
  source->start().value();
  auto finished = source->join();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options.seconds);
  while (finished.wait_for(100ms) != std::future_status::ready) {
    if (interrupted != 0 || std::chrono::steady_clock::now() >= deadline) {
      source->stop().value();
      break;
    }
  }
  if (finished.wait_for(5s) != std::future_status::ready) {
    throw std::runtime_error("Audio capture shutdown timed out");
  }
  const auto result = finished.get();
  if (!result) {
    throw std::runtime_error(result.error().message);
  }
  const auto& stats = result.value();
  std::cout << "Captured " << stats.frames_captured << " frames (" << static_cast<double>(stats.frames_captured) / 100.0
            << " seconds of audio).\n";
  if (stats.frames_captured == 0) {
    throw std::runtime_error("No audio frames captured; check microphone permission and the selected input device");
  }
  return 0;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
    printUsage();
    return 0;
  }
  int exit_code = 1;
  bool initialized = false;
  try {
    const auto options = parseOptions(argc, argv);
    if (std::signal(SIGINT, onSignal) == SIG_ERR || std::signal(SIGTERM, onSignal) == SIG_ERR) {
      throw std::runtime_error("Could not install signal handlers");
    }
    initialized = livekit::initialize(livekit::LogLevel::Warn);
    if (!initialized) {
      throw std::runtime_error("Could not initialize LiveKit");
    }
    exit_code = capture(options);
  } catch (const std::exception& error) {
    std::cerr << "capture_audio: " << error.what() << '\n';
  }
  if (initialized) {
    livekit::shutdown();
  }
  return exit_code;
}
