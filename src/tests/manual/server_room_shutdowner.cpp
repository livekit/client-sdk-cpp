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
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
#include <malloc/malloc.h>
#include <unistd.h>
#else
#include <unistd.h>

#include <fstream>
#endif

// Temporary internal diagnostic ABI. It is intentionally declared only in this
// manual test, rather than in the public C++ SDK headers.
struct FfiDebugLifecycleStats {
  std::uint64_t handles_total;
  std::uint64_t room_handles;
  std::uint64_t participant_handles;
  std::uint64_t track_handles;
  std::uint64_t publication_handles;
  std::uint64_t data_buffer_handles;
  std::uint64_t other_handles;
  std::uint64_t handle_drop_watchers;
  std::uint64_t ffi_room_inners_created;
  std::uint64_t ffi_room_inners_dropped;
  std::uint64_t ffi_room_inners_live;
  std::uint64_t room_sessions_created;
  std::uint64_t room_sessions_dropped;
  std::uint64_t room_sessions_live;
  std::uint64_t rtc_engines_created;
  std::uint64_t rtc_engines_dropped;
  std::uint64_t rtc_engines_live;
  std::uint64_t lk_runtimes_created;
  std::uint64_t lk_runtimes_dropped;
  std::uint64_t lk_runtimes_live;
  std::uint64_t rtc_sessions_created;
  std::uint64_t rtc_sessions_dropped;
  std::uint64_t rtc_sessions_live;
  std::uint64_t signal_clients_created;
  std::uint64_t signal_clients_dropped;
  std::uint64_t signal_clients_live;
  std::uint64_t peer_connection_factories_created;
  std::uint64_t peer_connection_factories_dropped;
  std::uint64_t peer_connection_factories_live;
  std::uint64_t peer_connections_created;
  std::uint64_t peer_connections_dropped;
  std::uint64_t peer_connections_live;
  std::uint64_t native_peer_connection_factories_created;
  std::uint64_t native_peer_connection_factories_dropped;
  std::uint64_t native_peer_connection_factories_live;
  std::uint64_t native_peer_connections_created;
  std::uint64_t native_peer_connections_dropped;
  std::uint64_t native_peer_connections_live;
  std::uint64_t native_peer_connection_observers_created;
  std::uint64_t native_peer_connection_observers_dropped;
  std::uint64_t native_peer_connection_observers_live;
  std::uint64_t ffi_room_tasks_spawned;
  std::uint64_t ffi_room_tasks_completed;
  std::uint64_t ffi_room_tasks_aborted;
  std::uint64_t ffi_room_tasks_active;
  std::uint64_t room_session_tasks_spawned;
  std::uint64_t room_session_tasks_completed;
  std::uint64_t room_session_tasks_aborted;
  std::uint64_t room_session_tasks_active;
  std::uint64_t rtc_session_tasks_spawned;
  std::uint64_t rtc_session_tasks_completed;
  std::uint64_t rtc_session_tasks_aborted;
  std::uint64_t rtc_session_tasks_active;
  std::uint64_t rtc_engine_tasks_spawned;
  std::uint64_t rtc_engine_tasks_completed;
  std::uint64_t rtc_engine_tasks_aborted;
  std::uint64_t rtc_engine_tasks_active;
  std::uint64_t signal_tasks_spawned;
  std::uint64_t signal_tasks_completed;
  std::uint64_t signal_tasks_aborted;
  std::uint64_t signal_tasks_active;
};

extern "C" bool livekit_ffi_debug_get_lifecycle_stats(FfiDebugLifecycleStats* out);
extern "C" void livekit_ffi_debug_set_keep_lk_runtime_alive(bool enabled);

struct FfiClientDebugStats {
  std::uint64_t listeners;
  std::uint64_t pending_async;
  std::uint64_t active_callbacks;
  std::uint64_t active_callback_threads;
  std::uint64_t next_listener_id;
  std::uint64_t next_async_id;
};

extern "C" bool livekit_debug_get_ffi_client_stats(FfiClientDebugStats* out);

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
  bool keep_runtime_alive{false};
  bool pause_at_runtime_reset{false};
  int runtime_reset_interval{0};
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

std::optional<std::uint64_t> currentHeapKib() {
#if defined(__APPLE__)
  malloc_statistics_t statistics{};
  malloc_zone_statistics(nullptr, &statistics);
  return static_cast<std::uint64_t>(statistics.size_in_use) / 1024;
#else
  return std::nullopt;
#endif
}

std::string formatHeapSample() {
  const auto heap_kib = currentHeapKib();
  if (!heap_kib) {
    return "unavailable";
  }
  return formatRssKib(*heap_kib);
}

void printMemorySample(const std::string& point) {
  std::cout << "Memory " << point << ": RSS " << formatRssSample() << ", heap in use " << formatHeapSample() << "\n";
  std::cout.flush();
}

