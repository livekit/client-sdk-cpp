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

// End-to-end coverage for text/byte data streams (registerTextStreamHandler /
// registerByteStreamHandler + TextStreamWriter / ByteStreamWriter). Requires a
// local SFU; see test_data_track.cpp for setup instructions. Run with:
//   ./build-debug/bin/livekit_integration_tests --gtest_filter=*DataStream*

#include <livekit/data_stream.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../common/test_common.h"

namespace livekit::test {

using namespace std::chrono_literals;

namespace {

constexpr auto kStreamWaitTimeout = 10s;

std::string makeTopic(const std::string& suffix) {
  return "test_topic_" + suffix + "_" + std::to_string(getTimestampUs());
}

/// Waits for a single incoming text stream on `topic` and captures its info
/// plus fully-read content. Registers itself as the topic's handler.
class TextStreamCollector {
public:
  ~TextStreamCollector() {
    if (recv_thread_.joinable()) {
      recv_thread_.join();
    }
  }
  void registerOn(Room& room, const std::string& topic) {
    // Handlers run on the Room event thread and must not block (per
    // registerTextStreamHandler's docs), since later chunk/close events for
    // this same reader are dispatched on that same thread. readAll() blocks
    // until close, so it has to happen on a separate thread.
    room.registerTextStreamHandler(
        topic, [this](std::shared_ptr<TextStreamReader> reader, const std::string& participant_identity) {
          recv_thread_ = std::thread([this, reader, participant_identity] {
            auto info = reader->info();
            auto text = reader->readAll();
            std::lock_guard<std::mutex> lock(mutex_);
            info_ = std::move(info);
            text_ = std::move(text);
            sender_identity_ = participant_identity;
            done_ = true;
            cv_.notify_all();
          });
        });
  }

  bool wait(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return done_; });
  }

  const TextStreamInfo& info() const { return info_; }
  const std::string& text() const { return text_; }
  const std::string& senderIdentity() const { return sender_identity_; }

private:
  std::thread recv_thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool done_ = false;
  TextStreamInfo info_;
  std::string text_;
  std::string sender_identity_;
};

/// Same idea as TextStreamCollector, for byte streams.
class ByteStreamCollector {
public:
  ~ByteStreamCollector() {
    if (recv_thread_.joinable()) {
      recv_thread_.join();
    }
  }
  void registerOn(Room& room, const std::string& topic) {
    // See TextStreamCollector::registerOn: the blocking readNext() loop must
    // not run on the Room event thread.
    room.registerByteStreamHandler(
        topic, [this](std::shared_ptr<ByteStreamReader> reader, const std::string& participant_identity) {
          recv_thread_ = std::thread([this, reader, participant_identity] {
            auto info = reader->info();
            std::vector<std::uint8_t> content;
            std::vector<std::uint8_t> chunk;
            while (reader->readNext(chunk)) {
              content.insert(content.end(), chunk.begin(), chunk.end());
            }
            std::lock_guard<std::mutex> lock(mutex_);
            info_ = std::move(info);
            content_ = std::move(content);
            sender_identity_ = participant_identity;
            done_ = true;
            cv_.notify_all();
          });
        });
  }

  bool wait(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return done_; });
  }

  const ByteStreamInfo& info() const { return info_; }
  const std::vector<std::uint8_t>& content() const { return content_; }
  const std::string& senderIdentity() const { return sender_identity_; }

private:
  std::thread recv_thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool done_ = false;
  ByteStreamInfo info_;
  std::vector<std::uint8_t> content_;
  std::string sender_identity_;
};

} // namespace

class DataStreamE2ETest : public LiveKitTestBase {};

TEST_F(DataStreamE2ETest, TextStreamRoundTripEndToEnd) {
  const auto topic = makeTopic("text");

  auto rooms = testRooms(2);
  auto& sender_room = rooms[0];
  auto& receiver_room = rooms[1];
  auto sender = lockLocalParticipant(*sender_room);
  const auto sender_identity = sender->identity();

  TextStreamCollector collector;
  collector.registerOn(*receiver_room, topic);

  TextStreamWriter writer(*sender, topic);
  writer.write("hello, ");
  writer.write("world!");
  writer.close();

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for text stream";
  EXPECT_EQ(collector.text(), "hello, world!");
  EXPECT_EQ(collector.senderIdentity(), sender_identity);
  EXPECT_EQ(collector.info().topic, topic);
  EXPECT_EQ(collector.info().mime_type, "text/plain");
}

// Regression coverage for the `text_header.reply_to_stream_id` field: writing
// a text stream with a reply-to id set should surface that id on the
// receiving side's TextStreamInfo. If this fails while the C++-side
// conversion (room_proto_converter.cpp: makeTextInfo) looks correct, the gap
// is upstream in the Rust FFI layer not forwarding the field.
TEST_F(DataStreamE2ETest, TextStreamReplyToStreamIdIsRoutedEndToEnd) {
  const auto topic = makeTopic("text_reply");
  const std::string reply_to_id = "original-stream-" + std::to_string(getTimestampUs());

  auto rooms = testRooms(2);
  auto& sender_room = rooms[0];
  auto& receiver_room = rooms[1];

  TextStreamCollector collector;
  collector.registerOn(*receiver_room, topic);

  TextStreamWriter writer(*lockLocalParticipant(*sender_room), topic, /*attributes=*/{}, /*stream_id=*/"",
                          /*total_size=*/std::nullopt, reply_to_id);
  writer.write("reply payload");
  writer.close();

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for text stream";
  EXPECT_EQ(collector.text(), "reply payload");
  ASSERT_TRUE(collector.info().reply_to_stream_id.has_value())
      << "reply_to_stream_id was not routed through FFI from the Rust SDK";
  EXPECT_EQ(collector.info().reply_to_stream_id.value(), reply_to_id);
}

TEST_F(DataStreamE2ETest, ByteStreamRoundTripEndToEnd) {
  const auto topic = makeTopic("bytes");
  const std::vector<std::uint8_t> payload{0x00, 0x01, 0x02, 0xFE, 0xFF, 'h', 'i'};

  auto rooms = testRooms(2);
  auto& sender_room = rooms[0];
  auto& receiver_room = rooms[1];
  const auto sender_identity = lockLocalParticipant(*sender_room)->identity();

  ByteStreamCollector collector;
  collector.registerOn(*receiver_room, topic);

  ByteStreamWriter writer(*lockLocalParticipant(*sender_room), /*name=*/"payload.bin", topic);
  writer.write(payload);
  writer.close();

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for byte stream";
  EXPECT_EQ(collector.content(), payload);
  EXPECT_EQ(collector.senderIdentity(), sender_identity);
  EXPECT_EQ(collector.info().topic, topic);
  EXPECT_EQ(collector.info().name, "payload.bin");
}

} // namespace livekit::test
