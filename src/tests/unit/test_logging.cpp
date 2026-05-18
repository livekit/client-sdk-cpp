/*
 * Copyright 2025 LiveKit
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
#include <livekit/audio_source.h>
#include <livekit/livekit.h>
#include <livekit/local_audio_track.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ffi_client.h"
#include "livekit_ffi.h"
#include "lk_log.h"

namespace livekit::test {

namespace {

// FFI event entry point for tests. Forwards to the SDK handler so log batches
// reach setLogCallback() the same way they do in production.
extern "C" void testFfiCallback(const uint8_t* buf, size_t len) { LivekitFfiCallback(buf, len); }

// Utility function to convert LogLevel to a string for debug printing
const char* logLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return "TRACE";
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
    case LogLevel::Critical:
      return "CRITICAL";
    case LogLevel::Off:
      return "OFF";
  }
  return "?";
}

// Used to capture log records and verify them in tests
struct LogRecord {
  LogLevel level;
  std::string logger_name;
  std::string message;
};

} // namespace

class LoggingTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(); }

  void TearDown() override {
    livekit::setLogCallback(nullptr);
    livekit::shutdown();
  }
};

// ---------------------------------------------------------------------------
// setLogLevel / getLogLevel round-trip
// ---------------------------------------------------------------------------

TEST_F(LoggingTest, SetAndGetLogLevel) {
  const LogLevel levels[] = {
      LogLevel::Trace, LogLevel::Debug,    LogLevel::Info, LogLevel::Warn,
      LogLevel::Error, LogLevel::Critical, LogLevel::Off,
  };

  for (auto level : levels) {
    livekit::setLogLevel(level);
    EXPECT_EQ(livekit::getLogLevel(), level);
  }
}

TEST_F(LoggingTest, DefaultLogLevelIsInfo) { EXPECT_EQ(livekit::getLogLevel(), LogLevel::Info); }

// ---------------------------------------------------------------------------
// setLogCallback captures messages
// ---------------------------------------------------------------------------

TEST_F(LoggingTest, CallbackReceivesLogMessages) {
  struct Captured {
    LogLevel level;
    std::string logger_name;
    std::string message;
  };

  std::mutex mtx;
  std::vector<Captured> captured;

  livekit::setLogCallback([&](LogLevel level, const std::string& logger_name, const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);
    captured.push_back({level, logger_name, message});
  });

  livekit::setLogLevel(LogLevel::Trace);

  LK_LOG_INFO("hello from test");

  std::lock_guard<std::mutex> lock(mtx);
  ASSERT_GE(captured.size(), 1u);

  bool found = false;
  for (const auto& entry : captured) {
    if (entry.message.find("hello from test") != std::string::npos) {
      EXPECT_EQ(entry.level, LogLevel::Info);
      EXPECT_EQ(entry.logger_name, "livekit");
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Expected callback to capture 'hello from test'";
}

TEST_F(LoggingTest, CallbackReceivesCorrectLevel) {
  std::mutex mtx;
  std::vector<LogLevel> levels_seen;

  livekit::setLogCallback([&](LogLevel level, const std::string&, const std::string&) {
    std::lock_guard<std::mutex> lock(mtx);
    levels_seen.push_back(level);
  });

  livekit::setLogLevel(LogLevel::Trace);

  LK_LOG_WARN("a warning");
  LK_LOG_ERROR("an error");

  std::lock_guard<std::mutex> lock(mtx);
  ASSERT_GE(levels_seen.size(), 2u);

  size_t n = levels_seen.size();
  EXPECT_EQ(levels_seen[n - 2], LogLevel::Warn);
  EXPECT_EQ(levels_seen[n - 1], LogLevel::Error);
}

// ---------------------------------------------------------------------------
// Level filtering via setLogLevel
// ---------------------------------------------------------------------------

TEST_F(LoggingTest, MessagesFilteredBelowLevel) {
  std::atomic<int> call_count{0};

  livekit::setLogCallback([&](LogLevel, const std::string&, const std::string&) { call_count.fetch_add(1); });

  livekit::setLogLevel(LogLevel::Warn);

  LK_LOG_TRACE("should be filtered");
  LK_LOG_DEBUG("should be filtered");
  LK_LOG_INFO("should be filtered");

  EXPECT_EQ(call_count.load(), 0) << "Messages below Warn should not reach callback";

  LK_LOG_WARN("should pass");
  LK_LOG_ERROR("should pass");
  LK_LOG_CRITICAL("should pass");

  EXPECT_EQ(call_count.load(), 3) << "Messages at or above Warn should reach callback";
}

TEST_F(LoggingTest, OffLevelSuppressesEverything) {
  std::atomic<int> call_count{0};

  livekit::setLogCallback([&](LogLevel, const std::string&, const std::string&) { call_count.fetch_add(1); });

  livekit::setLogLevel(LogLevel::Off);

  LK_LOG_TRACE("suppressed");
  LK_LOG_DEBUG("suppressed");
  LK_LOG_INFO("suppressed");
  LK_LOG_WARN("suppressed");
  LK_LOG_ERROR("suppressed");
  LK_LOG_CRITICAL("suppressed");

  EXPECT_EQ(call_count.load(), 0);
}

// ---------------------------------------------------------------------------
// setLogCallback(nullptr) restores default
// ---------------------------------------------------------------------------

TEST_F(LoggingTest, NullCallbackRestoresDefault) {
  std::atomic<int> call_count{0};

  livekit::setLogCallback([&](LogLevel, const std::string&, const std::string&) { call_count.fetch_add(1); });

  livekit::setLogLevel(LogLevel::Trace);
  LK_LOG_INFO("goes to callback");
  EXPECT_GE(call_count.load(), 1);

  int before = call_count.load();
  livekit::setLogCallback(nullptr);

  LK_LOG_INFO("goes to stderr, not callback");
  EXPECT_EQ(call_count.load(), before) << "After setLogCallback(nullptr), old callback should not fire";
}

// ---------------------------------------------------------------------------
// Callback replacement
// ---------------------------------------------------------------------------

TEST_F(LoggingTest, ReplacingCallbackStopsOldOne) {
  std::atomic<int> old_count{0};
  std::atomic<int> new_count{0};

  livekit::setLogCallback([&](LogLevel, const std::string&, const std::string&) { old_count.fetch_add(1); });

  livekit::setLogLevel(LogLevel::Trace);
  LK_LOG_INFO("to old");
  EXPECT_GE(old_count.load(), 1);

  int old_before = old_count.load();

  livekit::setLogCallback([&](LogLevel, const std::string&, const std::string&) { new_count.fetch_add(1); });

  LK_LOG_INFO("to new");

  EXPECT_EQ(old_count.load(), old_before) << "Old callback should not receive messages after replacement";
  EXPECT_GE(new_count.load(), 1) << "New callback should receive messages";
}

// ---------------------------------------------------------------------------
// Level is preserved across callback changes
// ---------------------------------------------------------------------------

TEST_F(LoggingTest, LogLevelPreservedAcrossCallbackChange) {
  livekit::setLogLevel(LogLevel::Error);

  livekit::setLogCallback([](LogLevel, const std::string&, const std::string&) {});

  EXPECT_EQ(livekit::getLogLevel(), LogLevel::Error) << "setLogCallback should preserve the current log level";
}

// ---------------------------------------------------------------------------
// Thread safety
// ---------------------------------------------------------------------------

TEST_F(LoggingTest, ConcurrentSetLogLevelDoesNotCrash) {
  constexpr int kThreads = 8;
  constexpr int kIterations = 500;

  const LogLevel levels[] = {LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
                             LogLevel::Warn,  LogLevel::Error, LogLevel::Off};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < kIterations; ++i) {
        livekit::setLogLevel(levels[(t + i) % 6]);
        [[maybe_unused]] auto lvl = livekit::getLogLevel();
      }
    });
  }

  for (auto& th : threads) {
    th.join();
  }
}

TEST_F(LoggingTest, ConcurrentLogEmissionDoesNotCrash) {
  std::atomic<int> call_count{0};

  livekit::setLogCallback([&](LogLevel, const std::string&, const std::string&) { call_count.fetch_add(1); });

  livekit::setLogLevel(LogLevel::Trace);

  constexpr int kThreads = 8;
  constexpr int kIterations = 200;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < kIterations; ++i) {
        LK_LOG_INFO("thread {} iteration {}", t, i);
      }
    });
  }

  for (auto& th : threads) {
    th.join();
  }

  EXPECT_GE(call_count.load(), kThreads * kIterations);
}

// ---------------------------------------------------------------------------
// Rust FFI -> C++ log forwarding
// ---------------------------------------------------------------------------

TEST_F(LoggingTest, RustLogsAreForwarded) {
  // User toggle for debugging this unit test
  bool print_logs = true;

  // Start from clean slate
  livekit::shutdown();

  std::mutex mut;
  std::vector<LogRecord> captured;
  std::condition_variable cv;

  // Used to track how many logs of each level have been forwarded,
  // and used by the condition variable to immedtiate unblock once the buffered logs are consumed
  std::size_t info_log_count = 0;
  std::size_t warn_log_count = 0;
  std::size_t debug_log_count = 0;
  std::size_t error_log_count = 0;

  const auto required_rust_logs_received = [&] {
    return debug_log_count > 0 && warn_log_count > 0 && error_log_count > 0;
  };

  livekit::setLogLevel(LogLevel::Trace);
  livekit::setLogCallback([&](LogLevel level, const std::string& logger_name, const std::string& message) {
    const std::scoped_lock<std::mutex> lock(mut);
    captured.push_back({level, logger_name, message});
    if (print_logs) {
      std::cout << "[Forwarded Rust log] [" << logLevelName(level) << "] [" << logger_name << "] " << message << '\n';
    }

    switch (level) {
      case LogLevel::Info:
        ++info_log_count;
        break;
      case LogLevel::Warn:
        ++warn_log_count;
        break;
      case LogLevel::Debug:
        ++debug_log_count;
        break;
      case LogLevel::Error:
        ++error_log_count;
        break;
      default:
        break;
    }

    // Wake once init (debug), invalid handle (warn), and bad request (error) have all been forwarded.
    if (required_rust_logs_received()) {
      cv.notify_one();
    }
  });

  // Induce a Rust-side debug level log
  // Via FFI initialization messages
  ASSERT_NO_THROW(livekit_ffi_initialize(testFfiCallback, true, "cpp-test", "1.0.0"));

  // Induce a Rust-wise warning level log
  // Via dropping an invalid handle
  livekit_ffi_drop_handle(12345);

  // Induce a Rust-side error level log
  // Via an invalid protobuf payload
  {
    std::vector<uint8_t> data(100);
    const uint8_t* res_ptr = nullptr;
    size_t res_len = 0;
    const auto handle = livekit_ffi_request(data.data(), data.size(), &res_ptr, &res_len);
    EXPECT_EQ(handle, INVALID_HANDLE);
  }

  // Wait for the required logs to be forwarded
  {
    std::unique_lock<std::mutex> lock(mut);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(1), required_rust_logs_received));
  }

  if (print_logs) {
    std::cout << "Info logs: " << info_log_count << '\n';
    std::cout << "Warn logs: " << warn_log_count << '\n';
    std::cout << "Debug logs: " << debug_log_count << '\n';
    std::cout << "Error logs: " << error_log_count << '\n';
  }

  EXPECT_GT(warn_log_count, 0);
  EXPECT_GT(debug_log_count, 0);
  EXPECT_GT(error_log_count, 0);

  livekit_ffi_dispose();
}

TEST_F(LoggingTest, DummyRustTest) {
  livekit::shutdown();
  ASSERT_TRUE(livekit::initialize(LogLevel::Info));
  LK_LOG_INFO("Hello from C++ SDK");

  // The single FFI call here -- CreateAudioTrack -- is what crosses the FFI
  // boundary into the `livekit` crate and triggers `LkRuntime::instance()`.
  // The AudioSource ctor on its own does NOT instantiate LkRuntime;] the
  // track creation does.
  {
    auto audio_source = std::make_shared<AudioSource>(48000, 1);
    auto track = LocalAudioTrack::createLocalAudioTrack("ffi-log-probe", audio_source);
    ASSERT_NE(track, nullptr);
    // Hold the handles briefly so any synchronous Rust-side debug logs land
    // before the strong refs drop. The FFI logger flushes on a 1s interval,
    // so this sleep does not gate observability -- it just keeps things tidy.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

} // namespace livekit::test