std::uint64_t currentProcessId() {
#if defined(_WIN32)
  return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

void waitForProfilerCheckpoint(const std::string& point) {
  std::cout << "Profiler checkpoint " << point << " (PID " << currentProcessId()
            << "). Capture now, then press Enter to continue.\n";
  std::cout.flush();

  std::string ignored;
  if (!std::getline(std::cin, ignored)) {
    std::cerr << "Profiler checkpoint input unavailable; continuing without waiting\n";
  }
}

std::optional<std::uint64_t> currentThreadCount() {
#if defined(__APPLE__)
  thread_act_array_t threads = nullptr;
  mach_msg_type_number_t count = 0;
  if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) {
    return std::nullopt;
  }
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_t));
  return count;
#else
  return std::nullopt;
#endif
}

void printCycleStatus(int iteration, int iteration_count) {
  std::cout << "Completed " << iteration << "/" << iteration_count << " cycles (RSS " << formatRssSample()
            << ", heap in use " << formatHeapSample() << ")\n";
  std::cout.flush();
}

void printLifecycleStats(const std::string& point) {
  FfiDebugLifecycleStats stats{};
  if (!livekit_ffi_debug_get_lifecycle_stats(&stats)) {
    std::cerr << "Unable to retrieve FFI lifecycle statistics\n";
    return;
  }

  FfiClientDebugStats client_stats{};
  if (!livekit_debug_get_ffi_client_stats(&client_stats)) {
    std::cerr << "Unable to retrieve C++ FFI client statistics\n";
    return;
  }
  const auto thread_count = currentThreadCount();

  std::cout << "Lifecycle stats " << point << ": handles=" << stats.handles_total << " (rooms=" << stats.room_handles
            << ", participants=" << stats.participant_handles << ", tracks=" << stats.track_handles
            << ", publications=" << stats.publication_handles << ", response buffers=" << stats.data_buffer_handles
            << ", other=" << stats.other_handles << ", drop watchers=" << stats.handle_drop_watchers << ")\n"
            << "  C++ FfiClient [listeners=" << client_stats.listeners
            << ", pending async=" << client_stats.pending_async
            << ", active callbacks=" << client_stats.active_callbacks
            << ", callback threads=" << client_stats.active_callback_threads
            << ", next listener=" << client_stats.next_listener_id << ", next async=" << client_stats.next_async_id
            << "]\n"
            << "  process threads=" << (thread_count ? std::to_string(*thread_count) : std::string("unavailable"))
            << "\n"
            << "  live [ffi room=" << stats.ffi_room_inners_live << ", room session=" << stats.room_sessions_live
            << ", rtc engine=" << stats.rtc_engines_live << ", lk runtime=" << stats.lk_runtimes_live
            << ", rtc session=" << stats.rtc_sessions_live << ", signal client=" << stats.signal_clients_live
            << ", pc factory=" << stats.peer_connection_factories_live
            << ", peer connection=" << stats.peer_connections_live << "]\n"
            << "  native live [pc factory=" << stats.native_peer_connection_factories_live
            << ", peer connection=" << stats.native_peer_connections_live
            << ", observer=" << stats.native_peer_connection_observers_live << "]\n"
            << "  created/dropped [ffi room=" << stats.ffi_room_inners_created << "/" << stats.ffi_room_inners_dropped
            << ", room session=" << stats.room_sessions_created << "/" << stats.room_sessions_dropped
            << ", rtc engine=" << stats.rtc_engines_created << "/" << stats.rtc_engines_dropped
            << ", lk runtime=" << stats.lk_runtimes_created << "/" << stats.lk_runtimes_dropped
            << ", rtc session=" << stats.rtc_sessions_created << "/" << stats.rtc_sessions_dropped
            << ", signal client=" << stats.signal_clients_created << "/" << stats.signal_clients_dropped
            << ", pc factory=" << stats.peer_connection_factories_created << "/"
            << stats.peer_connection_factories_dropped << ", peer connection=" << stats.peer_connections_created << "/"
            << stats.peer_connections_dropped << "]\n"
            << "  native created/dropped [pc factory=" << stats.native_peer_connection_factories_created << "/"
            << stats.native_peer_connection_factories_dropped
            << ", peer connection=" << stats.native_peer_connections_created << "/"
            << stats.native_peer_connections_dropped << ", observer=" << stats.native_peer_connection_observers_created
            << "/" << stats.native_peer_connection_observers_dropped << "]\n"
            << "  tasks spawned/completed/aborted/active [ffi room=" << stats.ffi_room_tasks_spawned << "/"
            << stats.ffi_room_tasks_completed << "/" << stats.ffi_room_tasks_aborted << "/"
            << stats.ffi_room_tasks_active << ", room session=" << stats.room_session_tasks_spawned << "/"
            << stats.room_session_tasks_completed << "/" << stats.room_session_tasks_aborted << "/"
            << stats.room_session_tasks_active << ", rtc session=" << stats.rtc_session_tasks_spawned << "/"
            << stats.rtc_session_tasks_completed << "/" << stats.rtc_session_tasks_aborted << "/"
            << stats.rtc_session_tasks_active << ", rtc engine=" << stats.rtc_engine_tasks_spawned << "/"
            << stats.rtc_engine_tasks_completed << "/" << stats.rtc_engine_tasks_aborted << "/"
            << stats.rtc_engine_tasks_active << ", signal=" << stats.signal_tasks_spawned << "/"
            << stats.signal_tasks_completed << "/" << stats.signal_tasks_aborted << "/" << stats.signal_tasks_active
            << "]\n";
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
               " [--keep-runtime-alive] [--runtime-reset-interval N] [--pause-at-runtime-reset]"
               " [--profiler-checkpoint-cycle N]\n"
            << "  N / --iterations N   Number of connect/wait-for-disconnect cycles (default: 1).\n"
            << "  --status-interval S  Print cycle/RSS status every S seconds (default: 1). 0 prints every "
               "iteration.\n"
            << "  --keep-runtime-alive  Pin one LkRuntime/peer-connection factory across all iterations.\n";
  std::cerr << "  --runtime-reset-interval N  Unpin and recreate the runtime every N completed cycles.\n";
  std::cerr << "  --pause-at-runtime-reset  Wait for Enter immediately before and after each runtime unpin.\n";
  std::cerr << "  --profiler-checkpoint-cycle N  Wait for Enter after cycle N. May be repeated.\n";
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
    } else if (std::strcmp(value, "--keep-runtime-alive") == 0) {
      options.keep_runtime_alive = true;
    } else if (std::strcmp(value, "--pause-at-runtime-reset") == 0) {
      options.pause_at_runtime_reset = true;
      options.keep_runtime_alive = true;
    } else if (std::strcmp(value, "--profiler-checkpoint-cycle") == 0) {
      if (++argument == argc) {
        throw std::runtime_error("--profiler-checkpoint-cycle requires a value");
      }
      options.profiler_checkpoint_cycles.push_back(parseIterationCount(argv[argument]));
    } else if (std::strcmp(value, "--runtime-reset-interval") == 0) {
      if (++argument == argc) {
        throw std::runtime_error("--runtime-reset-interval requires a value");
      }
      options.runtime_reset_interval = parseIterationCount(argv[argument]);
      options.keep_runtime_alive = true;
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

  livekit_ffi_debug_set_keep_lk_runtime_alive(options.keep_runtime_alive);
  std::cout << "LkRuntime pinning: " << (options.keep_runtime_alive ? "enabled" : "disabled") << "\n";

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
    if (iteration % 100 == 0) {
      printLifecycleStats("after cycle " + std::to_string(iteration));
    }
    if (std::find(options.profiler_checkpoint_cycles.begin(), options.profiler_checkpoint_cycles.end(), iteration) !=
        options.profiler_checkpoint_cycles.end()) {
      printMemorySample("at profiler checkpoint after cycle " + std::to_string(iteration));
      waitForProfilerCheckpoint("after cycle " + std::to_string(iteration));
    }
    if (options.runtime_reset_interval > 0 && iteration < options.iteration_count &&
        iteration % options.runtime_reset_interval == 0) {
      printMemorySample("before runtime reset after cycle " + std::to_string(iteration));
      if (options.pause_at_runtime_reset) {
        waitForProfilerCheckpoint("before runtime reset after cycle " + std::to_string(iteration));
      }
      livekit_ffi_debug_set_keep_lk_runtime_alive(false);
      printMemorySample("after runtime reset release");
      printLifecycleStats("after runtime reset release");
      if (options.pause_at_runtime_reset) {
        waitForProfilerCheckpoint("after runtime reset release");
      }
      livekit_ffi_debug_set_keep_lk_runtime_alive(true);
      printMemorySample("after runtime reset recreate");
    }
  }

  std::cout << "RSS final: " << formatRssSample() << ", heap in use: " << formatHeapSample() << "\n";
  printLifecycleStats("before SDK shutdown");
  std::cout << "Shutting down...\n";
  livekit::shutdown();
  printLifecycleStats("after SDK shutdown");
  if (options.keep_runtime_alive) {
    printMemorySample("before final runtime unpin");
    if (options.pause_at_runtime_reset) {
      waitForProfilerCheckpoint("before final runtime unpin");
    }
    livekit_ffi_debug_set_keep_lk_runtime_alive(false);
    printMemorySample("after final runtime unpin");
    printLifecycleStats("after runtime unpin");
    if (options.pause_at_runtime_reset) {
      waitForProfilerCheckpoint("after final runtime unpin");
    }
  }

  return 0;
}
