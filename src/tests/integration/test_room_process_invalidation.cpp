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

/// @file test_room_process_invalidation.cpp
/// @brief Linux integration repro for server-driven room invalidation at process exit.
///
/// Reproduces the user-reported shutdown path: a C++ client joins a room, the server
/// invalidates the session (duplicate identity), and the client process exits without
/// calling Room::disconnect() or livekit::shutdown(). On glibc/Linux, heap corruption
/// during thread teardown can surface as:
///   SIGABRT: tcache_thread_shutdown(): unaligned tcache chunk detected

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <cstdlib>
#include <string>

#if defined(__linux__)

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <mutex>
#include <thread>
#endif

namespace livekit::test {

#if defined(__linux__)

namespace {

using namespace std::chrono_literals;

constexpr char kChildReadyByte = 1;
constexpr auto kChildConnectTimeout = 30s;
constexpr auto kInvalidationTimeout = 30s;
constexpr auto kChildExitTimeout = 30s;

struct ProcessInvalidationConfig {
  std::string url;
  std::string token;
  bool available = false;

  static ProcessInvalidationConfig fromEnv() {
    ProcessInvalidationConfig config;
    const char* url = std::getenv("LIVEKIT_URL");
    const char* token = std::getenv("LIVEKIT_TOKEN_A");
    if (url != nullptr && token != nullptr && url[0] != '\0' && token[0] != '\0') {
      config.url = url;
      config.token = token;
      config.available = true;
    }
    return config;
  }
};

class DuplicateIdentityWaiter : public RoomDelegate {
public:
  void onDisconnected(Room&, const DisconnectedEvent& event) override {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      reason_ = event.reason;
      disconnected_ = true;
    }
    cv_.notify_all();
  }

  bool waitForDuplicateIdentity(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout,
                        [this]() { return disconnected_ && reason_ == DisconnectReason::DuplicateIdentity; });
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool disconnected_ = false;
  DisconnectReason reason_ = DisconnectReason::Unknown;
};

bool writeByte(int fd, char value) { return ::write(fd, &value, 1) == 1; }

bool readByte(int fd, char* value) { return ::read(fd, value, 1) == 1; }

void runVictimChild(int ready_pipe_write, const std::string& url, const std::string& token) {
  if (!livekit::initialize(livekit::LogLevel::Info)) {
    _exit(2);
  }

  Room room;
  DuplicateIdentityWaiter delegate;
  room.setDelegate(&delegate);

  RoomOptions options;
  if (!room.connect(url, token, options)) {
    _exit(3);
  }

  if (!writeByte(ready_pipe_write, kChildReadyByte)) {
    _exit(4);
  }

  if (!delegate.waitForDuplicateIdentity(kInvalidationTimeout)) {
    _exit(5);
  }

  // Deliberately skip Room::disconnect() and livekit::shutdown() to mirror the
  // user report: process exit tears down Rust/WebRTC threads and glibc allocators.
  std::exit(0);
}

} // namespace

class RoomInvalidationProcessExitTest : public ::testing::Test {};

TEST_F(RoomInvalidationProcessExitTest, DuplicateIdentityInvalidationThenClientProcessExit) {
  const ProcessInvalidationConfig config = ProcessInvalidationConfig::fromEnv();
  if (!config.available) {
    GTEST_SKIP() << "LIVEKIT_URL and LIVEKIT_TOKEN_A must be set";
  }

  int ready_pipe[2] = {-1, -1};
  ASSERT_EQ(::pipe(ready_pipe), 0) << "pipe() failed: " << std::strerror(errno);

  const pid_t child_pid = ::fork();
  ASSERT_NE(child_pid, -1) << "fork() failed: " << std::strerror(errno);

  if (child_pid == 0) {
    ::close(ready_pipe[0]);
    runVictimChild(ready_pipe[1], config.url, config.token);
    ::close(ready_pipe[1]);
    _exit(1);
  }

  ::close(ready_pipe[1]);

  char ready_byte = 0;
  ASSERT_TRUE(readByte(ready_pipe[0], &ready_byte)) << "timed out waiting for child connect signal";
  ::close(ready_pipe[0]);
  ASSERT_EQ(ready_byte, kChildReadyByte);

  ASSERT_TRUE(livekit::initialize(livekit::LogLevel::Info)) << "parent initialize failed";

  Room usurper;
  RoomOptions options;
  ASSERT_TRUE(usurper.connect(config.url, config.token, options))
      << "parent failed to connect with duplicate identity token";

  int status = 0;
  const auto wait_deadline = std::chrono::steady_clock::now() + kChildExitTimeout;
  while (true) {
    const pid_t waited = ::waitpid(child_pid, &status, WNOHANG);
    if (waited == child_pid) {
      break;
    }
    if (waited == -1) {
      FAIL() << "waitpid() failed: " << std::strerror(errno);
    }
    if (std::chrono::steady_clock::now() >= wait_deadline) {
      ::kill(child_pid, SIGKILL);
      (void)::waitpid(child_pid, &status, 0);
      FAIL() << "timed out waiting for victim child process to exit";
    }
    std::this_thread::sleep_for(50ms);
  }

  usurper.disconnect();
  livekit::shutdown();

  if (WIFSIGNALED(status)) {
    const int signal = WTERMSIG(status);
    FAIL() << "victim child terminated by signal " << signal
           << " (expected clean exit 0; SIGABRT=6 often indicates glibc tcache corruption on shutdown)";
  }

  if (!WIFEXITED(status)) {
    FAIL() << "victim child did not exit normally";
  }

  EXPECT_EQ(WEXITSTATUS(status), 0) << "victim child should exit 0 after duplicate-identity invalidation";
}

#else

class RoomInvalidationProcessExitTest : public ::testing::Test {};

TEST_F(RoomInvalidationProcessExitTest, DuplicateIdentityInvalidationThenClientProcessExit) {
  GTEST_SKIP() << "Linux-only repro: glibc tcache_thread_shutdown() abort is not observable on this platform";
}

#endif

} // namespace livekit::test
