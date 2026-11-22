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

// Receive-side data stream conformance for behavior controlled through the
// C++ receive API: the per-stream payload cap (RoomOptions::data_stream) and
// closing a reader early (TextStreamReader::close / ByteStreamReader::close).
//
// Ported from the incoming-manager tests of the Rust `livekit-data-stream`
// crate (client-sdk-rust/livekit-data-stream/src/incoming/manager.rs); the
// remote side is an FfiPeer, as in the other receive-side conformance tests.
// See data_stream_conformance.md for the full mapping.
//
// Run with:
//   ./build-debug/bin/livekit_integration_tests --gtest_filter=DataStreamReceiveApi*

#include <livekit/data_stream.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../common/data_stream_test_utils.h"

namespace livekit::test::ds {

namespace {

constexpr std::uint64_t kPayloadCap = 1'000;
constexpr char kPayloadTooLarge[] = "stream payload exceeds maximum size";

} // namespace

/// The receiver (room B) caps incoming streams at kPayloadCap bytes.
class DataStreamReceiveApiCapTest : public DataStreamConformanceTest {
protected:
  void SetUp() override {
    LiveKitTestBase::SetUp();
    skipIfNotConfigured();
    if (IsSkipped()) {
      return;
    }
    std::vector<TestRoomConnectionOptions> configs(2);
    configs[1].room_options.data_stream.max_payload_byte_length = kPayloadCap;
    rooms_ = testRooms(configs);
  }

