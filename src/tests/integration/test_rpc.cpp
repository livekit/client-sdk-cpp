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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "../common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

// RPC v1 packet payload limit was ~15 KiB; these tests validate that RPC v2 can exceed it.
constexpr size_t kRpcV1PayloadLimit = 15 * 1024;
constexpr size_t kLargeRpcPayloadSize = 20 * 1000;

// Test configuration from environment variables
struct RpcTestConfig {
  std::string url;
  std::string token_a;
  std::string token_b;
  bool available = false;

  static RpcTestConfig fromEnv() {
    RpcTestConfig config;
    const char* url = std::getenv("LIVEKIT_URL");
    const char* token_a = std::getenv("LIVEKIT_TOKEN_A");
    const char* token_b = std::getenv("LIVEKIT_TOKEN_B");

    if (url && token_a && token_b) {
      config.url = url;
      config.token_a = token_a;
      config.token_b = token_b;
      config.available = true;
    }
    return config;
  }
};

// Sample sentences for generating compressible payloads
static const std::vector<std::string> kSampleSentences = {
    "The quick brown fox jumps over the lazy dog. ",
    "LiveKit is a real-time communication platform for building video and "
    "audio applications. ",
    "RPC allows participants to call methods on remote peers with "
    "request-response semantics. ",
    "This test measures the performance and reliability of the RPC system. ",
    "WebRTC enables peer-to-peer communication for real-time media streaming. ",
};

// Generate a payload of specified size using repeating sentences (compressible)
std::string generateRandomPayload(size_t size) {
  static std::random_device rd;
  static std::mt19937 gen(static_cast<std::mt19937::result_type>(rd()));
  static std::uniform_int_distribution<size_t> dis(0, kSampleSentences.size() - 1);

  std::string result;
  result.reserve(size);

  // Start with a random sentence to add some variation between payloads
  size_t start_idx = dis(gen);

  while (result.size() < size) {
    const std::string& sentence = kSampleSentences[(start_idx + result.size()) % kSampleSentences.size()];
    result += sentence;
  }

  return result.substr(0, size);
}

size_t checksumPayload(const std::string& payload) {
  size_t checksum = 0;
  for (char c : payload) {
    checksum += static_cast<unsigned char>(c);
  }
  return checksum;
}

class RpcIntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogLevel::Info);
    config_ = RpcTestConfig::fromEnv();
  }

  void TearDown() override { livekit::shutdown(); }

  RpcTestConfig config_;
};

// Test basic RPC round-trip
TEST_F(RpcIntegrationTest, BasicRpcRoundTrip) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  // Create receiver room
  auto receiver_room = std::make_unique<Room>();
  RoomOptions receiver_options;
  receiver_options.auto_subscribe = true;

  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, receiver_options)) << "Receiver failed to connect";

  std::string receiver_identity = lockLocalParticipant(*receiver_room)->identity();

  // Register RPC handler on receiver - returns size and checksum instead of
  // full payload
  std::atomic<int> rpc_calls_received{0};
  std::string caller_identity;
  std::string observed_request_id;
  std::string observed_caller_identity;
  double observed_response_timeout = 0.0;
  std::mutex observed_mutex;
  lockLocalParticipant(*receiver_room)
      ->registerRpcMethod("echo", [&](const RpcInvocationData& data) -> std::optional<std::string> {
        rpc_calls_received++;
        {
          const std::scoped_lock<std::mutex> lock(observed_mutex);
          observed_request_id = data.request_id;
          observed_caller_identity = data.caller_identity;
          observed_response_timeout = data.response_timeout_sec;
        }
        EXPECT_EQ(data.payload, "hello world");
        return "echo:" + std::to_string(data.payload.size()) + ":" + std::to_string(checksumPayload(data.payload));
      });

  // Create caller room
  auto caller_room = std::make_unique<Room>();
  RoomOptions caller_options;
  caller_options.auto_subscribe = true;

  ASSERT_TRUE(caller_room->connect(config_.url, config_.token_a, caller_options)) << "Caller failed to connect";
  caller_identity = lockLocalParticipant(*caller_room)->identity();

  // Wait for receiver to be visible to caller
  bool receiver_visible = waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  // Perform RPC call
  std::string test_payload = "hello world";
  std::string response = lockLocalParticipant(*caller_room)->performRpc(receiver_identity, "echo", test_payload, 10.0);

  // Verify response contains correct size and checksum
  std::string expected_response =
      "echo:" + std::to_string(test_payload.size()) + ":" + std::to_string(checksumPayload(test_payload));
  EXPECT_EQ(response, expected_response);
  EXPECT_EQ(rpc_calls_received.load(), 1);
  {
    const std::scoped_lock<std::mutex> lock(observed_mutex);
    EXPECT_FALSE(observed_request_id.empty());
    EXPECT_EQ(observed_caller_identity, caller_identity);
    EXPECT_GT(observed_response_timeout, 0.0);
    EXPECT_LE(observed_response_timeout, 10.0);
  }

  // Cleanup
  lockLocalParticipant(*receiver_room)->unregisterRpcMethod("echo");
  caller_room.reset();
  receiver_room.reset();
}

