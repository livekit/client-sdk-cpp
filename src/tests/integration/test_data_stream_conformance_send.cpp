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

// Send-side data stream conformance: the C++ SDK sends through its public API
// (sendText / sendBytes / sendFile / streamText / streamBytes), and the
// receiving room's raw packets are checked against what the Rust
// `livekit-data-stream` crate's outgoing-manager tests require
// (client-sdk-rust/livekit-data-stream/src/outgoing/manager.rs), along with
// the content the receiver's reader delivers.
//
// All test participants are FFI clients and advertise data streams v2 with
// deflate-raw compression, so every recipient here is "v2 capable". The raw
// header event carries neither `inline_content` nor `compression`, so an
// inline stream is observed as a header with no chunk or trailer packets, and
// compression as chunk bytes that differ from (and are smaller than) the
// payload.
//
// See data_stream_conformance.md for the full Rust-test -> C++-test mapping.
//
// Run with:
//   ./build-debug/bin/livekit_integration_tests --gtest_filter=DataStreamSend*

#include <livekit/data_stream.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "../common/data_stream_test_utils.h"

namespace livekit::test::ds {

namespace {

/// "hello hello compressible world": 30 bytes that deflate to 28.
const std::string kShortCompressible = "hello hello compressible world";

/// Waits for the header on `topic`, then checks no chunk or trailer follows.
RawHeader expectInline(const RawPacketSniffer& sniffer, const std::string& topic) {
  const auto header = sniffer.waitForHeader(topic);
  EXPECT_TRUE(header.has_value()) << "no raw header received";
  if (!header) {
    return {};
  }
  std::this_thread::sleep_for(kQuietPeriod);
  EXPECT_TRUE(sniffer.chunks(header->stream_id).empty()) << "inline stream must not send chunk packets";
  EXPECT_TRUE(sniffer.trailers(header->stream_id).empty()) << "inline stream must not send a trailer packet";
  return *header;
}

/// Waits for a complete multi-packet stream on `topic` and returns its header.
RawHeader expectMultipacket(const RawPacketSniffer& sniffer, const std::string& topic) {
  const auto header = sniffer.waitForHeader(topic);
  EXPECT_TRUE(header.has_value()) << "no raw header received";
  if (!header) {
    return {};
  }
  const auto trailer = sniffer.waitForTrailer(header->stream_id);
  EXPECT_TRUE(trailer.has_value()) << "multi-packet stream must end with a trailer";
  if (trailer) {
    EXPECT_EQ(trailer->reason, "");
  }
  return *header;
}

std::vector<std::size_t> chunkSizes(const RawPacketSniffer& sniffer, const std::string& stream_id) {
  std::vector<std::size_t> sizes;
  for (const auto& c : sniffer.chunks(stream_id)) {
    sizes.push_back(c.content.size());
  }
  return sizes;
}

std::vector<std::uint64_t> chunkIndices(const RawPacketSniffer& sniffer, const std::string& stream_id) {
  std::vector<std::uint64_t> indices;
  for (const auto& c : sniffer.chunks(stream_id)) {
    indices.push_back(c.chunk_index);
  }
  return indices;
}

/// A file in a fresh temporary directory, removed on destruction.
class TempFile {
public:
  TempFile(const std::string& name, const std::string& content) {
    dir_ = std::filesystem::temp_directory_path() / uniqueStreamId("ds_conf_file");
    std::filesystem::create_directories(dir_);
    path_ = dir_ / name;
    std::ofstream out(path_, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
  }
  ~TempFile() {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  TempFile(TempFile&&) = delete;
  TempFile& operator=(TempFile&&) = delete;

  std::string path() const { return path_.string(); }

private:
  std::filesystem::path dir_;
  std::filesystem::path path_;
};

} // namespace

class DataStreamSendConformanceTest : public DataStreamConformanceTest {
protected:
  std::shared_ptr<LocalParticipant> sender() { return lockLocalParticipant(senderRoom()); }

