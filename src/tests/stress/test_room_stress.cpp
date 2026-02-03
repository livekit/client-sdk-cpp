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
#include <gtest/gtest.h>
#include <livekit/livekit.h>
#include <memory>
#include <thread>
#include <vector>

namespace livekit {
namespace test {

class RoomStressTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogSink::kConsole); }

  void TearDown() override { livekit::shutdown(); }
};

// Stress test: Rapid Room object creation and destruction
TEST_F(RoomStressTest, RapidRoomCreation) {
  const int num_iterations = 1000;

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_iterations; ++i) {
    Room room;
    ASSERT_EQ(room.localParticipant(), nullptr);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Created and destroyed " << num_iterations << " Room objects in "
            << duration.count() << "ms"
            << " (" << (num_iterations * 1000.0 / duration.count())
            << " rooms/sec)" << std::endl;
}

// Stress test: Multiple simultaneous Room objects
TEST_F(RoomStressTest, MultipleSimultaneousRooms) {
  const int num_rooms = 100;
  std::vector<std::unique_ptr<Room>> rooms;
  rooms.reserve(num_rooms);

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_rooms; ++i) {
    rooms.push_back(std::make_unique<Room>());
  }

  // Verify all rooms are valid
  for (const auto &room : rooms) {
    ASSERT_NE(room, nullptr);
    ASSERT_EQ(room->localParticipant(), nullptr);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Held " << num_rooms << " Room objects simultaneously in "
            << duration.count() << "ms" << std::endl;

  // Rooms are destroyed when vector goes out of scope
}

// Stress test: Concurrent Room creation from multiple threads
TEST_F(RoomStressTest, ConcurrentRoomCreation) {
  const int num_threads = 4;
  const int rooms_per_thread = 100;
  std::atomic<int> total_rooms{0};
  std::vector<std::thread> threads;

  auto start = std::chrono::high_resolution_clock::now();

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&total_rooms, rooms_per_thread]() {
      for (int i = 0; i < rooms_per_thread; ++i) {
        Room room;
        if (room.localParticipant() == nullptr) {
          total_rooms.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  EXPECT_EQ(total_rooms.load(), num_threads * rooms_per_thread);

  std::cout << "Created " << total_rooms.load() << " Room objects across "
            << num_threads << " threads in " << duration.count() << "ms"
            << std::endl;
}

// Stress test: RoomOptions creation and copying
TEST_F(RoomStressTest, RoomOptionsStress) {
  const int num_iterations = 10000;

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_iterations; ++i) {
    RoomOptions options;
    options.auto_subscribe = (i % 2 == 0);
    options.dynacast = (i % 3 == 0);

    RtcConfig rtc_config;
    for (int j = 0; j < 5; ++j) {
      IceServer server;
      server.url = "stun:stun" + std::to_string(j) + ".example.com:19302";
      server.username = "user" + std::to_string(j);
      server.credential = "pass" + std::to_string(j);
      rtc_config.ice_servers.push_back(server);
    }
    options.rtc_config = rtc_config;

    // Copy the options
    RoomOptions copy = options;
    ASSERT_EQ(copy.auto_subscribe, options.auto_subscribe);
    ASSERT_EQ(copy.dynacast, options.dynacast);
    ASSERT_TRUE(copy.rtc_config.has_value());
    ASSERT_EQ(copy.rtc_config->ice_servers.size(), 5);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Created and copied " << num_iterations << " RoomOptions in "
            << duration.count() << "ms" << std::endl;
}

// Stress test: Stream handler registration and unregistration
TEST_F(RoomStressTest, StreamHandlerRegistrationStress) {
  Room room;
  const int num_topics = 100;

  auto start = std::chrono::high_resolution_clock::now();

  // Register many handlers
  for (int i = 0; i < num_topics; ++i) {
    std::string topic = "topic_" + std::to_string(i);
    room.registerTextStreamHandler(
        topic, [](std::shared_ptr<TextStreamReader>, const std::string &) {});
    room.registerByteStreamHandler(
        topic, [](std::shared_ptr<ByteStreamReader>, const std::string &) {});
  }

  // Unregister all handlers
  for (int i = 0; i < num_topics; ++i) {
    std::string topic = "topic_" + std::to_string(i);
    room.unregisterTextStreamHandler(topic);
    room.unregisterByteStreamHandler(topic);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Registered and unregistered " << (num_topics * 2)
            << " stream handlers in " << duration.count() << "ms" << std::endl;
}

// Server-dependent stress tests
class RoomServerStressTest : public ::testing::Test {
protected:
  void SetUp() override {
    livekit::initialize(livekit::LogSink::kConsole);

    const char *url_env = std::getenv("LIVEKIT_URL");
    const char *token_env = std::getenv("LIVEKIT_TOKEN");

    if (url_env && token_env) {
      server_url_ = url_env;
      token_ = token_env;
      server_available_ = true;
    }
  }

  void TearDown() override { livekit::shutdown(); }

  bool server_available_ = false;
  std::string server_url_;
  std::string token_;
};

TEST_F(RoomServerStressTest, RepeatedConnectDisconnect) {
  if (!server_available_) {
    GTEST_SKIP()
        << "LIVEKIT_URL and LIVEKIT_TOKEN not set, skipping server stress test";
  }

  const int num_iterations = 10;

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_iterations; ++i) {
    Room room;
    RoomOptions options;

    bool connected = room.Connect(server_url_, token_, options);
    if (connected) {
      ASSERT_NE(room.localParticipant(), nullptr);
    }
    // Room disconnects when it goes out of scope
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

  std::cout << "Completed " << num_iterations
            << " connect/disconnect cycles in " << duration.count() << "s"
            << std::endl;
}

} // namespace test
} // namespace livekit
