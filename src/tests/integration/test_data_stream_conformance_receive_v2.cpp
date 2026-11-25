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

// Receive-side data stream conformance, v2 streams: a remote participant
// sends inline (single-packet) and compressed streams exactly as another SDK
// would, and the C++ SDK must receive them correctly.
//
// Ported from the incoming-manager tests of the Rust `livekit-data-stream`
// crate (client-sdk-rust/livekit-data-stream/src/incoming/manager.rs). The
// remote side is an FfiPeer: it drives the sender room's participant through
// the raw FFI, so its one-shot sends go through the Rust outgoing manager and
// use data streams v2 (inline and compressed packets) just like the Python or
// Rust SDKs, and its raw requests send hand-built v1 packets. The C++ side
// receives through the public API (stream handlers + readers) only.
//
// See data_stream_conformance.md for the full Rust-test -> C++-test mapping,
// including the Rust tests that cannot be expressed from the C++ layer.
//
// Run with:
//   ./build-debug/bin/livekit_integration_tests --gtest_filter=DataStreamReceiveV2*

#include <livekit/data_stream.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../common/data_stream_test_utils.h"

namespace livekit::test::ds {

namespace {

/// Expects the stream on `topic` to have arrived as a single inline header
/// packet: a header and no chunk or trailer packets.
void expectInline(const RawPacketSniffer& sniffer, const std::string& topic) {
  const auto header = sniffer.waitForHeader(topic);
  ASSERT_TRUE(header.has_value()) << "no raw header received";
  // Any chunk or trailer would have arrived with (before) the reader's end.
  std::this_thread::sleep_for(300ms);
  EXPECT_TRUE(sniffer.chunks(header->stream_id).empty()) << "inline stream must not send chunk packets";
  EXPECT_TRUE(sniffer.trailers(header->stream_id).empty()) << "inline stream must not send a trailer packet";
}

/// Expects the stream on `topic` to have arrived as compressed chunk packets:
/// raw chunk bytes that are not the plaintext and are smaller than it.
void expectCompressedChunks(const RawPacketSniffer& sniffer, const std::string& topic, const std::string& plaintext) {
  const auto header = sniffer.waitForHeader(topic);
  ASSERT_TRUE(header.has_value()) << "no raw header received";
  ASSERT_TRUE(sniffer.waitForTrailer(header->stream_id).has_value()) << "no raw trailer received";
  const auto chunks = sniffer.chunks(header->stream_id);
  EXPECT_GE(chunks.size(), 2U);
  const auto raw = sniffer.rawContent(header->stream_id);
  EXPECT_NE(raw, plaintext) << "chunk content should be compressed";
  EXPECT_LT(raw.size(), plaintext.size());
  ASSERT_TRUE(header->total_length.has_value());
  EXPECT_EQ(*header->total_length, plaintext.size()) << "total_length is the uncompressed length";
}

/// Text of pseudo-random CJK ideographs (3 bytes each in UTF-8): high
/// entropy, so it compresses only modestly and is sent as compressed chunks,
/// and chunk boundaries of the compressed stream fall mid-character.
std::string randomCjkText(std::size_t approx_bytes) {
  const std::string seed_bytes = randomBytes(approx_bytes / 3 * 2, 0xC0FFEE);
  std::string out;
  out.reserve(approx_bytes + 3);
  for (std::size_t i = 0; i + 1 < seed_bytes.size(); i += 2) {
    const auto v = static_cast<std::uint32_t>(static_cast<unsigned char>(seed_bytes[i])) << 8 |
                   static_cast<unsigned char>(seed_bytes[i + 1]);
    const std::uint32_t cp = 0x4E00 + (v % 0x5000); // CJK Unified Ideographs
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
  return out;
}

} // namespace

class DataStreamReceiveV2Test : public DataStreamConformanceTest {};

// ---------------------------------------------------------------------------
// v2 inline streams (single header packet carrying the payload)
// ---------------------------------------------------------------------------

// Rust: v2_inline_uncompressed_text, inline_stream_emits_stream_closed,
//       inline_streams_are_never_counted
TEST_F(DataStreamReceiveV2Test, V2InlineUncompressedText) {
  const auto topic = uniqueTopic("v2_inline_text");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerTextOptions opts;
  opts.topic = topic;
  opts.attributes = {{"foo", "bar"}};
  // "inline hello" does not shrink under deflate, so it is inlined raw.
  ASSERT_TRUE(peer.sendText("inline hello", opts).ok);

  ASSERT_TRUE(collector.waitFor(1)) << "inline v2 stream was never delivered";
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, "inline hello");
  EXPECT_EQ(s.sender, peer.identity());
  EXPECT_EQ(s.info.attributes.at("foo"), "bar");
  ASSERT_TRUE(s.info.size.has_value());
  EXPECT_EQ(*s.info.size, 12U);
  expectInline(sniffer, topic);
}

// Rust: v2_inline_uncompressed_byte
TEST_F(DataStreamReceiveV2Test, V2InlineUncompressedByte) {
  const auto topic = uniqueTopic("v2_inline_bytes");
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerByteOptions opts;
  opts.topic = topic;
  opts.name = "file";
  ASSERT_TRUE(peer.sendBytes(std::string("\x01\x02\x03", 3), opts).ok);

  ASSERT_TRUE(collector.waitFor(1)) << "inline v2 stream was never delivered";
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, std::string("\x01\x02\x03", 3));
  EXPECT_EQ(s.info.name, "file");
  expectInline(sniffer, topic);
}

