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
#include <signal.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <fstream>
#endif

class DummyDelegate : public livekit::RoomDelegate {
public:
  void onDisconnected(livekit::Room& room, const livekit::DisconnectedEvent& event) override {
    std::cout << "Room disconnected: " << room.roomInfo().name << "\n";
    disconnected = true;
    disconnect_cv.notify_all();
  }

  std::condition_variable disconnect_cv;
  std::mutex disconnect_mutex;
  bool disconnected = false;
};

constexpr double kDefaultStatusIntervalS = 1.0;

struct Options {
  int iteration_count{1};
  double status_interval_s{kDefaultStatusIntervalS};
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

std::optional<std::uint64_t> currentRssKib() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(counters.WorkingSetSize) / 1024;
#elif defined(__APPLE__)
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  const kern_return_t result =
      task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
  if (result != KERN_SUCCESS) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(info.resident_size) / 1024;
#else
  std::ifstream status("/proc/self/status");
  if (!status) {
    return std::nullopt;
  }
  std::string line;
  while (std::getline(status, line)) {
    if (line.compare(0, 6, "VmRSS:") != 0) {
      continue;
    }
    std::istringstream fields(line.substr(6));
    std::uint64_t kib = 0;
    fields >> kib;
    if (!fields) {
      return std::nullopt;
    }
    return kib;
  }
  return std::nullopt;
#endif
}

std::string formatRssKib(std::uint64_t rss_kib) {
  std::ostringstream stream;
  stream << rss_kib << " KiB (" << std::fixed << std::setprecision(2) << static_cast<double>(rss_kib) / 1024.0
         << " MiB)";
  return stream.str();
}

std::string formatRssSample() {
  const auto rss_kib = currentRssKib();
  if (!rss_kib) {
    return "unavailable";
  }
  return formatRssKib(*rss_kib);
}

void printCycleStatus(int iteration, int iteration_count) {
  std::cout << "Completed " << iteration << "/" << iteration_count << " cycles (RSS " << formatRssSample() << ")\n";
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
  std::cerr << "usage: " << executable << " [N | --iterations N] [--status-interval SECONDS]\n"
            << "  N / --iterations N   Number of connect/wait-for-disconnect cycles (default: 1).\n"
            << "  --status-interval S  Print cycle/RSS status every S seconds (default: 1). 0 prints every "
               "iteration.\n";
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

    livekit::TokenRequestOptions token_request_options;
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

void signalHandler(int signal) {
  std::cout << "Signal " << signal << " received\n";
  shutdown_requested = true;
}

int main(int argc, char** argv) {
  Options options;
  try {
    options = parseOptions(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    printUsage(argv[0]);
    return 2;
  }

  if (!livekit::initialize(livekit::LogLevel::Info)) {
    std::cerr << "Failed to initialize LiveKit\n";
    return 1;
  }

  std::cout << "Running " << options.iteration_count << " iterations\n";

  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  const auto credentials = loadConnectCredentials();
  if (!credentials.has_value()) {
    livekit::shutdown();
    return 1;
  }

  auto last_status_at = std::chrono::steady_clock::now();
  for (int iteration = 1; iteration <= options.iteration_count && !shutdown_requested; ++iteration) {
    std::cout << "-----Starting iteration " << iteration << "-----\n";

    livekit::Room room;
    DummyDelegate delegate; // Fresh delegate is needed so that boolean resets
    room.setDelegate(&delegate);
    if (!room.connect(credentials->server_url, credentials->participant_token, livekit::RoomOptions())) {
      std::cerr << "Failed to connect to room\n";
      livekit::shutdown();
      return 1;
    }

    std::cout << "Connected to room: " << room.roomInfo().name << "\n";

    std::cout << "Waiting for room to disconnect...\n";
    std::unique_lock<std::mutex> lock(delegate.disconnect_mutex);
    delegate.disconnect_cv.wait(lock, [&]() { return delegate.disconnected || shutdown_requested; });

    if (shutdown_requested) {
      std::cout << "Shutdown requested, stopping iteration\n";
      break;
    }

    std::cout << "Disconnected from room\n";
    std::cout << "-----Stopped iteration " << iteration << "-----\n";

    const auto now = std::chrono::steady_clock::now();
    if (statusIsDue(iteration, options.iteration_count, options.status_interval_s, last_status_at, now)) {
      printCycleStatus(iteration, options.iteration_count);
      last_status_at = now;
    }
  }

  std::cout << "RSS final: " << formatRssSample() << "\n";
  std::cout << "Shutting down...\n";
  livekit::shutdown();

  return 0;
}