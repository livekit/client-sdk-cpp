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

#include <livekit/livekit.h>

#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct Options {
  std::uint64_t iterations{100};
  std::uint64_t warmup{20};
  std::uint64_t max_rss_growth_kib{std::uint64_t{8} * 1024U};
  std::uint64_t max_thread_growth{0};
};

struct ProcessSample {
  std::uint64_t rss_kib{};
  std::uint64_t threads{};
};

std::uint64_t parseUnsigned(std::string_view value, std::string_view option) {
  std::uint64_t parsed = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    throw std::runtime_error("invalid value for " + std::string(option));
  }
  return parsed;
}

Options parseOptions(int argc, char* argv[]) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view option(argv[index]);
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value for " + std::string(option));
    }
    const std::string_view value(argv[++index]);
    if (option == "--iterations") {
      options.iterations = parseUnsigned(value, option);
    } else if (option == "--warmup") {
      options.warmup = parseUnsigned(value, option);
    } else if (option == "--max-rss-growth-kib") {
      options.max_rss_growth_kib = parseUnsigned(value, option);
    } else if (option == "--max-thread-growth") {
      options.max_thread_growth = parseUnsigned(value, option);
    } else {
      throw std::runtime_error("unknown option: " + std::string(option));
    }
  }
  if (options.warmup == 0 || options.warmup >= options.iterations) {
    throw std::runtime_error("--warmup must be between 1 and --iterations - 1");
  }
  return options;
}

std::uint64_t statusValue(std::string_view line) {
  const auto separator = line.find(':');
  if (separator == std::string_view::npos) {
    throw std::runtime_error("malformed /proc/self/status value");
  }
  line.remove_prefix(separator + 1);
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())) != 0) {
    line.remove_prefix(1);
  }
  const auto end = line.find_first_not_of("0123456789");
  return parseUnsigned(line.substr(0, end), "/proc/self/status");
}

ProcessSample sampleProcess() {
  ProcessSample sample;
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("VmRSS:", 0) == 0) {
      sample.rss_kib = statusValue(line);
    } else if (line.rfind("Threads:", 0) == 0) {
      sample.threads = statusValue(line);
    }
  }
  if (sample.rss_kib == 0 || sample.threads == 0) {
    throw std::runtime_error("failed to read RSS or thread count from /proc/self/status");
  }
  return sample;
}

void runCycle() {
  if (!livekit::initialize(livekit::LogLevel::Warn)) {
    throw std::runtime_error("LiveKit initialization failed");
  }
  try {
    {
      auto audio_source = std::make_shared<livekit::AudioSource>(48'000, 1, 100);
      auto audio_track = livekit::LocalAudioTrack::createLocalAudioTrack("memory-audio", audio_source);
      auto video_source = std::make_shared<livekit::VideoSource>(640, 360);
      auto video_track = livekit::LocalVideoTrack::createLocalVideoTrack("memory-video", video_source);
      if (!audio_track || !video_track) {
        throw std::runtime_error("failed to create local tracks");
      }
    }
  } catch (...) {
    livekit::shutdown();
    throw;
  }
  livekit::shutdown();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

} // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parseOptions(argc, argv);
    ProcessSample baseline;
    ProcessSample final;
    for (std::uint64_t iteration = 1; iteration <= options.iterations; ++iteration) {
      runCycle();
      if (iteration == options.warmup) {
        baseline = sampleProcess();
      } else if (iteration == options.iterations) {
        final = sampleProcess();
      }
    }

    const auto rss_growth = static_cast<std::int64_t>(final.rss_kib) - static_cast<std::int64_t>(baseline.rss_kib);
    const auto thread_growth = static_cast<std::int64_t>(final.threads) - static_cast<std::int64_t>(baseline.threads);
    const bool passed = rss_growth <= static_cast<std::int64_t>(options.max_rss_growth_kib) &&
                        thread_growth <= static_cast<std::int64_t>(options.max_thread_growth);
    std::cout << "memory lifecycle: RSS " << baseline.rss_kib << " -> " << final.rss_kib << " KiB (" << rss_growth
              << " KiB), threads " << baseline.threads << " -> " << final.threads << " (" << thread_growth
              << "), verdict=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "memory lifecycle tester failed: " << error.what() << '\n';
    return 1;
  }
}
