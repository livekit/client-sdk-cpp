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
#include <livekit/room_event_types.h>
#include <livekit/token_source.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "../common/process_stats.h"

class DummyDelegate : public livekit::RoomDelegate {
public:
  void onDisconnected(livekit::Room& room, const livekit::DisconnectedEvent& event) override {
    std::cout << "Room disconnected: " << room.roomInfo().name << "\n";
    {
      const std::scoped_lock<std::mutex> lock(lifecycle_mutex);
      disconnected = true;
    }
    lifecycle_cv.notify_all();
  }

  void onRoomEos(livekit::Room&, const livekit::RoomEosEvent&) override {
    {
      const std::scoped_lock<std::mutex> lock(lifecycle_mutex);
      eos = true;
    }
    lifecycle_cv.notify_all();
  }

  std::condition_variable lifecycle_cv;
  std::mutex lifecycle_mutex;
  bool disconnected = false;
  bool eos = false;
};

constexpr double kDefaultStatusIntervalS = 1.0;

struct Options {
  int iteration_count{1};
  double status_interval_s{kDefaultStatusIntervalS};
  bool wait_for_disconnect{false};
  std::vector<int> profiler_checkpoint_cycles;
};

std::optional<std::string> getEnv(const char* env) {
  const char* result = std::getenv(env);
  if (result != nullptr && result[0] != '\0') return result;
  return std::nullopt;
}

int parseIterationCount(const char* value) {
  try {
    const int parsed = std::stoi(value);
    if (parsed <= 0) {
      throw std::runtime_error("iteration count must be greater than zero");
    }
    return parsed;
  } catch (const std::invalid_argument&) {
    throw std::runtime_error("iteration count must be an integer");
  } catch (const std::out_of_range&) {
    throw std::runtime_error("iteration count is out of range");
  }
}

double parseStatusInterval(const char* value) {
  try {
    const double parsed = std::stod(value);
    if (parsed < 0.0) {
      throw std::runtime_error("status interval must be greater than or equal to zero");
    }
    return parsed;
  } catch (const std::invalid_argument&) {
    throw std::runtime_error("status interval must be a number");
  } catch (const std::out_of_range&) {
    throw std::runtime_error("status interval is out of range");
  }
}

std::string formatCount(std::optional<std::uint64_t> count) {
  if (!count) {
    return "unavailable";
  }
  return std::to_string(*count);
}

void printMemorySample(const std::string& point) {
  const livekit::test::ProcessSample sample = livekit::test::currentProcessSample();
  std::cout << "Memory " << point << ": RSS " << livekit::test::formatKibSample(sample.rss_kib) << ", heap in use "
            << livekit::test::formatKibSample(sample.heap_kib) << ", threads " << formatCount(sample.thread_count)
            << "\n";
  std::cout.flush();
}

void waitForProfilerCheckpoint(const std::string& point) {
  std::cout << "Profiler checkpoint " << point << " (PID " << livekit::test::currentProcessId()
            << "). Capture now, then press Enter to continue.\n";
  std::cout.flush();

  std::string ignored;
  if (!std::getline(std::cin, ignored)) {
    std::cerr << "Profiler checkpoint input unavailable; continuing without waiting\n";
  }
}

void printCycleStatus(int iteration, int iteration_count) {
  const livekit::test::ProcessSample sample = livekit::test::currentProcessSample();
  std::cout << "Completed " << iteration << "/" << iteration_count << " cycles (RSS "
            << livekit::test::formatKibSample(sample.rss_kib) << ", heap in use "
            << livekit::test::formatKibSample(sample.heap_kib) << ", threads " << formatCount(sample.thread_count)
            << ")\n";
  std::cout.flush();
}

bool statusIsDue(int iteration, int iteration_count, double interval_s,
                 std::chrono::steady_clock::time_point last_status, std::chrono::steady_clock::time_point now) {
  if (iteration == 1 || iteration == iteration_count || interval_s == 0.0) {
    return true;
  }
  return now - last_status >= std::chrono::duration<double>(interval_s);
}

