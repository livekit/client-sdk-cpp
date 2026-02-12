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

#include "../common/bridge_test_common.h"
#include <condition_variable>
#include <random>

namespace livekit_bridge {
namespace test {

static std::vector<std::uint8_t> generatePayload(size_t size) {
  static thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<std::uint8_t> data(size);
  for (auto &b : data) {
    b = static_cast<std::uint8_t>(dist(gen));
  }
  return data;
}

class BridgeDataRoundtripTest : public BridgeTestBase {};

// ---------------------------------------------------------------------------
// Test 1: Basic data track round-trip.
//
// Caller publishes a data track, receiver registers a callback, caller sends
// frames, receiver verifies payload integrity.
// ---------------------------------------------------------------------------
TEST_F(BridgeDataRoundtripTest, DataFrameRoundTrip) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Data Frame Round-Trip Test ===" << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  const std::string track_name = "roundtrip-data";
  const std::string caller_identity = "rpc-caller";

  auto data_track = caller.createDataTrack(track_name);
  ASSERT_NE(data_track, nullptr);
  ASSERT_TRUE(data_track->isPublished());

  std::cout << "Data track published." << std::endl;

  std::mutex rx_mutex;
  std::condition_variable rx_cv;
  std::vector<std::vector<std::uint8_t>> received_payloads;
  std::vector<std::optional<std::uint64_t>> received_timestamps;

  receiver.setOnDataFrameCallback(
      caller_identity, track_name,
      [&](const std::vector<std::uint8_t> &payload,
          std::optional<std::uint64_t> user_timestamp) {
        std::lock_guard<std::mutex> lock(rx_mutex);
        received_payloads.push_back(payload);
        received_timestamps.push_back(user_timestamp);
        rx_cv.notify_all();
      });

  // Give the subscription time to be established
  std::this_thread::sleep_for(3s);

  std::cout << "Sending data frames..." << std::endl;

  const int num_frames = 10;
  std::vector<std::vector<std::uint8_t>> sent_payloads;
  std::vector<std::uint64_t> sent_timestamps;

  for (int i = 0; i < num_frames; ++i) {
    auto payload = generatePayload(256);
    auto ts = static_cast<std::uint64_t>(i * 1000);
    sent_payloads.push_back(payload);
    sent_timestamps.push_back(ts);

    bool pushed = data_track->pushFrame(payload, ts);
    EXPECT_TRUE(pushed) << "pushFrame failed for frame " << i;

    std::this_thread::sleep_for(100ms);
  }

  {
    std::unique_lock<std::mutex> lock(rx_mutex);
    rx_cv.wait_for(lock, 10s, [&] {
      return received_payloads.size() >= static_cast<size_t>(num_frames);
    });
  }

  std::cout << "\nResults:" << std::endl;
  std::cout << "  Frames sent:     " << num_frames << std::endl;
  std::cout << "  Frames received: " << received_payloads.size() << std::endl;

  EXPECT_EQ(received_payloads.size(), static_cast<size_t>(num_frames))
      << "Should receive all sent frames";

  for (size_t i = 0;
       i < std::min(received_payloads.size(), sent_payloads.size()); ++i) {
    EXPECT_EQ(received_payloads[i], sent_payloads[i])
        << "Payload mismatch at frame " << i;
    ASSERT_TRUE(received_timestamps[i].has_value())
        << "Missing timestamp at frame " << i;
    EXPECT_EQ(received_timestamps[i].value(), sent_timestamps[i])
        << "Timestamp mismatch at frame " << i;
  }

  receiver.clearOnDataFrameCallback(caller_identity, track_name);
}

// ---------------------------------------------------------------------------
// Test 2: Data track with callback registered AFTER track is published.
//
// Exercises the bridge's pending_remote_data_tracks_ mechanism: the remote
// data track is published before the receiver registers its callback.
// ---------------------------------------------------------------------------
TEST_F(BridgeDataRoundtripTest, LateCallbackRegistration) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Data Late Callback Registration Test ==="
            << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  const std::string track_name = "late-callback-data";
  const std::string caller_identity = "rpc-caller";

  auto data_track = caller.createDataTrack(track_name);
  ASSERT_NE(data_track, nullptr);

  std::cout << "Data track published, waiting before registering callback..."
            << std::endl;

  std::this_thread::sleep_for(3s);

  std::atomic<int> frames_received{0};
  std::condition_variable rx_cv;
  std::mutex rx_mutex;

  receiver.setOnDataFrameCallback(
      caller_identity, track_name,
      [&](const std::vector<std::uint8_t> &,
          std::optional<std::uint64_t>) {
        frames_received++;
        rx_cv.notify_all();
      });

