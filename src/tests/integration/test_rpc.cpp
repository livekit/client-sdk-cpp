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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <gtest/gtest.h>
#include <livekit/livekit.h>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace livekit {
namespace test {

using namespace std::chrono_literals;

// Maximum RPC payload size (15KB)
constexpr size_t kMaxRpcPayloadSize = 15 * 1024;

// Test configuration from environment variables
struct RpcTestConfig {
  std::string url;
  std::string caller_token;
  std::string receiver_token;
  bool available = false;

  static RpcTestConfig fromEnv() {
    RpcTestConfig config;
    const char *url = std::getenv("LIVEKIT_URL");
    const char *caller_token = std::getenv("LIVEKIT_CALLER_TOKEN");
    const char *receiver_token = std::getenv("LIVEKIT_RECEIVER_TOKEN");

    if (url && caller_token && receiver_token) {
      config.url = url;
      config.caller_token = caller_token;
      config.receiver_token = receiver_token;
      config.available = true;
    }
    return config;
  }
};

// Generate a random string of specified size
std::string generateRandomPayload(size_t size) {
  static const char charset[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);

  std::string result;
  result.reserve(size);
  for (size_t i = 0; i < size; ++i) {
    result += charset[dis(gen)];
  }
  return result;
}

// Wait for a remote participant to appear
bool waitForParticipant(Room *room, const std::string &identity,
                        std::chrono::milliseconds timeout) {
  auto start = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - start < timeout) {
    if (room->remoteParticipant(identity) != nullptr) {
      return true;
    }
    std::this_thread::sleep_for(100ms);
  }
  return false;
}

class RpcIntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogSink::kConsole);
    config_ = RpcTestConfig::fromEnv();
  }

  void TearDown() override { livekit::shutdown(); }

  RpcTestConfig config_;
};

// Test basic RPC round-trip
TEST_F(RpcIntegrationTest, BasicRpcRoundTrip) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  // Create receiver room
  auto receiver_room = std::make_unique<Room>();
  RoomOptions receiver_options;
  receiver_options.auto_subscribe = true;

  bool receiver_connected = receiver_room->Connect(
      config_.url, config_.receiver_token, receiver_options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  // Register RPC handler on receiver
  std::atomic<int> rpc_calls_received{0};
  receiver_room->localParticipant()->registerRpcMethod(
      "echo",
      [&rpc_calls_received](
          const RpcInvocationData &data) -> std::optional<std::string> {
        rpc_calls_received++;
        return "echo: " + data.payload;
      });

  // Create caller room
  auto caller_room = std::make_unique<Room>();
  RoomOptions caller_options;
  caller_options.auto_subscribe = true;

  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, caller_options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  // Wait for receiver to be visible to caller
  bool receiver_visible =
      waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  // Perform RPC call
  std::string response = caller_room->localParticipant()->performRpc(
      receiver_identity, "echo", "hello world", 10.0);

  EXPECT_EQ(response, "echo: hello world");
  EXPECT_EQ(rpc_calls_received.load(), 1);

  // Cleanup
  receiver_room->localParticipant()->unregisterRpcMethod("echo");
  caller_room.reset();
  receiver_room.reset();
}

// Test maximum payload size (15KB)
TEST_F(RpcIntegrationTest, MaxPayloadSize) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  bool receiver_connected =
      receiver_room->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  // Register handler that echoes payload size
  receiver_room->localParticipant()->registerRpcMethod(
      "payload-size",
      [](const RpcInvocationData &data) -> std::optional<std::string> {
        return std::to_string(data.payload.size());
      });

  auto caller_room = std::make_unique<Room>();
  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  bool receiver_visible =
      waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  // Test with max payload size (15KB)
  std::string max_payload = generateRandomPayload(kMaxRpcPayloadSize);
  std::string response = caller_room->localParticipant()->performRpc(
      receiver_identity, "payload-size", max_payload, 30.0);

  EXPECT_EQ(response, std::to_string(kMaxRpcPayloadSize));

  receiver_room->localParticipant()->unregisterRpcMethod("payload-size");
  caller_room.reset();
  receiver_room.reset();
}

// Test RPC timeout
TEST_F(RpcIntegrationTest, RpcTimeout) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  bool receiver_connected =
      receiver_room->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  // Register handler that takes too long
  receiver_room->localParticipant()->registerRpcMethod(
      "slow-method",
      [](const RpcInvocationData &) -> std::optional<std::string> {
        std::this_thread::sleep_for(10s);
        return "done";
      });

  auto caller_room = std::make_unique<Room>();
  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  bool receiver_visible =
      waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  // Call with short timeout - should fail
  EXPECT_THROW(
      {
        caller_room->localParticipant()->performRpc(receiver_identity,
                                                    "slow-method", "", 2.0);
      },
      RpcError);

  receiver_room->localParticipant()->unregisterRpcMethod("slow-method");
  caller_room.reset();
  receiver_room.reset();
}

// Test RPC with unsupported method
TEST_F(RpcIntegrationTest, UnsupportedMethod) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  bool receiver_connected =
      receiver_room->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  auto caller_room = std::make_unique<Room>();
  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  bool receiver_visible =
      waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  // Call unregistered method
  try {
    caller_room->localParticipant()->performRpc(receiver_identity,
                                                "nonexistent-method", "", 5.0);
    FAIL() << "Expected RpcError for unsupported method";
  } catch (const RpcError &e) {
    EXPECT_EQ(static_cast<RpcError::ErrorCode>(e.code()),
              RpcError::ErrorCode::UNSUPPORTED_METHOD);
  }

  caller_room.reset();
  receiver_room.reset();
}