// Rust: v2_inline_compressed_text
TEST_F(DataStreamReceiveV2Test, V2InlineCompressedText) {
  const auto topic = uniqueTopic("v2_inline_ztext");
  const std::string text = "hello hello compressible world";
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerTextOptions opts;
  opts.topic = topic;
  opts.attributes = {{"foo", "bar"}};
  ASSERT_TRUE(peer.sendText(text, opts).ok);

  ASSERT_TRUE(collector.waitFor(1)) << "compressed inline v2 stream was never delivered";
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, text);
  EXPECT_EQ(s.info.attributes.at("foo"), "bar");
  expectInline(sniffer, topic);
}

// Rust: v2_inline_compressed_byte
TEST_F(DataStreamReceiveV2Test, V2InlineCompressedByte) {
  const auto topic = uniqueTopic("v2_inline_zbytes");
  std::string payload(2000, '\0');
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<char>(i % 7);
  }
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerByteOptions opts;
  opts.topic = topic;
  ASSERT_TRUE(peer.sendBytes(payload, opts).ok);

  ASSERT_TRUE(collector.waitFor(1)) << "compressed inline v2 stream was never delivered";
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, payload);
  expectInline(sniffer, topic);
}

// Rust: v2_inline_zero_length_text (and v2_empty_text_sends_single_inline_packet
// from the sending side)
TEST_F(DataStreamReceiveV2Test, V2InlineZeroLengthText) {
  const auto topic = uniqueTopic("v2_inline_empty");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerTextOptions opts;
  opts.topic = topic;
  ASSERT_TRUE(peer.sendText("", opts).ok);

  ASSERT_TRUE(collector.waitFor(1)) << "empty inline v2 stream was never delivered";
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, "");
  ASSERT_TRUE(s.info.size.has_value());
  EXPECT_EQ(*s.info.size, 0U);
  expectInline(sniffer, topic);
}

// Rust: v2_large_highly_compressible_text_still_inlines (receiving side)
TEST_F(DataStreamReceiveV2Test, V2LargeHighlyCompressibleTextInlines) {
  const auto topic = uniqueTopic("v2_inline_large");
  std::string text;
  for (int i = 0; i < 20'000; ++i) {
    text += "hello world";
  }
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerTextOptions opts;
  opts.topic = topic;
  ASSERT_TRUE(peer.sendText(text, opts).ok);

  ASSERT_TRUE(collector.waitFor(1)) << "large compressed inline v2 stream was never delivered";
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content.size(), text.size());
  EXPECT_EQ(s.content, text);
  expectInline(sniffer, topic);
}

// ---------------------------------------------------------------------------
// v2 multi-packet compressed streams
// ---------------------------------------------------------------------------

// Rust: v2_multipacket_compressed_text, progress_reports_completion_compressed_text
TEST_F(DataStreamReceiveV2Test, V2MultipacketCompressedText) {
  const auto topic = uniqueTopic("v2_multi_ztext");
  const auto text = randomText(60'000);
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerTextOptions opts;
  opts.topic = topic;
  ASSERT_TRUE(peer.sendText(text, opts).ok);

  ASSERT_TRUE(collector.waitFor(1)) << "compressed multi-packet v2 stream was never delivered";
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content.size(), text.size());
  EXPECT_EQ(s.content, text) << "compressed chunks must be decompressed before delivery";
  expectCompressedChunks(sniffer, topic, text);
}

// Rust: v2_multipacket_compressed_byte_stream
TEST_F(DataStreamReceiveV2Test, V2MultipacketCompressedByteStream) {
  const auto topic = uniqueTopic("v2_multi_zbytes");
  const auto payload = randomText(60'000);
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerByteOptions opts;
  opts.topic = topic;
  ASSERT_TRUE(peer.sendBytes(payload, opts).ok);

  ASSERT_TRUE(collector.waitFor(1)) << "compressed multi-packet v2 stream was never delivered";
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content.size(), payload.size());
  EXPECT_EQ(s.content, payload) << "compressed chunks must be decompressed before delivery";
  expectCompressedChunks(sniffer, topic, payload);
}