  /// Sends `payload` with an FfiPeer stream writer in `chunk`-byte writes,
  /// optionally declaring `total_length`, and returns the received stream.
  ReceivedStream<ByteStreamInfo> sendWithWriter(const std::string& payload, std::size_t chunk,
                                                std::optional<std::uint64_t> total_length) {
    const auto topic = uniqueTopic("cap_writer");
    ByteStreamCollector collector;
    collector.registerOn(receiverRoom(), topic);
    FfiPeer peer(senderRoom());
    PeerByteOptions opts;
    opts.topic = topic;
    opts.name = "capped.bin";
    opts.total_length = total_length;
    const auto writer = peer.openBytes(opts);
    EXPECT_TRUE(writer.ok) << writer.error;
    for (std::size_t off = 0; off < payload.size(); off += chunk) {
      (void)peer.writeBytes(writer.writer_handle, payload.substr(off, chunk));
    }
    (void)peer.closeBytes(writer.writer_handle);
    EXPECT_TRUE(collector.waitFor(1)) << "stream never ended";
    return collector.streams().empty() ? ReceivedStream<ByteStreamInfo>{} : collector.streams().front();
  }
};

// Rust: v1_max_payload_size_breached_with_unknown_total
TEST_F(DataStreamReceiveApiCapTest, BreachedWithUnknownTotal) {
  const auto s = sendWithWriter(std::string(1'200, '\0'), 400, std::nullopt);
  EXPECT_TRUE(s.errored) << "a stream that grows past the cap must fail";
  EXPECT_NE(s.error.find(kPayloadTooLarge), std::string::npos) << s.error;
}

// Rust: v1_max_payload_size_fast_fails_on_declared_total
TEST_F(DataStreamReceiveApiCapTest, FastFailsOnDeclaredTotal) {
  const auto s = sendWithWriter(std::string(10, '\0'), 10, 2'000);
  EXPECT_TRUE(s.errored) << "a stream that declares more than the cap must fail";
  EXPECT_NE(s.error.find(kPayloadTooLarge), std::string::npos) << s.error;
  EXPECT_TRUE(s.content.empty()) << "no data is delivered for a stream rejected by its header";
}

// Rust: v1_payload_exactly_at_max_payload_size_succeeds
TEST_F(DataStreamReceiveApiCapTest, PayloadExactlyAtCapSucceeds) {
  const std::string payload(kPayloadCap, '\x07');
  const auto s = sendWithWriter(payload, 400, kPayloadCap);
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, payload);
}

// Rust: v2_inline_uncompressed_max_payload_size_breached
TEST_F(DataStreamReceiveApiCapTest, InlineUncompressedBreached) {
  const auto topic = uniqueTopic("cap_inline");
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());
  PeerByteOptions opts;
  opts.topic = topic;
  ASSERT_TRUE(peer.sendBytes(randomBytes(2'000), opts).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_TRUE(s.errored);
  EXPECT_NE(s.error.find(kPayloadTooLarge), std::string::npos) << s.error;
}

// Rust: v2_inline_compressed_max_payload_size_breached. The compressed inline
// packet is tiny, but it decompresses far past the cap.
TEST_F(DataStreamReceiveApiCapTest, InlineCompressedBreached) {
  const auto topic = uniqueTopic("cap_inline_z");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());
  PeerTextOptions opts;
  opts.topic = topic;
  ASSERT_TRUE(peer.sendText(compressibleText(220'000), opts).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_TRUE(s.errored);
  EXPECT_NE(s.error.find(kPayloadTooLarge), std::string::npos) << s.error;
}

// Rust: v2_max_payload_size_breached
TEST_F(DataStreamReceiveApiCapTest, CompressedMultipacketBreached) {
  const auto topic = uniqueTopic("cap_multi_z");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());
  PeerTextOptions opts;
  opts.topic = topic;
  ASSERT_TRUE(peer.sendText(randomText(60'000), opts).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_TRUE(s.errored);
  EXPECT_NE(s.error.find(kPayloadTooLarge), std::string::npos) << s.error;
}

// ---------------------------------------------------------------------------
// Closing a reader early
// ---------------------------------------------------------------------------

/// Captures readers as they open without reading them, so a test controls
/// exactly when (and whether) each reader is read or closed.
class ReaderCapture {
public:
  void registerOn(Room& room, const std::string& topic) {
    room.registerTextStreamHandler(topic, [this](std::shared_ptr<TextStreamReader> reader, const std::string&) {
      {
        const std::scoped_lock<std::mutex> lock(mutex_);
        readers_.push_back(std::move(reader));
      }
      cv_.notify_all();
    });
  }

  std::shared_ptr<TextStreamReader> waitForReader(std::size_t index) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, kStreamTimeout, [&] { return readers_.size() > index; })) {
      return nullptr;
    }
    return readers_[index];
  }

  std::size_t count() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return readers_.size();
  }

  ~ReaderCapture() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    for (auto& r : readers_) {
      r->close();
    }
  }

  ReaderCapture() = default;
  ReaderCapture(const ReaderCapture&) = delete;
  ReaderCapture& operator=(const ReaderCapture&) = delete;
  ReaderCapture(ReaderCapture&&) = delete;
  ReaderCapture& operator=(ReaderCapture&&) = delete;

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::shared_ptr<TextStreamReader>> readers_;
};

class DataStreamReceiveApiCloseTest : public DataStreamConformanceTest {};

// Rust: closer_ends_an_open_stream_once
TEST_F(DataStreamReceiveApiCloseTest, CloseEndsAnOpenStreamOnce) {
  const auto topic = uniqueTopic("close_open");
  ReaderCapture capture;
  capture.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());
  PeerTextOptions opts;
  opts.topic = topic;
  const auto writer = peer.openText(opts);
  ASSERT_TRUE(writer.ok) << writer.error;
  ASSERT_TRUE(peer.writeText(writer.writer_handle, "hello").ok);

  auto reader = capture.waitForReader(0);
  ASSERT_NE(reader, nullptr);
  std::string chunk;
  ASSERT_TRUE(reader->readNext(chunk));
  EXPECT_EQ(chunk, "hello");

  reader->close();
  reader->close(); // idempotent
  EXPECT_FALSE(reader->readNext(chunk)) << "a closed reader reports the end of the stream";

  // The sender keeps going; nothing reaches the closed reader and no new
  // stream is opened.
  ASSERT_TRUE(peer.writeText(writer.writer_handle, "world").ok);
  ASSERT_TRUE(peer.closeText(writer.writer_handle).ok);
  std::this_thread::sleep_for(kQuietPeriod);
  EXPECT_FALSE(reader->readNext(chunk));
  EXPECT_EQ(capture.count(), 1U);
}

// Rust: closer_discards_packets_still_queued
TEST_F(DataStreamReceiveApiCloseTest, CloseDiscardsQueuedChunks) {
  const auto topic = uniqueTopic("close_queued");
  RawPacketSniffer sniffer(receiverRoom());
  ReaderCapture capture;
  capture.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());
  PeerTextOptions opts;
  opts.topic = topic;
  const auto writer = peer.openText(opts);
  ASSERT_TRUE(writer.ok) << writer.error;
  ASSERT_TRUE(peer.writeText(writer.writer_handle, "hello").ok);
  ASSERT_TRUE(peer.writeText(writer.writer_handle, "world").ok);

  auto reader = capture.waitForReader(0);
  ASSERT_NE(reader, nullptr);
  ASSERT_TRUE(sniffer.waitForChunks(writer.stream_id, 2));
  // Give the chunks time to reach the reader's queue, unread.
  std::this_thread::sleep_for(300ms);

  reader->close();
  std::string chunk;
  EXPECT_FALSE(reader->readNext(chunk)) << "closing discards chunks that were never read";
  (void)peer.closeText(writer.writer_handle);
}

// Rust: closer_spares_a_later_stream_with_the_same_id
TEST_F(DataStreamReceiveApiCloseTest, CloseSparesALaterStreamWithTheSameId) {
  const auto topic = uniqueTopic("close_same_id");
  const auto sid = uniqueStreamId("reused");
  ReaderCapture capture;
  capture.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());
  PeerTextOptions opts;
  opts.topic = topic;
  opts.stream_id = sid;

  // First stream with the id runs to completion.
  auto first_writer = peer.openText(opts);
  ASSERT_TRUE(first_writer.ok) << first_writer.error;
  ASSERT_TRUE(peer.writeText(first_writer.writer_handle, "hello").ok);
  ASSERT_TRUE(peer.writeText(first_writer.writer_handle, "world").ok);
  ASSERT_TRUE(peer.closeText(first_writer.writer_handle).ok);
  auto first = capture.waitForReader(0);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->readAll(), "helloworld");