// Test RPC with application error
TEST_F(RpcIntegrationTest, ApplicationError) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  bool receiver_connected =
      receiver_room->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  // Register handler that throws an error
  receiver_room->localParticipant()->registerRpcMethod(
      "error-method",
      [](const RpcInvocationData &) -> std::optional<std::string> {
        throw std::runtime_error("intentional error");
      });

  auto caller_room = std::make_unique<Room>();
  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  bool receiver_visible =
      waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  try {
    caller_room->localParticipant()->performRpc(receiver_identity,
                                                "error-method", "", 5.0);
    FAIL() << "Expected RpcError for application error";
  } catch (const RpcError &e) {
    EXPECT_EQ(static_cast<RpcError::ErrorCode>(e.code()),
              RpcError::ErrorCode::APPLICATION_ERROR);
  }

  receiver_room->localParticipant()->unregisterRpcMethod("error-method");
  caller_room.reset();
  receiver_room.reset();
}

// Test multiple concurrent RPC calls
TEST_F(RpcIntegrationTest, ConcurrentRpcCalls) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  bool receiver_connected =
      receiver_room->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  std::atomic<int> calls_processed{0};
  receiver_room->localParticipant()->registerRpcMethod(
      "counter",
      [&calls_processed](
          const RpcInvocationData &data) -> std::optional<std::string> {
        int id = std::stoi(data.payload);
        calls_processed++;
        std::this_thread::sleep_for(100ms); // Simulate some work
        return std::to_string(id * 2);
      });

  auto caller_room = std::make_unique<Room>();
  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  bool receiver_visible =
      waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  const int num_concurrent_calls = 10;
  std::vector<std::thread> threads;
  std::atomic<int> successful_calls{0};

  for (int i = 0; i < num_concurrent_calls; ++i) {
    threads.emplace_back([&, i]() {
      try {
        std::string response = caller_room->localParticipant()->performRpc(
            receiver_identity, "counter", std::to_string(i), 30.0);
        int expected = i * 2;
        if (std::stoi(response) == expected) {
          successful_calls++;
        }
      } catch (const std::exception &e) {
        std::cerr << "RPC call " << i << " failed: " << e.what() << std::endl;
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  EXPECT_EQ(successful_calls.load(), num_concurrent_calls);
  EXPECT_EQ(calls_processed.load(), num_concurrent_calls);

  receiver_room->localParticipant()->unregisterRpcMethod("counter");
  caller_room.reset();
  receiver_room.reset();
}

// Integration test: Run for approximately 1 minute
TEST_F(RpcIntegrationTest, OneMinuteIntegration) {
  if (!config_.available) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_CALLER_TOKEN, and "
                    "LIVEKIT_RECEIVER_TOKEN not set";
  }

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  bool receiver_connected =
      receiver_room->Connect(config_.url, config_.receiver_token, options);
  ASSERT_TRUE(receiver_connected) << "Receiver failed to connect";

  std::string receiver_identity = receiver_room->localParticipant()->identity();

  std::atomic<int> total_received{0};
  std::atomic<size_t> total_bytes_received{0};

  receiver_room->localParticipant()->registerRpcMethod(
      "integration-test",
      [&](const RpcInvocationData &data) -> std::optional<std::string> {
        total_received++;
        total_bytes_received += data.payload.size();
        return "ack:" + std::to_string(data.payload.size());
      });

  auto caller_room = std::make_unique<Room>();
  bool caller_connected =
      caller_room->Connect(config_.url, config_.caller_token, options);
  ASSERT_TRUE(caller_connected) << "Caller failed to connect";

  bool receiver_visible =
      waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  // Run for 1 minute
  const auto test_duration = 60s;
  const auto start_time = std::chrono::steady_clock::now();

  std::atomic<int> total_sent{0};
  std::atomic<int> successful_calls{0};
  std::atomic<int> failed_calls{0};
  std::atomic<bool> running{true};

  // Sender thread
  std::thread sender([&]() {
    std::vector<size_t> payload_sizes = {100, 1024, 5 * 1024, 10 * 1024,
                                         kMaxRpcPayloadSize};
    int size_index = 0;

    while (running.load()) {
      size_t payload_size = payload_sizes[size_index % payload_sizes.size()];
      std::string payload = generateRandomPayload(payload_size);

      try {
        std::string response = caller_room->localParticipant()->performRpc(
            receiver_identity, "integration-test", payload, 30.0);
        if (response == "ack:" + std::to_string(payload_size)) {
          successful_calls++;
        }
      } catch (const std::exception &e) {
        failed_calls++;
      }

      total_sent++;
      size_index++;
      std::this_thread::sleep_for(100ms); // Rate limit
    }
  });

  // Wait for test duration
  while (std::chrono::steady_clock::now() - start_time < test_duration) {
    std::this_thread::sleep_for(1s);
    std::cout << "Progress: sent=" << total_sent.load()
              << " successful=" << successful_calls.load()
              << " failed=" << failed_calls.load()
              << " received=" << total_received.load() << std::endl;
  }

  running.store(false);
  sender.join();

  std::cout << "\n=== Integration Test Results (1 minute) ===" << std::endl;
  std::cout << "Total sent: " << total_sent.load() << std::endl;
  std::cout << "Successful: " << successful_calls.load() << std::endl;
  std::cout << "Failed: " << failed_calls.load() << std::endl;
  std::cout << "Total received: " << total_received.load() << std::endl;
  std::cout << "Total bytes received: " << total_bytes_received.load()
            << std::endl;

  EXPECT_GT(successful_calls.load(), 0);
  EXPECT_EQ(total_sent.load(), total_received.load());

  receiver_room->localParticipant()->unregisterRpcMethod("integration-test");
  caller_room.reset();
  receiver_room.reset();
}

} // namespace test
} // namespace livekit
