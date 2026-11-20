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
// registerByteStreamHandler + LocalParticipant::sendText / sendBytes / sendFile
// and TextStreamWriter / ByteStreamWriter). Requires a local SFU; see
// test_data_track.cpp for setup instructions. Run with:
//   ./build-debug/bin/livekit_integration_tests --gtest_filter=*DataStream*

#include <livekit/data_stream.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../common/test_common.h"
#include "ffi.pb.h"
#include "ffi_client.h"

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
    // If the test failed before the stream ended, wake the reader so joining
    // doesn't hang the test binary.
    std::unique_lock<std::mutex> lock(mutex_);
    auto reader = reader_.lock();
    lock.unlock();
    if (reader) {
      reader->close();
    }
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
          {
            std::lock_guard<std::mutex> lock(mutex_);
            reader_ = reader;
          }
          recv_thread_ = std::thread([this, reader, participant_identity] {
            std::string text;
            std::optional<std::string> error;
            try {
              text = reader->readAll();
            } catch (const DataStreamError& e) {
              error = e.what();
            }
            std::lock_guard<std::mutex> lock(mutex_);
            // Read info after the stream ends so trailer attributes are merged.
            info_ = reader->info();
            text_ = std::move(text);
            error_ = std::move(error);
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
  const std::optional<std::string>& error() const { return error_; }
  const std::string& senderIdentity() const { return sender_identity_; }

private:
  std::weak_ptr<TextStreamReader> reader_;
  std::thread recv_thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool done_ = false;
  TextStreamInfo info_;
  std::string text_;
  std::optional<std::string> error_;
  std::string sender_identity_;
};

/// Same idea as TextStreamCollector, for byte streams.
class ByteStreamCollector {
public:
  ~ByteStreamCollector() {
    // If the test failed before the stream ended, wake the reader so joining
    // doesn't hang the test binary.
    std::unique_lock<std::mutex> lock(mutex_);
    auto reader = reader_.lock();
    lock.unlock();
    if (reader) {
      reader->close();
    }
    if (recv_thread_.joinable()) {
      recv_thread_.join();
    }
  }
  void registerOn(Room& room, const std::string& topic) {
    // See TextStreamCollector::registerOn: the blocking readNext() loop must
    // not run on the Room event thread.
    room.registerByteStreamHandler(
        topic, [this](std::shared_ptr<ByteStreamReader> reader, const std::string& participant_identity) {
          {
            std::lock_guard<std::mutex> lock(mutex_);
            reader_ = reader;
          }
          recv_thread_ = std::thread([this, reader, participant_identity] {
            std::vector<std::uint8_t> content;
            std::vector<std::uint8_t> chunk;
            std::optional<std::string> error;
            try {
              while (reader->readNext(chunk)) {
                content.insert(content.end(), chunk.begin(), chunk.end());
              }
            } catch (const DataStreamError& e) {
              error = e.what();
            }
            std::lock_guard<std::mutex> lock(mutex_);
            info_ = reader->info();
            content_ = std::move(content);
            error_ = std::move(error);
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
  const std::optional<std::string>& error() const { return error_; }
  const std::string& senderIdentity() const { return sender_identity_; }

private:
  std::weak_ptr<ByteStreamReader> reader_;
  std::thread recv_thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool done_ = false;
  ByteStreamInfo info_;
  std::vector<std::uint8_t> content_;
  std::optional<std::string> error_;
  std::string sender_identity_;
};

/// Text that compresses well, cycling through ASCII letters.
std::string compressibleText(std::size_t size) {
  std::string out;
  out.reserve(size);
  for (std::size_t i = 0; i < size; ++i) {
    out.push_back(static_cast<char>('a' + (i % 26)));
  }
  return out;
}

/// Multi-byte UTF-8 text (2-, 3- and 4-byte code points) of at least @p size bytes.
std::string unicodeText(std::size_t size) {
  const std::string unit = "h\xC3\xA9llo \xE4\xB8\x96\xE7\x95\x8C \xF0\x9F\x91\x8B ";
  std::string out;
  while (out.size() < size) {
    out += unit;
  }
  return out;
}

/// Records the reader handle of every opened data stream, to probe whether the
/// SDK released them.
class OpenedStreamHandleRecorder {
public:
  OpenedStreamHandleRecorder() {
    listener_id_ = FfiClient::instance().addListener([this](const proto::FfiEvent& event) {
      if (event.message_case() != proto::FfiEvent::kRoomEvent) {
        return;
      }
      const auto& re = event.room_event();
      std::uint64_t handle = 0;
      if (re.has_text_stream_opened()) {
        handle = re.text_stream_opened().reader().handle().id();
      } else if (re.has_byte_stream_opened()) {
        handle = re.byte_stream_opened().reader().handle().id();
      } else {
        return;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      handles_.push_back(handle);
      cv_.notify_all();
    });
  }
  ~OpenedStreamHandleRecorder() { FfiClient::instance().removeListener(listener_id_); }
  OpenedStreamHandleRecorder(const OpenedStreamHandleRecorder&) = delete;
  OpenedStreamHandleRecorder& operator=(const OpenedStreamHandleRecorder&) = delete;

  bool waitForCount(std::size_t count, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this, count] { return handles_.size() >= count; });
  }

  std::vector<std::uint64_t> handles() {
    std::lock_guard<std::mutex> lock(mutex_);
    return handles_;
  }

private:
  FfiClient::ListenerId listener_id_ = 0;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::uint64_t> handles_;
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

// ---------------------------------------------------------------------------
// One-shot sends (sendText / sendBytes / sendFile). These produce data streams
// v2 traffic (single-packet and compressed), like every other LiveKit SDK.
// ---------------------------------------------------------------------------

TEST_F(DataStreamE2ETest, SendTextSmallRoundTrip) {
  const auto topic = makeTopic("send_text_small");
  auto rooms = testRooms(2);
  auto sender = lockLocalParticipant(*rooms[0]);

  TextStreamCollector collector;
  collector.registerOn(*rooms[1], topic);

  StreamTextOptions options;
  options.topic = topic;
  options.attributes = {{"kind", "greeting"}};
  const auto sent = sender->sendText("hi there", options);
  EXPECT_FALSE(sent.stream_id.empty());
  EXPECT_EQ(sent.topic, topic);

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for text stream";
  EXPECT_FALSE(collector.error().has_value()) << *collector.error();
  EXPECT_EQ(collector.text(), "hi there");
  EXPECT_EQ(collector.info().stream_id, sent.stream_id);
  EXPECT_EQ(collector.info().mime_type, "text/plain");
  EXPECT_EQ(collector.info().attributes.at("kind"), "greeting");
  EXPECT_EQ(collector.senderIdentity(), sender->identity());
}

TEST_F(DataStreamE2ETest, SendTextLargeCompressedRoundTrip) {
  const auto topic = makeTopic("send_text_large");
  const auto payload = compressibleText(50 * 1024);
  auto rooms = testRooms(2);

  TextStreamCollector collector;
  collector.registerOn(*rooms[1], topic);

  StreamTextOptions options;
  options.topic = topic;
  (void)lockLocalParticipant(*rooms[0])->sendText(payload, options);

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for text stream";
  EXPECT_FALSE(collector.error().has_value()) << *collector.error();
  EXPECT_EQ(collector.text(), payload);
}

TEST_F(DataStreamE2ETest, SendTextUncompressedRoundTrip) {
  const auto topic = makeTopic("send_text_uncompressed");
  const auto payload = compressibleText(50 * 1024);
  auto rooms = testRooms(2);

  TextStreamCollector collector;
  collector.registerOn(*rooms[1], topic);

  StreamTextOptions options;
  options.topic = topic;
  options.compress = false;
  (void)lockLocalParticipant(*rooms[0])->sendText(payload, options);

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for text stream";
  EXPECT_FALSE(collector.error().has_value()) << *collector.error();
  EXPECT_EQ(collector.text(), payload);
}

TEST_F(DataStreamE2ETest, SendTextUnicodeRoundTrip) {
  const auto topic = makeTopic("send_text_unicode");
  // Larger than one 15 KB chunk, uncompressed, so multi-byte code points
  // straddle chunk boundaries.
  const auto payload = unicodeText(40 * 1024);
  auto rooms = testRooms(2);

  TextStreamCollector collector;
  collector.registerOn(*rooms[1], topic);

  StreamTextOptions options;
  options.topic = topic;
  options.compress = false;
  (void)lockLocalParticipant(*rooms[0])->sendText(payload, options);

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for text stream";
  EXPECT_FALSE(collector.error().has_value()) << *collector.error();
  EXPECT_EQ(collector.text(), payload);
}

TEST_F(DataStreamE2ETest, SendBytesSmallRoundTrip) {
  const auto topic = makeTopic("send_bytes");
  const std::vector<std::uint8_t> payload{0x00, 0x01, 0xFE, 0xFF, 'o', 'k'};
  auto rooms = testRooms(2);

  ByteStreamCollector collector;
  collector.registerOn(*rooms[1], topic);

  StreamByteOptions options;
  options.topic = topic;
  options.name = "blob.bin";
  options.mime_type = "application/x-test";
  const auto sent = lockLocalParticipant(*rooms[0])->sendBytes(payload, options);

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for byte stream";
  EXPECT_FALSE(collector.error().has_value()) << *collector.error();
  EXPECT_EQ(collector.content(), payload);
  EXPECT_EQ(collector.info().stream_id, sent.stream_id);
  EXPECT_EQ(collector.info().name, "blob.bin");
  EXPECT_EQ(collector.info().mime_type, "application/x-test");
}

TEST_F(DataStreamE2ETest, SendFileRoundTrip) {
  const auto topic = makeTopic("send_file");
  const std::string file_name = "lk_send_file_" + std::to_string(getTimestampUs()) + ".txt";
  const std::string path = ::testing::TempDir() + file_name;
  const auto payload = compressibleText(20 * 1024);
  {
    std::ofstream out(path, std::ios::binary);
    out << payload;
  }
  auto rooms = testRooms(2);

  ByteStreamCollector collector;
  collector.registerOn(*rooms[1], topic);

  StreamByteOptions options;
  options.topic = topic;
  const auto sent = lockLocalParticipant(*rooms[0])->sendFile(path, options);
  EXPECT_EQ(sent.name, file_name) << "name defaults to the file's basename";
  EXPECT_FALSE(sent.mime_type.empty());

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for file stream";
  std::remove(path.c_str());
  EXPECT_FALSE(collector.error().has_value()) << *collector.error();
  EXPECT_EQ(std::string(collector.content().begin(), collector.content().end()), payload);
  EXPECT_EQ(collector.info().name, file_name);
  EXPECT_EQ(collector.info().mime_type, sent.mime_type);
}

TEST_F(DataStreamE2ETest, SendFileMissingFileThrows) {
  auto rooms = testRooms(1);
  StreamByteOptions options;
  options.topic = makeTopic("send_file_missing");
  EXPECT_THROW((void)lockLocalParticipant(*rooms[0])->sendFile(::testing::TempDir() + "no-such-file.bin", options),
               DataStreamError);
}

TEST_F(DataStreamE2ETest, SendAfterDisconnectThrows) {
  auto rooms = testRooms(1);
  auto sender = lockLocalParticipant(*rooms[0]);
  ASSERT_TRUE(rooms[0]->disconnect());

  StreamTextOptions text_options;
  text_options.topic = makeTopic("after_disconnect");
  StreamByteOptions byte_options;
  byte_options.topic = text_options.topic;
  EXPECT_THROW((void)sender->sendText("late", text_options), DataStreamError);
  EXPECT_THROW((void)sender->sendBytes({1}, byte_options), DataStreamError);
}

// ---------------------------------------------------------------------------
// Receive-side behaviour
// ---------------------------------------------------------------------------

TEST_F(DataStreamE2ETest, ReceiveSizeLimitEndsStreamWithError) {
  const auto topic = makeTopic("size_limit");
  std::vector<TestRoomConnectionOptions> configs(2);
  configs[1].room_options.data_stream.max_payload_byte_length = 1000;
  auto rooms = testRooms(configs);

  TextStreamCollector collector;
  collector.registerOn(*rooms[1], topic);

  StreamTextOptions options;
  options.topic = topic;
  (void)lockLocalParticipant(*rooms[0])->sendText(compressibleText(50 * 1024), options);

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for text stream";
  ASSERT_TRUE(collector.error().has_value()) << "an oversized stream must surface an error";
  EXPECT_NE(collector.error()->find("payload exceeds maximum size"), std::string::npos) << *collector.error();
}

TEST_F(DataStreamE2ETest, TrailerAttributesAreMergedIntoInfo) {
  const auto topic = makeTopic("trailer_attrs");
  auto rooms = testRooms(2);

  TextStreamCollector collector;
  collector.registerOn(*rooms[1], topic);

  TextStreamWriter writer(*lockLocalParticipant(*rooms[0]), topic, {{"header_key", "h"}});
  writer.write("body");
  writer.close("", {{"trailer_key", "t"}});

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for text stream";
  EXPECT_FALSE(collector.error().has_value()) << *collector.error();
  EXPECT_EQ(collector.text(), "body");
  EXPECT_EQ(collector.info().attributes.at("header_key"), "h");
  EXPECT_EQ(collector.info().attributes.at("trailer_key"), "t");
}

TEST_F(DataStreamE2ETest, AbnormalCloseSurfacesError) {
  const auto topic = makeTopic("abnormal_close");
  auto rooms = testRooms(2);

  ByteStreamCollector collector;
  collector.registerOn(*rooms[1], topic);

  ByteStreamWriter writer(*lockLocalParticipant(*rooms[0]), "partial.bin", topic);
  writer.write({1, 2, 3});
  writer.close("boom");

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "Timed out waiting for byte stream";
  ASSERT_TRUE(collector.error().has_value()) << "a close with a reason must surface an error";
  EXPECT_NE(collector.error()->find("boom"), std::string::npos) << *collector.error();
  EXPECT_EQ(collector.content(), (std::vector<std::uint8_t>{1, 2, 3}));
}

TEST_F(DataStreamE2ETest, ReceiverDisconnectMidStreamWakesReader) {
  const auto topic = makeTopic("receiver_disconnect");
  auto rooms = testRooms(2);

  TextStreamCollector collector;
  collector.registerOn(*rooms[1], topic);

  TextStreamWriter writer(*lockLocalParticipant(*rooms[0]), topic);
  writer.write("first part");
  // Give the receiver time to open the reader and block on the next chunk.
  std::this_thread::sleep_for(1s);
  ASSERT_TRUE(rooms[1]->disconnect());

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout)) << "A blocked reader must wake when its room disconnects";
  EXPECT_TRUE(collector.error().has_value()) << "an interrupted stream must surface an error";
  writer.close();
}

TEST_F(DataStreamE2ETest, ReaderCloseWakesPendingRead) {
  const auto topic = makeTopic("reader_close");
  auto rooms = testRooms(2);

  std::mutex mutex;
  std::condition_variable cv;
  std::shared_ptr<TextStreamReader> reader;
  rooms[1]->registerTextStreamHandler(topic, [&](std::shared_ptr<TextStreamReader> r, const std::string&) {
    std::lock_guard<std::mutex> lock(mutex);
    reader = std::move(r);
    cv.notify_all();
  });

  TextStreamWriter writer(*lockLocalParticipant(*rooms[0]), topic);
  writer.write("first");
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(cv.wait_for(lock, kStreamWaitTimeout, [&] { return reader != nullptr; }));
  }

  std::atomic<int> chunks{0};
  std::atomic<bool> finished{false};
  std::thread read_thread([&] {
    std::string chunk;
    while (reader->readNext(chunk)) {
      ++chunks;
    }
    finished = true;
  });

  std::this_thread::sleep_for(500ms);
  EXPECT_FALSE(finished) << "reader should still be waiting for more data";
  reader->close();
  read_thread.join();
  EXPECT_TRUE(finished);
  EXPECT_EQ(chunks.load(), 1);
  writer.close();
}

TEST_F(DataStreamE2ETest, StreamHandlesAreReleased) {
  const auto handled_topic = makeTopic("handled");
  const auto unhandled_topic = makeTopic("unhandled");
  constexpr std::size_t kUnhandledStreams = 20;

  auto rooms = testRooms(2);
  auto sender = lockLocalParticipant(*rooms[0]);
  // Only count streams that reach the receiver room.
  OpenedStreamHandleRecorder recorder;

  TextStreamCollector collector;
  collector.registerOn(*rooms[1], handled_topic);

  StreamTextOptions unhandled;
  unhandled.topic = unhandled_topic;
  for (std::size_t i = 0; i < kUnhandledStreams; ++i) {
    (void)sender->sendText("nobody listens " + std::to_string(i), unhandled);
  }
  StreamTextOptions handled;
  handled.topic = handled_topic;
  (void)sender->sendText("somebody listens", handled);

  ASSERT_TRUE(collector.wait(kStreamWaitTimeout));
  ASSERT_TRUE(recorder.waitForCount(kUnhandledStreams + 1, kStreamWaitTimeout));
  // The Room handles each opened event in the same callback that the recorder
  // sees; allow a moment for the last one to finish.
  std::this_thread::sleep_for(200ms);

  // Dropping a handle that the SDK already released (unhandled topic) or
  // consumed (read) reports that it no longer exists.
  for (const auto handle : recorder.handles()) {
    EXPECT_FALSE(dropFfiHandle(handle)) << "reader handle " << handle << " was leaked";
  }
}

} // namespace livekit::test