  // A second stream reuses the id; closing the first reader must not touch it.
  auto second_writer = peer.openText(opts);
  ASSERT_TRUE(second_writer.ok) << second_writer.error;
  ASSERT_TRUE(peer.writeText(second_writer.writer_handle, "hello").ok);
  auto second = capture.waitForReader(1);
  ASSERT_NE(second, nullptr);
  first->close();
  ASSERT_TRUE(peer.writeText(second_writer.writer_handle, "world").ok);
  ASSERT_TRUE(peer.closeText(second_writer.writer_handle).ok);

  std::string content;
  std::thread reader_thread([&] { content = second->readAll(); });
  reader_thread.join();
  EXPECT_EQ(content, "helloworld");
  EXPECT_EQ(second->info().stream_id, sid);
}

// Rust: closer_after_shutdown_does_nothing
TEST_F(DataStreamReceiveApiCloseTest, CloseAfterDisconnectIsHarmless) {
  const auto topic = uniqueTopic("close_after_shutdown");
  ReaderCapture capture;
  capture.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());
  PeerTextOptions opts;
  opts.topic = topic;
  const auto writer = peer.openText(opts);
  ASSERT_TRUE(writer.ok) << writer.error;
  ASSERT_TRUE(peer.writeText(writer.writer_handle, "hello").ok);
  auto reader = capture.waitForReader(0);
  ASSERT_NE(reader, nullptr);

  (void)receiverRoom().disconnect();
  EXPECT_THROW((void)reader->readAll(), DataStreamError) << "a stream cut off by a disconnect ends with an error";
  EXPECT_NO_THROW(reader->close());
  EXPECT_NO_THROW(reader->close());
  (void)peer.closeText(writer.writer_handle);
}

} // namespace livekit::test::ds