  std::cout << "Callback registered (late), sending frames..." << std::endl;

  std::this_thread::sleep_for(2s);

  const int num_frames = 5;
  for (int i = 0; i < num_frames; ++i) {
    auto payload = generatePayload(128);
    data_track->pushFrame(payload);
    std::this_thread::sleep_for(100ms);
  }

  {
    std::unique_lock<std::mutex> lock(rx_mutex);
    rx_cv.wait_for(lock, 10s, [&] {
      return frames_received.load() >= num_frames;
    });
  }

  std::cout << "Frames received: " << frames_received.load() << std::endl;

  EXPECT_EQ(frames_received.load(), num_frames)
      << "Late callback should still receive all frames";

  receiver.clearOnDataFrameCallback(caller_identity, track_name);
}

// ---------------------------------------------------------------------------
// Test 3: Varying payload sizes.
//
// Tests data track with payloads from tiny (1 byte) to large (64KB) to
// verify the bridge handles different frame sizes correctly.
// ---------------------------------------------------------------------------
TEST_F(BridgeDataRoundtripTest, VaryingPayloadSizes) {
  skipIfNotConfigured();

  std::cout << "\n=== Bridge Data Varying Payload Sizes Test ===" << std::endl;

  LiveKitBridge caller;
  LiveKitBridge receiver;

  ASSERT_TRUE(connectPair(caller, receiver));

  const std::string track_name = "size-test-data";
  const std::string caller_identity = "rpc-caller";

  auto data_track = caller.createDataTrack(track_name);
  ASSERT_NE(data_track, nullptr);

  std::mutex rx_mutex;
  std::condition_variable rx_cv;
  std::vector<size_t> received_sizes;

  receiver.setOnDataFrameCallback(
      caller_identity, track_name,
      [&](const std::vector<std::uint8_t> &payload,
          std::optional<std::uint64_t>) {
        std::lock_guard<std::mutex> lock(rx_mutex);
        received_sizes.push_back(payload.size());
        rx_cv.notify_all();
      });

  std::this_thread::sleep_for(3s);

  std::vector<size_t> test_sizes = {1, 10, 100, 1024, 4096, 16384, 65536};

  std::cout << "Sending " << test_sizes.size()
            << " frames with varying sizes..." << std::endl;

  for (size_t sz : test_sizes) {
    auto payload = generatePayload(sz);
    bool pushed = data_track->pushFrame(payload);
    EXPECT_TRUE(pushed) << "pushFrame failed for size " << sz;
    std::this_thread::sleep_for(200ms);
  }

  {
    std::unique_lock<std::mutex> lock(rx_mutex);
    rx_cv.wait_for(lock, 15s, [&] {
      return received_sizes.size() >= test_sizes.size();
    });
  }

  std::cout << "Received " << received_sizes.size() << "/"
            << test_sizes.size() << " frames." << std::endl;

  EXPECT_EQ(received_sizes.size(), test_sizes.size());

  for (size_t i = 0;
       i < std::min(received_sizes.size(), test_sizes.size()); ++i) {
    EXPECT_EQ(received_sizes[i], test_sizes[i])
        << "Size mismatch at index " << i;
  }

  receiver.clearOnDataFrameCallback(caller_identity, track_name);
}

// ---------------------------------------------------------------------------
// Test 4: Connect → publish data track → disconnect cycle.
// ---------------------------------------------------------------------------
TEST_F(BridgeDataRoundtripTest, ConnectPublishDisconnectCycle) {
  skipIfNotConfigured();

  const int cycles = config_.test_iterations;
  std::cout << "\n=== Bridge Data Connect/Disconnect Cycles ===" << std::endl;
  std::cout << "Cycles: " << cycles << std::endl;

  for (int i = 0; i < cycles; ++i) {
    {
      LiveKitBridge bridge;
      livekit::RoomOptions options;
      options.auto_subscribe = true;

      bool connected =
          bridge.connect(config_.url, config_.caller_token, options);
      ASSERT_TRUE(connected) << "Cycle " << i << ": connect failed";

      auto track = bridge.createDataTrack("cycle-data");
      ASSERT_NE(track, nullptr);

      for (int f = 0; f < 5; ++f) {
        auto payload = generatePayload(256);
        track->pushFrame(payload);
      }
    } // bridge destroyed here → disconnect + shutdown

    std::cout << "  Cycle " << (i + 1) << "/" << cycles << " OK" << std::endl;
    std::this_thread::sleep_for(1s);
  }
}

} // namespace test
} // namespace livekit_bridge