  StreamTextOptions textOptions(const std::string& topic) {
    StreamTextOptions opts;
    opts.topic = topic;
    return opts;
  }

  StreamByteOptions byteOptions(const std::string& topic) {
    StreamByteOptions opts;
    opts.topic = topic;
    return opts;
  }
};

// ---------------------------------------------------------------------------
// sendText
// ---------------------------------------------------------------------------

// Rust: v2_short_incompressible_text_inlines_raw
TEST_F(DataStreamSendConformanceTest, SendTextShortIncompressibleInlines) {
  const auto topic = uniqueTopic("send_short");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  const auto info = sender()->sendText("short", textOptions(topic));

  const auto header = expectInline(sniffer, topic);
  EXPECT_EQ(header.stream_id, info.stream_id);
  EXPECT_TRUE(header.is_text);
  EXPECT_EQ(header.mime_type, "text/plain");
  ASSERT_TRUE(header.total_length.has_value());
  EXPECT_EQ(*header.total_length, 5U);
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, "short");
}

// Rust: v2_short_compressible_text_inlines_compressed,
//       v2_broadcast_all_capable_inlines_compressed
TEST_F(DataStreamSendConformanceTest, SendTextShortCompressibleInlines) {
  const auto topic = uniqueTopic("send_short_z");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  (void)sender()->sendText(kShortCompressible, textOptions(topic));

  const auto header = expectInline(sniffer, topic);
  ASSERT_TRUE(header.total_length.has_value());
  EXPECT_EQ(*header.total_length, kShortCompressible.size());
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, kShortCompressible);
}

// Rust: v2_large_highly_compressible_text_still_inlines
TEST_F(DataStreamSendConformanceTest, SendTextLargeHighlyCompressibleStillInlines) {
  const auto topic = uniqueTopic("send_large_z");
  std::string text;
  for (int i = 0; i < 20'000; ++i) {
    text += "hello world";
  }
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  (void)sender()->sendText(text, textOptions(topic));

  const auto header = expectInline(sniffer, topic);
  ASSERT_TRUE(header.total_length.has_value());
  EXPECT_EQ(*header.total_length, text.size()) << "total_length is the uncompressed length";
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, text);
}

// Rust: v2_somewhat_compressible_text_is_compressed_multipacket
TEST_F(DataStreamSendConformanceTest, SendTextSomewhatCompressibleIsCompressedMultipacket) {
  const auto topic = uniqueTopic("send_somewhat_z");
  const auto text = randomText(50'000);
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  (void)sender()->sendText(text, textOptions(topic));

  const auto header = expectMultipacket(sniffer, topic);
  ASSERT_TRUE(header.total_length.has_value());
  EXPECT_EQ(*header.total_length, text.size());
  const auto sizes = chunkSizes(sniffer, header.stream_id);
  ASSERT_GE(sizes.size(), 2U);
  EXPECT_LT(sizes.size(), 4U) << "compressed chunks are fewer than uncompressed ones would be";
  EXPECT_EQ(sizes.front(), kMtu);
  const auto raw = sniffer.rawContent(header.stream_id);
  EXPECT_LT(raw.size(), text.size());
  EXPECT_NE(raw, text.substr(0, raw.size())) << "chunks must carry compressed bytes";
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, text);
}

// Rust: v2_compress_false_short_inlines_raw
TEST_F(DataStreamSendConformanceTest, SendTextCompressFalseShortInlines) {
  const auto topic = uniqueTopic("send_nocomp_short");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto opts = textOptions(topic);
  opts.compress = false;
  (void)sender()->sendText(kShortCompressible, opts);

  (void)expectInline(sniffer, topic);
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, kShortCompressible);
}