void printUsage(const char* executable) {
  std::cerr << "usage: " << executable
            << " [N | --iterations N] [--status-interval SECONDS] [--wait-for-disconnect]"
               " [--profiler-checkpoint-cycle N]\n"
            << "  N / --iterations N   Number of connect/wait-for-disconnect cycles (default: 1).\n"
            << "  --status-interval S  Print cycle/RSS status every S seconds (default: 1). 0 prints every "
               "iteration.\n"
            << "  --profiler-checkpoint-cycle N  Wait for Enter after cycle N. May be repeated.\n";
}

Options parseOptions(int argc, char* argv[]) {
  Options options;
  bool saw_iteration_count = false;
  for (int argument = 1; argument < argc; ++argument) {
    const char* value = argv[argument];
    if (std::strcmp(value, "--iterations") == 0) {
      if (++argument == argc) {
        throw std::runtime_error("--iterations requires a value");
      }
      options.iteration_count = parseIterationCount(argv[argument]);
      saw_iteration_count = true;
    } else if (std::strcmp(value, "--status-interval") == 0) {
      if (++argument == argc) {
        throw std::runtime_error("--status-interval requires a value");
      }
      options.status_interval_s = parseStatusInterval(argv[argument]);
    } else if (std::strcmp(value, "--wait-for-disconnect") == 0) {
      options.wait_for_disconnect = true;
    } else if (std::strcmp(value, "--profiler-checkpoint-cycle") == 0) {
      if (++argument == argc) {
        throw std::runtime_error("--profiler-checkpoint-cycle requires a value");
      }
      options.profiler_checkpoint_cycles.push_back(parseIterationCount(argv[argument]));
    } else if (std::strcmp(value, "--help") == 0 || std::strcmp(value, "-h") == 0) {
      printUsage(argv[0]);
      std::exit(0);
    } else if (!saw_iteration_count && value[0] != '-') {
      options.iteration_count = parseIterationCount(value);
      saw_iteration_count = true;
    } else {
      throw std::runtime_error(std::string("unknown argument: ") + value);
    }
  }
  return options;
}

struct ConnectCredentials {
  std::string server_url;
  std::string participant_token;
};

std::optional<ConnectCredentials> loadConnectCredentials() {
  const auto token_server_id = getEnv("LIVEKIT_TOKEN_SERVER_ID");
  const auto url = getEnv("LIVEKIT_URL");
  auto token = getEnv("LIVEKIT_TOKEN");
  if (!token.has_value()) {
    token = getEnv("LIVEKIT_TOKEN_A");
  }

  const bool has_token_server = token_server_id.has_value();
  const bool has_url = url.has_value();
  const bool has_token = token.has_value();
  const bool has_direct = has_url || has_token;

  if (has_token_server && has_direct) {
    std::cerr << "Provide either LIVEKIT_TOKEN_SERVER_ID or LIVEKIT_URL + "
                 "LIVEKIT_TOKEN (or LIVEKIT_TOKEN_A), not both\n";
    return std::nullopt;
  }

  if (has_token_server) {
    auto token_source = livekit::DevelopmentTokenSource::create(token_server_id.value());
    if (!token_source) {
      std::cerr << "Failed to create token source\n";
      return std::nullopt;
    }

    const livekit::TokenRequestOptions token_request_options;
    auto token_response = token_source->fetch(token_request_options).get();
    if (!token_response.ok()) {
      std::cerr << "Failed to fetch token from development token server\n";
      return std::nullopt;
    }

    return ConnectCredentials{token_response.value().server_url, token_response.value().participant_token};
  }

  if (has_url && has_token) {
    return ConnectCredentials{url.value(), token.value()};
  }

  if (has_url != has_token) {
    std::cerr << "LIVEKIT_URL and LIVEKIT_TOKEN (or LIVEKIT_TOKEN_A) must both be set\n";
    return std::nullopt;
  }

  std::cerr << "Set LIVEKIT_TOKEN_SERVER_ID, or LIVEKIT_URL and LIVEKIT_TOKEN "
               "(or LIVEKIT_TOKEN_A)\n";
  return std::nullopt;
}

