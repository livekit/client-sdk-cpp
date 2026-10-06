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

#pragma once

#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

// Current process counters for manual lifecycle testers. Unavailable samples
// are nullopt. Windows has no cheap malloc-in-use counter, so heap stays empty
// there. Windows RSS links psapi.

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
#include <tlhelp32.h>
// clang-format on
#if defined(_MSC_VER)
#pragma comment(lib, "psapi.lib")
#endif
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <unistd.h>
#elif defined(__linux__)
#include <unistd.h>

#include <cctype>
#include <fstream>
#include <string_view>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#else
#include <unistd.h>
#endif

namespace livekit::test {

struct ProcessSample {
  std::optional<std::uint64_t> rss_kib;
  std::optional<std::uint64_t> heap_kib;
  std::optional<std::uint64_t> thread_count;
};

inline std::uint64_t currentProcessId() {
#if defined(_WIN32)
  return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

#if defined(__linux__)

inline std::optional<std::uint64_t> parseLeadingUint(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  if (text.empty() || std::isdigit(static_cast<unsigned char>(text.front())) == 0) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const char digit : text) {
    if (std::isdigit(static_cast<unsigned char>(digit)) == 0) {
      break;
    }
    value = value * 10U + static_cast<std::uint64_t>(digit - '0');
  }
  return value;
}

inline std::optional<std::uint64_t> readSmapsRssKib() {
  std::ifstream smaps("/proc/self/smaps_rollup");
  if (!smaps) {
    return std::nullopt;
  }
  std::string line;
  while (std::getline(smaps, line)) {
    constexpr std::string_view kPrefix = "Rss:";
    if (line.compare(0, kPrefix.size(), kPrefix) != 0) {
      continue;
    }
    return parseLeadingUint(std::string_view(line).substr(kPrefix.size()));
  }
  return std::nullopt;
}

struct ProcStatus {
  std::optional<std::uint64_t> vm_rss_kib;
  std::optional<std::uint64_t> thread_count;
};

inline ProcStatus readProcStatus() {
  ProcStatus status;
  std::ifstream file("/proc/self/status");
  if (!file) {
    return status;
  }
  std::string line;
  while (std::getline(file, line)) {
    constexpr std::string_view kRss = "VmRSS:";
    constexpr std::string_view kThreads = "Threads:";
    if (line.compare(0, kRss.size(), kRss) == 0) {
      status.vm_rss_kib = parseLeadingUint(std::string_view(line).substr(kRss.size()));
    } else if (line.compare(0, kThreads.size(), kThreads) == 0) {
      status.thread_count = parseLeadingUint(std::string_view(line).substr(kThreads.size()));
    }
  }
  return status;
}

#endif

inline std::optional<std::uint64_t> currentRssKib() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(counters.WorkingSetSize) / 1024U;
#elif defined(__APPLE__)
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  const kern_return_t result =
      task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
  if (result != KERN_SUCCESS) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(info.resident_size) / 1024U;
#elif defined(__linux__)
  if (const auto smaps_rss = readSmapsRssKib()) {
    return smaps_rss;
  }
  return readProcStatus().vm_rss_kib;
#else
  return std::nullopt;
#endif
}

inline std::optional<std::uint64_t> currentHeapKib() {
#if defined(__APPLE__)
  malloc_statistics_t statistics{};
  malloc_zone_statistics(nullptr, &statistics);
  return static_cast<std::uint64_t>(statistics.size_in_use) / 1024U;
#elif defined(__linux__) && defined(__GLIBC__)
  const struct mallinfo2 info = mallinfo2();
  return static_cast<std::uint64_t>(info.uordblks) / 1024U;
#else
  return std::nullopt;
#endif
}

inline std::optional<std::uint64_t> currentThreadCount() {
#if defined(_WIN32)
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  const DWORD pid = GetCurrentProcessId();
  if (Thread32First(snapshot, &entry) == FALSE) {
    CloseHandle(snapshot);
    return std::nullopt;
  }
  std::uint64_t count = 0;
  do {
    if (entry.th32OwnerProcessID == pid) {
      ++count;
    }
  } while (Thread32Next(snapshot, &entry) != FALSE);
  CloseHandle(snapshot);
  return count;
#elif defined(__APPLE__)
  thread_act_array_t threads = nullptr;
  mach_msg_type_number_t count = 0;
  if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) {
    return std::nullopt;
  }
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_t));
  return count;
#elif defined(__linux__)
  return readProcStatus().thread_count;
#else
  return std::nullopt;
#endif
}

inline ProcessSample currentProcessSample() {
  ProcessSample sample;
  sample.rss_kib = currentRssKib();
  sample.heap_kib = currentHeapKib();
  sample.thread_count = currentThreadCount();
  return sample;
}

inline std::string formatKib(std::uint64_t kib) {
  std::ostringstream stream;
  stream << kib << " KiB (" << std::fixed << std::setprecision(2) << static_cast<double>(kib) / 1024.0 << " MiB)";
  return stream.str();
}

inline std::string formatKibSample(std::optional<std::uint64_t> kib) {
  if (!kib) {
    return "unavailable";
  }
  return formatKib(*kib);
}

} // namespace livekit::test