// Rust: v2_compress_false_large_is_uncompressed_multipacket
TEST_F(DataStreamSendConformanceTest, SendTextCompressFalseLargeIsUncompressedMultipacket) {
  const auto topic = uniqueTopic("send_nocomp_large");
  const std::string text(50'000, 'B');
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto opts = textOptions(topic);
  opts.compress = false;
  (void)sender()->sendText(text, opts);

  const auto header = expectMultipacket(sniffer, topic);
  EXPECT_EQ(chunkSizes(sniffer, header.stream_id), (std::vector<std::size_t>{kMtu, kMtu, kMtu, 5'000}));
  EXPECT_EQ(chunkIndices(sniffer, header.stream_id), (std::vector<std::uint64_t>{0, 1, 2, 3}));
  EXPECT_EQ(sniffer.rawContent(header.stream_id), text) << "uncompressed chunks carry the text verbatim";
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, text);
}

// Rust: v2_send_text_with_attachments_never_inlines
TEST_F(DataStreamSendConformanceTest, SendTextWithAttachmentsNeverInlines) {
  const auto topic = uniqueTopic("send_attach");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto opts = textOptions(topic);
  opts.attached_stream_ids = {"att1"};
  (void)sender()->sendText(kShortCompressible, opts);

  const auto header = expectMultipacket(sniffer, topic);
  EXPECT_EQ(header.attached_stream_ids, std::vector<std::string>{"att1"});
  EXPECT_EQ(sniffer.chunks(header.stream_id).size(), 1U);
  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_EQ(s.content, kShortCompressible);
  EXPECT_EQ(s.info.attachments, std::vector<std::string>{"att1"});
}

// Rust: v2_empty_text_sends_single_inline_packet
TEST_F(DataStreamSendConformanceTest, SendTextEmptyIsSingleInlinePacket) {
  const auto topic = uniqueTopic("send_empty");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  (void)sender()->sendText("", textOptions(topic));

  const auto header = expectInline(sniffer, topic);
  ASSERT_TRUE(header.total_length.has_value());
  EXPECT_EQ(*header.total_length, 0U);
  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, "");
}

// Rust: oversized_attributes_on_chunked_path_errors
TEST_F(DataStreamSendConformanceTest, SendTextOversizedAttributesThrows) {
  const auto topic = uniqueTopic("send_big_attrs");
  RawPacketSniffer sniffer(receiverRoom());

  auto opts = textOptions(topic);
  opts.attributes = {{"big", std::string(20'000, 'x')}};
  try {
    (void)sender()->sendText("hello", opts);
    FAIL() << "a header over the packet size limit must be rejected";
  } catch (const DataStreamError& e) {
    EXPECT_NE(std::string(e.what()).find("stream header exceeds maximum size"), std::string::npos) << e.what();
  }
  EXPECT_FALSE(sniffer.waitForHeader(topic, kQuietPeriod).has_value()) << "nothing may be sent";
}

// ---------------------------------------------------------------------------
// sendBytes
// ---------------------------------------------------------------------------

// Rust: v2_send_bytes_short_incompressible_inlines_raw
TEST_F(DataStreamSendConformanceTest, SendBytesShortIncompressibleInlines) {
  const auto topic = uniqueTopic("bytes_short");
  const std::vector<std::uint8_t> payload{0, 1, 2, 3};
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  (void)sender()->sendBytes(payload, byteOptions(topic));

  const auto header = expectInline(sniffer, topic);
  EXPECT_FALSE(header.is_text);
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, fromBytes(payload));
}

// Rust: v2_send_bytes_short_compressible_inlines_compressed (incl. returned info)
TEST_F(DataStreamSendConformanceTest, SendBytesShortCompressibleInlinesAndReturnsInfo) {
  const auto topic = uniqueTopic("bytes_short_z");
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto opts = byteOptions(topic);
  opts.attributes = {{"foo", "bar"}};
  const auto info = sender()->sendBytes(toBytes(kShortCompressible), opts);

  EXPECT_EQ(info.name, "unknown");
  EXPECT_EQ(info.mime_type, "application/octet-stream");
  ASSERT_TRUE(info.size.has_value());
  EXPECT_EQ(*info.size, kShortCompressible.size());
  EXPECT_EQ(info.attributes.at("foo"), "bar");
  const auto header = expectInline(sniffer, topic);
  EXPECT_EQ(header.stream_id, info.stream_id);
  EXPECT_EQ(header.name, "unknown");
  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_EQ(s.content, kShortCompressible);
  EXPECT_EQ(s.info.name, "unknown");
  EXPECT_EQ(s.info.attributes.at("foo"), "bar");
}

// Rust: v2_send_bytes_large_highly_compressible_inlines
TEST_F(DataStreamSendConformanceTest, SendBytesLargeHighlyCompressibleInlines) {
  const auto topic = uniqueTopic("bytes_large_z");
  const std::string payload(50'000, '\x01');
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  (void)sender()->sendBytes(toBytes(payload), byteOptions(topic));

  const auto header = expectInline(sniffer, topic);
  ASSERT_TRUE(header.total_length.has_value());
  EXPECT_EQ(*header.total_length, 50'000U) << "total_length is the pre-compression length";
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, payload);
}

// Rust: v2_send_bytes_somewhat_compressible_is_compressed_multipacket
TEST_F(DataStreamSendConformanceTest, SendBytesSomewhatCompressibleIsCompressedMultipacket) {
  const auto topic = uniqueTopic("bytes_somewhat_z");
  const auto payload = randomText(50'000);
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  (void)sender()->sendBytes(toBytes(payload), byteOptions(topic));

  const auto header = expectMultipacket(sniffer, topic);
  EXPECT_FALSE(header.is_text);
  const auto sizes = chunkSizes(sniffer, header.stream_id);
  ASSERT_GE(sizes.size(), 2U);
  EXPECT_LT(sizes.size(), 4U);
  EXPECT_EQ(sizes.front(), kMtu);
  EXPECT_LT(sniffer.rawContent(header.stream_id).size(), payload.size());
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, payload);
}

// Rust: v2_send_bytes_compress_false_large_is_uncompressed_multipacket
TEST_F(DataStreamSendConformanceTest, SendBytesCompressFalseLargeIsUncompressedMultipacket) {
  const auto topic = uniqueTopic("bytes_nocomp_large");
  const std::string payload(40'000, '\x07');
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto opts = byteOptions(topic);
  opts.compress = false;
  (void)sender()->sendBytes(toBytes(payload), opts);

  const auto header = expectMultipacket(sniffer, topic);
  EXPECT_EQ(chunkSizes(sniffer, header.stream_id), (std::vector<std::size_t>{kMtu, kMtu, 10'000}));
  EXPECT_EQ(sniffer.rawContent(header.stream_id), payload);
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, payload);
}

// ---------------------------------------------------------------------------
// sendFile
// ---------------------------------------------------------------------------

// Rust: send_file_never_inlines_and_compresses_when_eligible
TEST_F(DataStreamSendConformanceTest, SendFileNeverInlinesAndCompresses) {
  const auto topic = uniqueTopic("file_z");
  const std::string content(10'000, '\x01');
  TempFile file("ones.bin", content);
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  const auto info = sender()->sendFile(file.path(), byteOptions(topic));

  EXPECT_EQ(info.name, "ones.bin") << "the name defaults to the file's name";
  const auto header = expectMultipacket(sniffer, topic);
  ASSERT_TRUE(header.total_length.has_value());
  EXPECT_EQ(*header.total_length, content.size());
  const auto sizes = chunkSizes(sniffer, header.stream_id);
  ASSERT_EQ(sizes.size(), 1U);
  EXPECT_LT(sizes.front(), content.size()) << "the chunk must carry compressed bytes";
  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_EQ(s.content, content);
  EXPECT_EQ(s.info.name, "ones.bin");
}

// Rust: send_file_uncompressed_splits_at_mtu, send_file_streams_one_packet_per_request
TEST_F(DataStreamSendConformanceTest, SendFileUncompressedSplitsAtMtu) {
  const auto topic = uniqueTopic("file_nocomp");
  const std::string content(20'000, '\x07');
  TempFile file("sevens.bin", content);
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto opts = byteOptions(topic);
  opts.compress = false;
  (void)sender()->sendFile(file.path(), opts);

  const auto header = expectMultipacket(sniffer, topic);
  EXPECT_EQ(chunkSizes(sniffer, header.stream_id), (std::vector<std::size_t>{kMtu, 5'000}));
  EXPECT_EQ(chunkIndices(sniffer, header.stream_id), (std::vector<std::uint64_t>{0, 1}));
  EXPECT_EQ(sniffer.rawContent(header.stream_id), content);
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, content);
}

// Rust: send_file_incompressible_compressed_expands
TEST_F(DataStreamSendConformanceTest, SendFileIncompressibleCompressedExpands) {
  const auto topic = uniqueTopic("file_random");
  const auto content = randomBytes(50'000);
  TempFile file("random.bin", content);
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  (void)sender()->sendFile(file.path(), byteOptions(topic));

  const auto header = expectMultipacket(sniffer, topic);
  const auto sizes = chunkSizes(sniffer, header.stream_id);
  ASSERT_EQ(sizes.size(), 4U);
  EXPECT_EQ(sizes.front(), kMtu);
  EXPECT_GT(sniffer.rawContent(header.stream_id).size(), content.size())
      << "sendFile compresses whenever the recipients support it, even if that expands the data";
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, content);
}

// Rust: send_file_empty_file, send_file_reports_progress_for_empty_file
TEST_F(DataStreamSendConformanceTest, SendFileEmptyFile) {
  const auto topic = uniqueTopic("file_empty");
  TempFile file("empty.bin", "");
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  (void)sender()->sendFile(file.path(), byteOptions(topic));

  const auto header = expectMultipacket(sniffer, topic);
  ASSERT_TRUE(header.total_length.has_value());
  EXPECT_EQ(*header.total_length, 0U);
  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, "");
}

TEST_F(DataStreamSendConformanceTest, SendFileMissingFileThrows) {
  const auto topic = uniqueTopic("file_missing");
  const auto missing = (std::filesystem::temp_directory_path() / uniqueStreamId("missing") / "nope.bin").string();
  EXPECT_THROW((void)sender()->sendFile(missing, byteOptions(topic)), DataStreamError);
}

// ---------------------------------------------------------------------------
// Stream writers
// ---------------------------------------------------------------------------

// Rust: stream_text_never_compresses_or_inlines, incremental_writer_sends_per_write
TEST_F(DataStreamSendConformanceTest, StreamTextSendsEachPacketAsItIsWritten) {
  const auto topic = uniqueTopic("writer_text");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto writer = sender()->streamText(textOptions(topic));
  const auto header = sniffer.waitForHeader(topic);
  ASSERT_TRUE(header.has_value()) << "the header is sent when the writer opens";
  EXPECT_EQ(header->stream_id, writer->streamId());
  EXPECT_TRUE(sniffer.chunks(header->stream_id).empty());

  writer->write("hello world");
  ASSERT_TRUE(sniffer.waitForChunks(header->stream_id, 1)) << "each write is sent immediately";
  EXPECT_EQ(sniffer.chunks(header->stream_id).front().content, "hello world") << "writers never compress";
  writer->write("!");
  ASSERT_TRUE(sniffer.waitForChunks(header->stream_id, 2));
  EXPECT_TRUE(sniffer.trailers(header->stream_id).empty());

  writer->close();
  ASSERT_TRUE(sniffer.waitForTrailer(header->stream_id).has_value());
  EXPECT_EQ(chunkIndices(sniffer, header->stream_id), (std::vector<std::uint64_t>{0, 1}));
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, "hello world!");
}

// Rust: stream_bytes_never_compresses_or_inlines
TEST_F(DataStreamSendConformanceTest, StreamBytesNeverCompressesOrInlines) {
  const auto topic = uniqueTopic("writer_bytes");
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto opts = byteOptions(topic);
  opts.name = "blob.bin";
  opts.total_length = 4;
  auto writer = sender()->streamBytes(opts);
  writer->write({0, 1, 2, 3});
  writer->close();

  const auto header = expectMultipacket(sniffer, topic);
  EXPECT_EQ(header.name, "blob.bin");
  ASSERT_TRUE(header.total_length.has_value()) << "a declared total_length is sent in the header";
  EXPECT_EQ(*header.total_length, 4U);
  EXPECT_EQ(sniffer.rawContent(header.stream_id), std::string("\x00\x01\x02\x03", 4));
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, std::string("\x00\x01\x02\x03", 4));
}

// Rust: stream_bytes_multi_write_splits_at_mtu
TEST_F(DataStreamSendConformanceTest, StreamBytesMultiWriteSplitsAtMtu) {
  const auto topic = uniqueTopic("writer_split");
  const std::vector<std::uint8_t> block(20'000, 0x01);
  RawPacketSniffer sniffer(receiverRoom());
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto writer = sender()->streamBytes(byteOptions(topic));
  writer->write(block);
  writer->write(block);
  writer->close();

  const auto header = expectMultipacket(sniffer, topic);
  EXPECT_EQ(chunkSizes(sniffer, header.stream_id), (std::vector<std::size_t>{kMtu, 5'000, kMtu, 5'000}));
  EXPECT_EQ(chunkIndices(sniffer, header.stream_id), (std::vector<std::uint64_t>{0, 1, 2, 3}));
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, std::string(40'000, '\x01'));
}

// Rust: stream_text_splits_on_utf8_boundaries_at_mtu (and the utf8_chunk tests)
TEST_F(DataStreamSendConformanceTest, StreamTextSplitsOnUtf8BoundariesAtMtu) {
  const auto topic = uniqueTopic("writer_utf8");
  const std::string text = std::string(14'999, 'a') + "\xF0\x9F\x98\x80" + std::string(10, 'b');
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto writer = sender()->streamText(textOptions(topic));
  writer->write(text);
  writer->close();

  const auto header = expectMultipacket(sniffer, topic);
  const auto chunks = sniffer.chunks(header.stream_id);
  ASSERT_EQ(chunks.size(), 2U);
  EXPECT_EQ(chunks[0].content.size(), 14'999U) << "the split moves back to the character boundary";
  EXPECT_TRUE(isValidUtf8(chunks[0].content));
  EXPECT_TRUE(isValidUtf8(chunks[1].content));
  EXPECT_EQ(chunks[0].content + chunks[1].content, text);
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, text);
}

// Rust: close_with_options_sends_trailer_attributes
TEST_F(DataStreamSendConformanceTest, CloseWithAttributesSendsTrailerAttributes) {
  const auto topic = uniqueTopic("writer_close_attrs");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto opts = textOptions(topic);
  opts.attributes = {{"stage", "start"}};
  auto writer = sender()->streamText(opts);
  writer->write("hello");
  writer->close("", {{"result", "ok"}, {"stage", "end"}});

  const auto header = expectMultipacket(sniffer, topic);
  const auto trailer = sniffer.trailers(header.stream_id);
  ASSERT_EQ(trailer.size(), 1U);
  EXPECT_EQ(trailer[0].reason, "");
  const std::map<std::string, std::string> trailer_attrs{{"result", "ok"}, {"stage", "end"}};
  EXPECT_EQ(trailer[0].attributes, trailer_attrs);
  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.info.attributes, trailer_attrs) << "trailer attributes override the header's";
}

// Rust: close_with_options_sends_reason_and_attributes
TEST_F(DataStreamSendConformanceTest, CloseWithReasonSendsReasonAndAttributes) {
  const auto topic = uniqueTopic("writer_close_reason");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto writer = sender()->streamText(textOptions(topic));
  writer->close("aborted", {{"cause", "cancelled"}});

  const auto header = sniffer.waitForHeader(topic);
  ASSERT_TRUE(header.has_value());
  const auto trailer = sniffer.waitForTrailer(header->stream_id);
  ASSERT_TRUE(trailer.has_value());
  EXPECT_EQ(trailer->reason, "aborted");
  EXPECT_EQ(trailer->attributes, (std::map<std::string, std::string>{{"cause", "cancelled"}}));
  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_TRUE(s.errored);
  EXPECT_NE(s.error.find("stream closed abnormally: aborted"), std::string::npos) << s.error;
}

// Rust: stream_text_oversized_attributes_errors
TEST_F(DataStreamSendConformanceTest, StreamTextOversizedAttributesThrows) {
  const auto topic = uniqueTopic("writer_big_attrs");
  RawPacketSniffer sniffer(receiverRoom());

  auto opts = textOptions(topic);
  opts.attributes = {{"big", std::string(20'000, 'x')}};
  try {
    (void)sender()->streamText(opts);
    FAIL() << "a header over the packet size limit must be rejected";
  } catch (const DataStreamError& e) {
    EXPECT_NE(std::string(e.what()).find("stream header exceeds maximum size"), std::string::npos) << e.what();
  }
  EXPECT_FALSE(sniffer.waitForHeader(topic, kQuietPeriod).has_value()) << "nothing may be sent";
}

// Rust: drop_raw_stream_on_non_tokio_thread_does_not_panic. Destroying an
// unclosed writer (on an application thread) must still end the stream.
TEST_F(DataStreamSendConformanceTest, DestroyedWriterSendsTrailer) {
  const auto topic = uniqueTopic("writer_dropped");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  std::thread([&] {
    auto writer = sender()->streamText(textOptions(topic));
    writer->write("unfinished");
  }).join();

  const auto header = expectMultipacket(sniffer, topic);
  EXPECT_FALSE(header.stream_id.empty());
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, "unfinished");
}

// ---------------------------------------------------------------------------
// Routing and identity
// ---------------------------------------------------------------------------

// Rust: packets_carry_no_identity_when_sender_identity_unset. Without an
// explicit sender identity, every packet is attributed to the sender.
TEST_F(DataStreamSendConformanceTest, PacketsAreAttributedToTheSender) {
  const auto topic = uniqueTopic("identity_default");
  RawPacketSniffer sniffer(receiverRoom());

  auto writer = sender()->streamText(textOptions(topic));
  writer->write("hello");
  writer->close();

  const auto header = expectMultipacket(sniffer, topic);
  EXPECT_EQ(header.sender, senderIdentity());
  for (const auto& c : sniffer.chunks(header.stream_id)) {
    EXPECT_EQ(c.sender, senderIdentity());
  }
  for (const auto& t : sniffer.trailers(header.stream_id)) {
    EXPECT_EQ(t.sender, senderIdentity());
  }
}

// Rust: empty_room_broadcast_is_v2_eligible
TEST_F(DataStreamSendConformanceTest, BroadcastToEmptyRoomSucceeds) {
  (void)receiverRoom().disconnect();
  const auto deadline = std::chrono::steady_clock::now() + kStreamTimeout;
  while (!senderRoom().remoteParticipants().empty() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(50ms);
  }
  ASSERT_TRUE(senderRoom().remoteParticipants().empty());
  const auto topic = uniqueTopic("empty_room");
  EXPECT_NO_THROW((void)sender()->sendText(kShortCompressible, textOptions(topic)));
  EXPECT_NO_THROW((void)sender()->sendBytes(toBytes(kShortCompressible), byteOptions(topic)));
}

// Rust: one_shot_send_text_targets_every_packet, stream_text_targets_every_packet,
//       send_file_targets_every_packet, compressed_send_file_progress_targets_every_packet,
//       mixed_targeted_v2_subset_inlines_compressed
TEST_F(DataStreamSendConformanceTest, TargetedStreamsReachOnlyTheirDestinations) {
  rooms_.clear();
  rooms_ = threeTestRooms();
  if (rooms_.empty()) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_C not set";
  }
  Room& target = *rooms_[1];
  Room& bystander = *rooms_[2];
  const auto topic = uniqueTopic("targeted");
  RawPacketSniffer target_sniffer(target);
  RawPacketSniffer bystander_sniffer(bystander);
  TextStreamCollector target_text;
  target_text.registerOn(target, topic);
  ByteStreamCollector target_bytes;
  target_bytes.registerOn(target, topic);
  TextStreamCollector bystander_text;
  bystander_text.registerOn(bystander, topic);
  ByteStreamCollector bystander_bytes;
  bystander_bytes.registerOn(bystander, topic);

  auto text_opts = textOptions(topic);
  text_opts.destination_identities = {identityOf(target)};
  auto byte_opts = byteOptions(topic);
  byte_opts.destination_identities = {identityOf(target)};

  // Inline, compressed multi-packet, writer, and file streams.
  (void)sender()->sendText(kShortCompressible, text_opts);
  const auto large = randomText(50'000);
  (void)sender()->sendText(large, text_opts);
  auto writer = sender()->streamText(text_opts);
  writer->write("written");
  writer->close();
  const auto file_content = randomBytes(20'000);
  TempFile file("targeted.bin", file_content);
  (void)sender()->sendFile(file.path(), byte_opts);

  ASSERT_TRUE(target_text.waitFor(3));
  ASSERT_TRUE(target_bytes.waitFor(1));
  for (const auto& s : target_text.streams()) {
    EXPECT_FALSE(s.errored) << s.error;
  }
  EXPECT_EQ(target_bytes.only().content, file_content);
  EXPECT_EQ(target_sniffer.headers(topic).size(), 4U);

  std::this_thread::sleep_for(kQuietPeriod);
  EXPECT_EQ(bystander_text.openedCount(), 0U) << "a non-targeted participant must not receive the stream";
  EXPECT_EQ(bystander_bytes.openedCount(), 0U) << "a non-targeted participant must not receive the stream";
  EXPECT_TRUE(bystander_sniffer.headers(topic).empty()) << "no packet may reach a non-targeted participant";
}

// Rust: stream_text_with_sender_identity_stamps_every_packet,
//       send_text_inline_with_sender_identity_stamps_packet,
//       one_shot_send_with_sender_identity_stamps_every_packet
// (partial). The Rust tests check the identity stamped on outgoing packets.
// End to end, the SFU attributes every packet to the authenticated sender
// unless its token allows impersonation, which the test tokens do not, so
// this checks that the option is accepted, every kind of send still
// delivers, and packets arrive attributed to the real sender.
TEST_F(DataStreamSendConformanceTest, SenderIdentityOptionIsAcceptedAndDelivers) {
  const auto topic = uniqueTopic("identity_override");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  auto opts = textOptions(topic);
  opts.sender_identity = "impostor";
  auto writer = sender()->streamText(opts);
  writer->write("written");
  writer->close();
  (void)sender()->sendText("inline", opts);
  opts.compress = false;
  const std::string chunked(20'000, 'A');
  (void)sender()->sendText(chunked, opts);

  ASSERT_TRUE(collector.waitFor(3));
  std::vector<std::string> contents;
  for (const auto& s : collector.streams()) {
    EXPECT_FALSE(s.errored) << s.error;
    EXPECT_EQ(s.sender, senderIdentity());
    contents.push_back(s.content);
  }
  std::sort(contents.begin(), contents.end());
  EXPECT_EQ(contents, (std::vector<std::string>{chunked, "inline", "written"}));
  for (const auto& h : sniffer.headers(topic)) {
    EXPECT_EQ(h.sender, senderIdentity());
    for (const auto& c : sniffer.chunks(h.stream_id)) {
      EXPECT_EQ(c.sender, senderIdentity());
    }
  }
}

} // namespace livekit::test::ds