std::atomic<bool> shutdown_requested = false;

void signalHandler(int) { shutdown_requested.store(true); }

int main(int argc, char** argv) {
  Options options;
  try {
    options = parseOptions(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    printUsage(argv[0]);
    return 2;
  }

  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);
#ifndef _WIN32
  std::signal(SIGHUP, signalHandler);
#endif

  if (!livekit::initialize(livekit::LogLevel::Info)) {
    std::cerr << "Failed to initialize LiveKit\n";
    return 1;
  }

  std::cout << "Running " << options.iteration_count << " iterations\n";

  if (shutdown_requested.load()) {
    livekit::shutdown();
    return 0;
  }

  const auto credentials = loadConnectCredentials();
  if (!credentials.has_value()) {
    livekit::shutdown();
    return 1;
  }

  auto last_status_at = std::chrono::steady_clock::now();
  for (int iteration = 1; iteration <= options.iteration_count && !shutdown_requested; ++iteration) {
    std::cout << "-----Starting iteration " << iteration << "-----\n";

    {
      livekit::Room room;
      DummyDelegate delegate;
      room.setDelegate(&delegate);
      if (!room.connect(credentials->server_url, credentials->participant_token, livekit::RoomOptions())) {
        std::cerr << "Failed to connect to room\n";
        livekit::shutdown();
        return 1;
      }

      std::cout << "Connected to room: " << room.roomInfo().name << "\n";

      if (options.wait_for_disconnect) {
        std::cout << "Waiting for room to disconnect...\n";
        {
          std::unique_lock<std::mutex> lock(delegate.lifecycle_mutex);
          while (!delegate.disconnected && !shutdown_requested.load()) {
            delegate.lifecycle_cv.wait_for(lock, std::chrono::milliseconds(100));
          }
        }

        if (shutdown_requested.load()) {
          std::cout << "Shutdown requested, disconnecting room\n";
          (void)room.disconnect();
          break;
        }

        std::cout << "Waiting for room teardown...\n";
        {
          std::unique_lock<std::mutex> lock(delegate.lifecycle_mutex);
          while (!delegate.eos && !shutdown_requested.load()) {
            delegate.lifecycle_cv.wait_for(lock, std::chrono::milliseconds(100));
          }
        }
      } else {
        std::cout << "Disconnecting room...\n";
        (void)room.disconnect();
      }

      std::cout << "Disconnected from room\n";
    }
    std::cout << "-----Stopped iteration " << iteration << "-----\n";

    const auto now = std::chrono::steady_clock::now();
    if (statusIsDue(iteration, options.iteration_count, options.status_interval_s, last_status_at, now)) {
      printCycleStatus(iteration, options.iteration_count);
      last_status_at = now;
    }
    if (std::find(options.profiler_checkpoint_cycles.begin(), options.profiler_checkpoint_cycles.end(), iteration) !=
        options.profiler_checkpoint_cycles.end()) {
      printMemorySample("at profiler checkpoint after cycle " + std::to_string(iteration));
      waitForProfilerCheckpoint("after cycle " + std::to_string(iteration));
    }
  }

  const livekit::test::ProcessSample final_sample = livekit::test::currentProcessSample();
  std::cout << "RSS final: " << livekit::test::formatKibSample(final_sample.rss_kib)
            << ", heap in use: " << livekit::test::formatKibSample(final_sample.heap_kib)
            << ", threads: " << formatCount(final_sample.thread_count) << "\n";
  std::cout << "Shutting down...\n";
  livekit::shutdown();

  return 0;
}