TEST_F(RpcIntegrationTest, LargeRequestPayloadRoundTrip) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  std::string receiver_identity = lockLocalParticipant(*receiver_room)->identity();

  std::atomic<size_t> received_payload_size{0};
  std::atomic<size_t> received_payload_checksum{0};
  lockLocalParticipant(*receiver_room)
      ->registerRpcMethod("large-request", [&](const RpcInvocationData& data) -> std::optional<std::string> {
        received_payload_size = data.payload.size();
        received_payload_checksum = checksumPayload(data.payload);
        return std::to_string(data.payload.size()) + ":" + std::to_string(checksumPayload(data.payload));
      });

  auto caller_room = std::make_unique<Room>();
  ASSERT_TRUE(caller_room->connect(config_.url, config_.token_a, options)) << "Caller failed to connect";

  bool receiver_visible = waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  std::string max_payload = generateRandomPayload(kLargeRpcPayloadSize);
  ASSERT_GT(max_payload.size(), kRpcV1PayloadLimit);
  std::string response =
      lockLocalParticipant(*caller_room)->performRpc(receiver_identity, "large-request", max_payload, 30.0);

  EXPECT_EQ(response, std::to_string(kLargeRpcPayloadSize) + ":" + std::to_string(checksumPayload(max_payload)));
  EXPECT_EQ(received_payload_size.load(), kLargeRpcPayloadSize);
  EXPECT_EQ(received_payload_checksum.load(), checksumPayload(max_payload));

  lockLocalParticipant(*receiver_room)->unregisterRpcMethod("large-request");
  caller_room.reset();
  receiver_room.reset();
}

TEST_F(RpcIntegrationTest, LargeResponsePayloadRoundTrip) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  std::string receiver_identity = lockLocalParticipant(*receiver_room)->identity();
  const std::string large_response = generateRandomPayload(kLargeRpcPayloadSize);
  ASSERT_GT(large_response.size(), kRpcV1PayloadLimit);

  lockLocalParticipant(*receiver_room)
      ->registerRpcMethod("large-response", [&](const RpcInvocationData& data) -> std::optional<std::string> {
        EXPECT_EQ(data.payload, "send-large-response");
        return large_response;
      });

  auto caller_room = std::make_unique<Room>();
  ASSERT_TRUE(caller_room->connect(config_.url, config_.token_a, options)) << "Caller failed to connect";

  bool receiver_visible = waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  std::string response =
      lockLocalParticipant(*caller_room)->performRpc(receiver_identity, "large-response", "send-large-response", 30.0);

  EXPECT_EQ(response.size(), large_response.size());
  EXPECT_EQ(checksumPayload(response), checksumPayload(large_response));
  EXPECT_EQ(response, large_response);

  lockLocalParticipant(*receiver_room)->unregisterRpcMethod("large-response");
  caller_room.reset();
  receiver_room.reset();
}

TEST_F(RpcIntegrationTest, LargeRequestAndResponseRoundTrip) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  std::string receiver_identity = lockLocalParticipant(*receiver_room)->identity();
  const std::string large_response = generateRandomPayload(kLargeRpcPayloadSize + 123);
  ASSERT_GT(large_response.size(), kRpcV1PayloadLimit);

  lockLocalParticipant(*receiver_room)
      ->registerRpcMethod("large-both", [&](const RpcInvocationData& data) -> std::optional<std::string> {
        EXPECT_GT(data.payload.size(), kRpcV1PayloadLimit);
        return large_response;
      });

  auto caller_room = std::make_unique<Room>();
  ASSERT_TRUE(caller_room->connect(config_.url, config_.token_a, options)) << "Caller failed to connect";

  bool receiver_visible = waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  const std::string large_request = generateRandomPayload(kLargeRpcPayloadSize);
  ASSERT_GT(large_request.size(), kRpcV1PayloadLimit);

  std::string response =
      lockLocalParticipant(*caller_room)->performRpc(receiver_identity, "large-both", large_request, 30.0);

  EXPECT_EQ(response.size(), large_response.size());
  EXPECT_EQ(checksumPayload(response), checksumPayload(large_response));
  EXPECT_EQ(response, large_response);

  lockLocalParticipant(*receiver_room)->unregisterRpcMethod("large-both");
  caller_room.reset();
  receiver_room.reset();
}

