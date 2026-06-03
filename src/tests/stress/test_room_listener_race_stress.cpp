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

#include <livekit/local_data_track.h>

#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr int kListenerRaceIterations = 5;
constexpr int kTracksPerPublisher = 3;
constexpr auto kConnectTimeout = 30s;

void addStressError(std::vector<std::string>& errors, std::mutex& errors_mutex, const std::string& message) {
  std::lock_guard<std::mutex> lock(errors_mutex);
  errors.push_back(message);
}

std::string describeStressErrors(const std::vector<std::string>& errors) {
  std::ostringstream out;
  for (std::size_t i = 0; i < errors.size(); ++i) {
    if (i != 0) {
      out << "; ";
    }
    out << errors[i];
  }
  return errors.empty() ? "<none>" : out.str();
}

} // namespace

class RoomListenerRaceStressTest : public LiveKitTestBase {};

TEST_F(RoomListenerRaceStressTest, ConnectFailDestroyReconnectAndPublishDataTracksConcurrently) {
  if (!config_.available) {
    throw std::runtime_error("LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B not set");
  }

  for (int iteration = 0; iteration < kListenerRaceIterations; ++iteration) {
    std::atomic<int> connected_publishers{0};
    std::atomic<bool> publish_start{false};
    std::mutex errors_mutex;
    std::vector<std::string> errors;
    std::vector<std::thread> publishers;
    publishers.reserve(2);

    const std::array<std::string, 2> tokens = {config_.token_a, config_.token_b};

    for (std::size_t publisher_index = 0; publisher_index < tokens.size(); ++publisher_index) {
      publishers.emplace_back([&, publisher_index]() {
        try {
          RoomOptions options;
          options.auto_subscribe = true;

          {
            Room failed_room;
            if (failed_room.connect("ws://127.0.0.1:9", tokens[publisher_index], options)) {
              addStressError(errors, errors_mutex, "unexpected successful failed connect");
              return;
            }
          }

          Room room;
          if (!room.connect(config_.url, tokens[publisher_index], options)) {
            addStressError(errors, errors_mutex, "valid connect failed");
            return;
          }

          auto local_participant = room.localParticipant().lock();
          if (local_participant == nullptr) {
            addStressError(errors, errors_mutex, "local participant missing after valid connect");
            return;
          }

          connected_publishers.fetch_add(1, std::memory_order_release);
          while (!publish_start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
          }

          for (int track_index = 0; track_index < kTracksPerPublisher; ++track_index) {
            const std::string track_name = "listener-race-" + std::to_string(iteration) + "-" +
                                           std::to_string(publisher_index) + "-" + std::to_string(track_index);
            auto publish_result = local_participant->publishDataTrack(track_name);
            if (!publish_result) {
              addStressError(errors, errors_mutex,
                             "publishDataTrack failed for " + track_name + ": " + publish_result.error().message);
              continue;
            }

            const auto& track = publish_result.value();
            if (!track || !track->isPublished()) {
              addStressError(errors, errors_mutex, "published data track not marked published: " + track_name);
            }
          }

          std::this_thread::sleep_for(50ms);
        } catch (const std::exception& e) {
          addStressError(errors, errors_mutex, std::string("exception in publisher thread: ") + e.what());
        } catch (...) {
          addStressError(errors, errors_mutex, "unknown exception in publisher thread");
        }
      });
    }

    const auto connect_deadline = std::chrono::steady_clock::now() + kConnectTimeout;
    while (connected_publishers.load(std::memory_order_acquire) < static_cast<int>(tokens.size()) &&
           std::chrono::steady_clock::now() < connect_deadline) {
      {
        std::lock_guard<std::mutex> lock(errors_mutex);
        if (!errors.empty()) {
          break;
        }
      }
      std::this_thread::sleep_for(10ms);
    }

    if (connected_publishers.load(std::memory_order_acquire) != static_cast<int>(tokens.size())) {
      addStressError(errors, errors_mutex, "timed out waiting for publishers to connect");
    }

    publish_start.store(true, std::memory_order_release);
    for (auto& publisher : publishers) {
      if (publisher.joinable()) {
        publisher.join();
      }
    }

    std::vector<std::string> errors_snapshot;
    {
      std::lock_guard<std::mutex> lock(errors_mutex);
      errors_snapshot = errors;
    }
    ASSERT_TRUE(errors_snapshot.empty()) << "iteration " << iteration << ": " << describeStressErrors(errors_snapshot);
  }
}

} // namespace livekit::test