// Rust: v2_compressed_text_reframes_multibyte_utf8. The Rust test hand-splits
// a compressed stream; here the payload is high-entropy multibyte text, so the
// sender emits several compressed chunks whose boundaries fall mid-character
// once decompressed. Every chunk the reader delivers must be valid UTF-8.
TEST_F(DataStreamReceiveV2Test, V2CompressedTextReframesMultibyteUtf8) {
  const auto topic = uniqueTopic("v2_utf8");
  const auto text = randomCjkText(60'000);
  ASSERT_TRUE(isValidUtf8(text));
  RawPacketSniffer sniffer(receiverRoom());
  // Collect chunks individually to check each is valid UTF-8.
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> chunks;
  bool done = false;
  std::string error;
  std::thread reader_thread;
  receiverRoom().registerTextStreamHandler(topic, [&](std::shared_ptr<TextStreamReader> reader, const std::string&) {
    reader_thread = std::thread([&, reader] {
      std::vector<std::string> local;
      std::string local_error;
      try {
        std::string chunk;
        while (reader->readNext(chunk)) {
          local.push_back(chunk);
        }
      } catch (const std::exception& e) {
        local_error = e.what();
      }
      const std::scoped_lock<std::mutex> lock(mutex);
      chunks = std::move(local);
      error = std::move(local_error);
      done = true;
      cv.notify_all();
    });
  });
  FfiPeer peer(senderRoom());

  PeerTextOptions opts;
  opts.topic = topic;
  ASSERT_TRUE(peer.sendText(text, opts).ok);

  bool finished = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    finished = cv.wait_for(lock, kStreamTimeout, [&] { return done; });
  }
  receiverRoom().unregisterTextStreamHandler(topic);
  if (!finished) {
    if (reader_thread.joinable()) {
      reader_thread.detach();
    }
    FAIL() << "compressed multibyte v2 stream was never delivered";
  }
  reader_thread.join();
  EXPECT_TRUE(error.empty()) << error;
  std::string joined;
  for (const auto& c : chunks) {
    EXPECT_TRUE(isValidUtf8(c)) << "a delivered chunk split a multibyte character";
    joined += c;
  }
  EXPECT_EQ(joined, text);
  expectCompressedChunks(sniffer, topic, text);
}

// Rust: text_stream_with_attachments_round_trips (and
// v2_send_text_with_attachments_never_inlines from the sending side)
TEST_F(DataStreamReceiveV2Test, TextStreamWithAttachmentsRoundTrips) {
  const auto text_topic = uniqueTopic("attach_text");
  const auto byte_topic = uniqueTopic("attach_bytes");
  const auto attachment_id = uniqueStreamId("att");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector text_collector;
  text_collector.registerOn(receiverRoom(), text_topic);
  ByteStreamCollector byte_collector;
  byte_collector.registerOn(receiverRoom(), byte_topic);
  FfiPeer peer(senderRoom());

  PeerTextOptions text_opts;
  text_opts.topic = text_topic;
  text_opts.attached_stream_ids = {attachment_id};
  ASSERT_TRUE(peer.sendText("hello world", text_opts).ok);
  PeerByteOptions byte_opts;
  byte_opts.topic = byte_topic;
  byte_opts.stream_id = attachment_id;
  ASSERT_TRUE(peer.sendBytes(std::string("\x01\x02\x03", 3), byte_opts).ok);

  ASSERT_TRUE(text_collector.waitFor(1)) << "text stream with attachments was never delivered";
  const auto t = text_collector.only();
  EXPECT_FALSE(t.errored) << t.error;
  EXPECT_EQ(t.content, "hello world");
  EXPECT_EQ(t.info.attachments, std::vector<std::string>{attachment_id});

  ASSERT_TRUE(byte_collector.waitFor(1)) << "attachment byte stream was never delivered";
  const auto b = byte_collector.only();
  EXPECT_FALSE(b.errored) << b.error;
  EXPECT_EQ(b.info.stream_id, attachment_id);
  EXPECT_EQ(b.content, std::string("\x01\x02\x03", 3));

  // A text stream with attachments is never inlined.
  const auto header = sniffer.waitForHeader(text_topic);
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ(header->attached_stream_ids, std::vector<std::string>{attachment_id});
  EXPECT_TRUE(sniffer.waitForTrailer(header->stream_id).has_value());
  EXPECT_FALSE(sniffer.chunks(header->stream_id).empty());
}

} // namespace livekit::test::ds