// Test RPC timeout
TEST_F(RpcIntegrationTest, RpcTimeout) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  std::string receiver_identity = lockLocalParticipant(*receiver_room)->identity();

  // Register handler that takes too long
  lockLocalParticipant(*receiver_room)
      ->registerRpcMethod("slow-method", [](const RpcInvocationData&) -> std::optional<std::string> {
        std::this_thread::sleep_for(10s);
        return "done";
      });

  auto caller_room = std::make_unique<Room>();
  ASSERT_TRUE(caller_room->connect(config_.url, config_.token_a, options)) << "Caller failed to connect";

  bool receiver_visible = waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  // Call with short timeout - should fail
  EXPECT_THROW(
      { lockLocalParticipant(*caller_room)->performRpc(receiver_identity, "slow-method", "", 2.0); }, RpcError);

  lockLocalParticipant(*receiver_room)->unregisterRpcMethod("slow-method");
  caller_room.reset();
  receiver_room.reset();
}

// Test RPC with unsupported method
TEST_F(RpcIntegrationTest, UnsupportedMethod) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  std::string receiver_identity = lockLocalParticipant(*receiver_room)->identity();

  auto caller_room = std::make_unique<Room>();
  ASSERT_TRUE(caller_room->connect(config_.url, config_.token_a, options)) << "Caller failed to connect";

  bool receiver_visible = waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  // Call unregistered method
  try {
    lockLocalParticipant(*caller_room)->performRpc(receiver_identity, "nonexistent-method", "", 5.0);
    FAIL() << "Expected RpcError for unsupported method";
  } catch (const RpcError& e) {
    EXPECT_EQ(static_cast<RpcError::ErrorCode>(e.code()), RpcError::ErrorCode::UNSUPPORTED_METHOD);
  }

  caller_room.reset();
  receiver_room.reset();
}

// Test RPC with application error
TEST_F(RpcIntegrationTest, ApplicationError) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  std::string receiver_identity = lockLocalParticipant(*receiver_room)->identity();

  // Register handler that throws an error
  lockLocalParticipant(*receiver_room)
      ->registerRpcMethod("error-method", [](const RpcInvocationData&) -> std::optional<std::string> {
        throw std::runtime_error("intentional error");
      });

  auto caller_room = std::make_unique<Room>();
  ASSERT_TRUE(caller_room->connect(config_.url, config_.token_a, options)) << "Caller failed to connect";

  bool receiver_visible = waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  try {
    lockLocalParticipant(*caller_room)->performRpc(receiver_identity, "error-method", "", 5.0);
    FAIL() << "Expected RpcError for application error";
  } catch (const RpcError& e) {
    EXPECT_EQ(static_cast<RpcError::ErrorCode>(e.code()), RpcError::ErrorCode::APPLICATION_ERROR);
  }

  lockLocalParticipant(*receiver_room)->unregisterRpcMethod("error-method");
  caller_room.reset();
  receiver_room.reset();
}

// Test multiple concurrent RPC calls
TEST_F(RpcIntegrationTest, ConcurrentRpcCalls) {
  EXPECT_TRUE(config_.available) << "Missing integration configuration";

  auto receiver_room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;

  ASSERT_TRUE(receiver_room->connect(config_.url, config_.token_b, options)) << "Receiver failed to connect";

  std::string receiver_identity = lockLocalParticipant(*receiver_room)->identity();

  std::atomic<int> calls_processed{0};
  lockLocalParticipant(*receiver_room)
      ->registerRpcMethod("counter", [&calls_processed](const RpcInvocationData& data) -> std::optional<std::string> {
        calls_processed++;
        std::this_thread::sleep_for(100ms); // Simulate some work
        return data.payload + ":handled";
      });

  auto caller_room = std::make_unique<Room>();
  ASSERT_TRUE(caller_room->connect(config_.url, config_.token_a, options)) << "Caller failed to connect";

  bool receiver_visible = waitForParticipant(caller_room.get(), receiver_identity, 10s);
  ASSERT_TRUE(receiver_visible) << "Receiver not visible to caller";

  // Hold the local participant alive for the duration of the worker threads so
  // it cannot expire mid-call while RPCs are in flight.
  ASSERT_NO_THROW(lockLocalParticipant(*caller_room));

  const int num_concurrent_calls = 5;
  std::vector<std::thread> threads;
  std::atomic<int> successful_calls{0};

  for (int i = 0; i < num_concurrent_calls; ++i) {
    threads.emplace_back([&, i]() {
      try {
        auto caller_lp = lockLocalParticipant(*caller_room);
        ASSERT_NE(caller_lp, nullptr);
        const std::string payload =
            "call-" + std::to_string(i) + ":" + std::string(256 + i, static_cast<char>('a' + i));
        std::string response = caller_lp->performRpc(receiver_identity, "counter", payload, 30.0);
        if (response == payload + ":handled") {
          successful_calls++;
        }
      } catch (const std::exception& e) {
        std::cerr << "RPC call " << i << " failed: " << e.what() << std::endl;
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(successful_calls.load(), num_concurrent_calls);
  EXPECT_EQ(calls_processed.load(), num_concurrent_calls);

  lockLocalParticipant(*receiver_room)->unregisterRpcMethod("counter");
  caller_room.reset();
  receiver_room.reset();
}

} // namespace livekit::test
