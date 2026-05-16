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
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lk_log.h"

namespace livekit::test {

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

// ---------------------------------------------------------------------------
// Rust FFI -> C++ log forwarding
// ---------------------------------------------------------------------------

namespace {

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

} // namespace

// Verifies that records emitted from the Rust FFI core surface through the
// user-installed `setLogCallback` when the SDK is initialised with
// `LogSink::kCallback`.  Without this wiring, log forwarding would silently
// drop on the C++ side even though Rust is shipping `LogBatch` events over
// the FFI bridge.
//
// This test does NOT use the `LoggingTest` fixture because that fixture
// pre-initialises the SDK with `LogSink::kConsole` (i.e. `capture_logs=false`
// on the Rust side).  Toggling capture_logs requires a fresh
// `livekit::initialize()` call, so we manage the SDK lifecycle inline.
TEST(FfiLoggingTest, RustLogsReachUserCallback) {
  // Defensive: ensure the SDK is in a clean state in case this test runs
  // after another test that left it initialised.  shutdown() is a no-op
  // when not initialised.
  livekit::shutdown();

  struct Captured {
    LogLevel level;
    std::string logger_name;
    std::string message;
  };

  std::mutex mtx;
  std::vector<Captured> captured;

  // Install the callback BEFORE initialize() so that the very first records
  // emitted by Rust during FFI server setup (e.g. "initializing ffi server
  // v...") are observed.  Each record is also echoed to std::cout for ad
  // hoc inspection of what the Rust side is forwarding.
  livekit::setLogCallback([&](LogLevel level, const std::string& logger_name, const std::string& message) {
    const std::scoped_lock<std::mutex> lock(mtx);
    std::cout << "[ffi-log] [" << logLevelName(level) << "] [" << logger_name << "] " << message << '\n';
    captured.push_back({level, logger_name, message});
  });
  livekit::setLogLevel(LogLevel::Trace);

  ASSERT_TRUE(livekit::initialize(LogLevel::Trace, LogSink::kCallback));

  // The Rust FFI logger batches records and flushes on the next iteration
  // of its forwarding task.  Poll up to 5s for the well-known startup log
  // to surface; this is generous to keep CI stable but typically resolves
  // in well under a second.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  bool found_rust_log = false;
  while (std::chrono::steady_clock::now() < deadline) {
    {
      const std::scoped_lock<std::mutex> lock(mtx);
      for (const auto& entry : captured) {
        if (entry.message.find("initializing ffi server") != std::string::npos) {
          found_rust_log = true;
          break;
        }
      }
    }
    if (found_rust_log) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  EXPECT_TRUE(found_rust_log) << "Expected the Rust 'initializing ffi server' log record to reach the C++ "
                                 "setLogCallback via the FFI LogBatch bridge";

  // Sanity-check that the forwarded record carries the Rust target as the
  // logger name (i.e. NOT the C++-side "livekit" logger), proving that the
  // bridge preserves provenance instead of collapsing every record into the
  // SDK logger.
  bool saw_non_livekit_logger = false;
  {
    const std::scoped_lock<std::mutex> lock(mtx);
    for (const auto& entry : captured) {
      if (entry.message.find("initializing ffi server") != std::string::npos) {
        // Rust's `log::info!` in `livekit-ffi` reports a target rooted at
        // `livekit_ffi::...`; we only require that *some* target was
        // surfaced (any non-empty string), since the exact target string
        // is owned by the Rust side and may evolve.
        EXPECT_FALSE(entry.logger_name.empty()) << "FFI-forwarded log record should carry a target name";
      }
      if (!entry.logger_name.empty() && entry.logger_name != "livekit") {
        saw_non_livekit_logger = true;
      }
    }
  }
  EXPECT_TRUE(saw_non_livekit_logger)
      << "Expected at least one forwarded record to carry a Rust-side target distinct from the C++ 'livekit' logger";

  livekit::setLogCallback(nullptr);
  livekit::shutdown();
}

TEST_F(LoggingTest, DummyRustTest) {
  livekit::shutdown();
  ASSERT_TRUE(livekit::initialize(LogLevel::Info));
  LK_LOG_INFO("Hello from C++ SDK");

  // The single FFI call here -- CreateAudioTrack -- is what crosses the FFI
  // boundary into the `livekit` crate and triggers `LkRuntime::instance()`.
  // The AudioSource ctor on its own does NOT instantiate LkRuntime; the
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

// Verifies that internal Rust logs originating BELOW the FFI layer (i.e. in
// the `livekit` crate or in libwebrtc itself, not in `livekit-ffi`) propagate
// through the FFI bridge into the user-installed `setLogCallback`.
//
// The sibling `RustLogsReachUserCallback` test only proves that the FFI layer
// itself can forward its own startup log (`"initializing ffi server"`,
// emitted from inside `livekit_ffi::server::FfiServer::setup`). That alone
// does not prove that records emitted by deeper Rust crates -- which is what
// shows up under `RUST_LOG=debug` as `[DEBUG libwebrtc] (...)` or as logs
// targeted at `livekit::rtc_engine::*` -- also reach the user callback.
//
// Trigger path used here (fully in-process, no network):
//
//   C++  LocalAudioTrack::createLocalAudioTrack
//     -> FFI request CreateAudioTrack
//       -> Rust on_create_audio_track                     (livekit_ffi crate)
//         -> LocalAudioTrack::create_audio_track          (livekit crate)
//           -> LkRuntime::instance()
//                -> log::debug!("LkRuntime::new()")
//                   target = "livekit::rtc_engine::lk_runtime"
//           -> PeerConnectionFactory::default()           (libwebrtc-rs)
//                -> installs the libwebrtc LogSink that forwards every
//                   internal libwebrtc message as `log::debug!(target:
//                   "libwebrtc", ...)`.
//
// The libwebrtc-internal logs (e.g. the `text_pcap_packet_observer.cc` lines
// the user sees under `RUST_LOG=debug`) generally fire only during real
// signaling / ICE / SDP work, which requires a live LiveKit server and so
// belong in integration tests. The deepest deterministic signal we can
// trigger from a unit test is `LkRuntime::new()` -- which is still below the
// FFI layer (it lives in the `livekit` Rust crate), so capturing it proves
// the cross-crate forwarding works.
//
// Note about std::cout: the test installs a `setLogCallback`, which causes
// the C++ logging layer to swap its spdlog sink set entirely for a callback
// sink. While the callback is installed, no SDK log output reaches stderr or
// stdout, so this test does NOT pollute GTest's console output (the only
// echoes are the deliberate ad-hoc `[ffi-log]` prints below).
TEST(FfiLoggingTest, LogsFromBelowFfiLayerReachUserCallback) {
  // Defensive: start from a clean slate in case another test left the SDK
  // initialised. shutdown() is a no-op when not initialised.
  livekit::shutdown();

  struct Captured {
    LogLevel level;
    std::string logger_name;
    std::string message;
  };

  std::mutex mtx;
  std::vector<Captured> captured;

  // Install BEFORE initialize() so any startup records are observed too.
  // livekit::setLogCallback([&](LogLevel level, const std::string& logger_name, const std::string& message) {
  //   const std::scoped_lock<std::mutex> lock(mtx);
  //   std::cout << "[ffi-log] [" << logLevelName(level) << "] [" << logger_name << "] " << message << '\n';
  //   captured.push_back({level, logger_name, message});
  // });

  // Trace level so the FFI logger forwards everything
  ASSERT_TRUE(livekit::initialize(LogLevel::Trace, LogSink::kCallback));

  // The single FFI call here -- CreateAudioTrack -- is what crosses the FFI
  // boundary into the `livekit` crate and triggers `LkRuntime::instance()`.
  // The AudioSource ctor on its own does NOT instantiate LkRuntime; the
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

  // The Rust FfiLogger batches log records and flushes on a 1-second tick
  // (see livekit-ffi/src/server/logger.rs FLUSH_INTERVAL). Allow generous
  // headroom for slow CI machines without making the test wall-time-heavy.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  bool found_below_ffi_log = false;
  std::string matched_target;
  std::string matched_message;
  while (std::chrono::steady_clock::now() < deadline) {
    {
      const std::scoped_lock<std::mutex> lock(mtx);
      for (const auto& entry : captured) {
        // Targets demonstrably emitted from a layer BELOW livekit-ffi:
        //   * "livekit::..."   -> livekit Rust crate (e.g. LkRuntime)
        //   * "libwebrtc"      -> libwebrtc-rs LogSink shim
        //   * "webrtc"/"webrtc::..." -> direct libwebrtc-rs targets, future-proofing
        // We deliberately do NOT match "livekit_ffi" here -- that's the FFI
        // layer itself and would not prove cross-crate propagation.
        const auto& t = entry.logger_name;
        const bool is_livekit_crate = t.rfind("livekit::", 0) == 0;
        const bool is_libwebrtc = (t == "libwebrtc");
        const bool is_webrtc_crate = (t == "webrtc") || t.rfind("webrtc::", 0) == 0;
        if (is_livekit_crate || is_libwebrtc || is_webrtc_crate) {
          found_below_ffi_log = true;
          matched_target = t;
          matched_message = entry.message;
          break;
        }
      }
    }
    if (found_below_ffi_log) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  EXPECT_TRUE(found_below_ffi_log) << "Expected at least one log record from below the FFI layer "
                                      "(target starting with 'livekit::', 'libwebrtc', or 'webrtc') to "
                                      "reach the C++ setLogCallback. This validates the "
                                      "C++ -> FFI -> livekit-rust -> log -> FFI -> C++ chain. If this "
                                      "fires, it likely means either capture_logs=true is not being "
                                      "honoured by the Rust FfiLogger, or the LkRuntime weak-ref is "
                                      "still upgrading from a previous test (in which case run this "
                                      "test in isolation: --gtest_filter=FfiLoggingTest.LogsFromBelowFfiLayer*).";

  if (found_below_ffi_log) {
    // Ad hoc breadcrumb so test logs make it obvious WHICH below-FFI record
    // was the one that satisfied the assertion.
    std::cout << "[ffi-log] matched below-FFI target='" << matched_target << "' message='" << matched_message << "'"
              << '\n';
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

} // namespace livekit::test
