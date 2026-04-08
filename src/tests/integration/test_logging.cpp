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
#include <livekit/livekit.h>

#include "lk_log.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace livekit {
namespace test {

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

TEST_F(LoggingTest, DefaultLogLevelIsInfo) {
  EXPECT_EQ(livekit::getLogLevel(), LogLevel::Info);
}

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

  livekit::setLogCallback([&](LogLevel level, const std::string &logger_name,
                              const std::string &message) {
    std::lock_guard<std::mutex> lock(mtx);
    captured.push_back({level, logger_name, message});
  });

  livekit::setLogLevel(LogLevel::Trace);

  LK_LOG_INFO("hello from test");

  std::lock_guard<std::mutex> lock(mtx);
  ASSERT_GE(captured.size(), 1u);

  bool found = false;
  for (const auto &entry : captured) {
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

  livekit::setLogCallback(
      [&](LogLevel level, const std::string &, const std::string &) {
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

  livekit::setLogCallback(
      [&](LogLevel, const std::string &, const std::string &) {
        call_count.fetch_add(1);
      });

  livekit::setLogLevel(LogLevel::Warn);

  LK_LOG_TRACE("should be filtered");
  LK_LOG_DEBUG("should be filtered");
  LK_LOG_INFO("should be filtered");

  EXPECT_EQ(call_count.load(), 0)
      << "Messages below Warn should not reach callback";

  LK_LOG_WARN("should pass");
  LK_LOG_ERROR("should pass");
  LK_LOG_CRITICAL("should pass");

  EXPECT_EQ(call_count.load(), 3)
      << "Messages at or above Warn should reach callback";
}

TEST_F(LoggingTest, OffLevelSuppressesEverything) {
  std::atomic<int> call_count{0};

  livekit::setLogCallback(
      [&](LogLevel, const std::string &, const std::string &) {
        call_count.fetch_add(1);
      });

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

  livekit::setLogCallback(
      [&](LogLevel, const std::string &, const std::string &) {
        call_count.fetch_add(1);
      });

  livekit::setLogLevel(LogLevel::Trace);
  LK_LOG_INFO("goes to callback");
  EXPECT_GE(call_count.load(), 1);

  int before = call_count.load();
  livekit::setLogCallback(nullptr);

  LK_LOG_INFO("goes to stderr, not callback");
  EXPECT_EQ(call_count.load(), before)
      << "After setLogCallback(nullptr), old callback should not fire";
}

// ---------------------------------------------------------------------------
// Callback replacement
// ---------------------------------------------------------------------------

TEST_F(LoggingTest, ReplacingCallbackStopsOldOne) {
  std::atomic<int> old_count{0};
  std::atomic<int> new_count{0};

  livekit::setLogCallback([&](LogLevel, const std::string &,
                              const std::string &) { old_count.fetch_add(1); });

  livekit::setLogLevel(LogLevel::Trace);
  LK_LOG_INFO("to old");
  EXPECT_GE(old_count.load(), 1);

  int old_before = old_count.load();

  livekit::setLogCallback([&](LogLevel, const std::string &,
                              const std::string &) { new_count.fetch_add(1); });

  LK_LOG_INFO("to new");

  EXPECT_EQ(old_count.load(), old_before)
      << "Old callback should not receive messages after replacement";
  EXPECT_GE(new_count.load(), 1) << "New callback should receive messages";
}

// ---------------------------------------------------------------------------
// Level is preserved across callback changes
// ---------------------------------------------------------------------------

TEST_F(LoggingTest, LogLevelPreservedAcrossCallbackChange) {
  livekit::setLogLevel(LogLevel::Error);

  livekit::setLogCallback(
      [](LogLevel, const std::string &, const std::string &) {});

  EXPECT_EQ(livekit::getLogLevel(), LogLevel::Error)
      << "setLogCallback should preserve the current log level";
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

  for (auto &th : threads) {
    th.join();
  }
}

TEST_F(LoggingTest, ConcurrentLogEmissionDoesNotCrash) {
  std::atomic<int> call_count{0};

  livekit::setLogCallback(
      [&](LogLevel, const std::string &, const std::string &) {
        call_count.fetch_add(1);
      });

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

  for (auto &th : threads) {
    th.join();
  }

  EXPECT_GE(call_count.load(), kThreads * kIterations);
}

} // namespace test
} // namespace livekit
