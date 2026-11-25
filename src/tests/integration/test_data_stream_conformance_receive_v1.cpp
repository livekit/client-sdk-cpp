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

// Receive-side data stream conformance, v1 (multi-packet, uncompressed)
// streams: a remote participant sends streams exactly as another SDK would,
// and the C++ SDK must receive them correctly. Covers hand-built v1 packets,
// remote stream writers, and participant disconnects mid-stream.
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
//   ./build-debug/bin/livekit_integration_tests --gtest_filter=DataStreamReceiveV1*

#include <livekit/data_stream.h>

#include <string>
#include <thread>
#include <vector>

#include "../common/data_stream_test_utils.h"

namespace livekit::test::ds {

class DataStreamReceiveV1Test : public DataStreamConformanceTest {};

// ---------------------------------------------------------------------------
// v1 multi-packet streams (hand-built header / chunk / trailer packets)
// ---------------------------------------------------------------------------

// Rust: v1_text_stream_round_trips
TEST_F(DataStreamReceiveV1Test, V1TextStreamRoundTrips) {
  const auto topic = uniqueTopic("v1_text");
  const auto sid = uniqueStreamId("v1_text");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  ASSERT_TRUE(peer.sendRawHeader({sid, topic, "text/plain", 11, {{"foo", "bar"}}}).ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 0, "hello world").ok);
  ASSERT_TRUE(peer.sendRawTrailer(sid).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, "hello world");
  EXPECT_EQ(s.sender, peer.identity());
  EXPECT_EQ(s.info.stream_id, sid);
  EXPECT_EQ(s.info.topic, topic);
  EXPECT_EQ(s.info.mime_type, "text/plain");
  ASSERT_TRUE(s.info.size.has_value());
  EXPECT_EQ(*s.info.size, 11U);
  EXPECT_EQ(s.info.attributes.at("foo"), "bar");
}

// Rust: v1_byte_stream_round_trips
TEST_F(DataStreamReceiveV1Test, V1ByteStreamRoundTrips) {
  const auto topic = uniqueTopic("v1_bytes");
  const auto sid = uniqueStreamId("v1_bytes");
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerRawHeader header{sid, topic, "application/octet-stream", 4};
  header.is_text = false;
  header.name = "file";
  ASSERT_TRUE(peer.sendRawHeader(header).ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 0, std::string("\x01\x02\x03\x04", 4)).ok);
  ASSERT_TRUE(peer.sendRawTrailer(sid).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, std::string("\x01\x02\x03\x04", 4));
  EXPECT_EQ(s.info.name, "file");
  EXPECT_EQ(s.info.mime_type, "application/octet-stream");
}

// Rust: v1_merges_trailer_attributes, v1_trailer_attributes_merged_after_close
TEST_F(DataStreamReceiveV1Test, V1TrailerAttributesMergedAfterClose) {
  const auto topic = uniqueTopic("v1_trailer_attrs");
  const auto sid = uniqueStreamId("v1_trailer_attrs");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  ASSERT_TRUE(peer.sendRawHeader({sid, topic, "text/plain", 11, {{"foo", "bar"}, {"baz", "quux"}}}).ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 0, "hello world").ok);
  ASSERT_TRUE(peer.sendRawTrailer(sid, "", {{"hello", "world"}, {"foo", "updated"}}).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, "hello world");
  const std::map<std::string, std::string> expected{{"baz", "quux"}, {"hello", "world"}, {"foo", "updated"}};
  EXPECT_EQ(s.info.attributes, expected) << "trailer attributes are merged over the header's";
}

// Rust: v1_errors_when_too_few_bytes
TEST_F(DataStreamReceiveV1Test, V1ErrorsWhenTooFewBytes) {
  const auto topic = uniqueTopic("v1_too_few");
  const auto sid = uniqueStreamId("v1_too_few");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  ASSERT_TRUE(peer.sendRawHeader({sid, topic, "text/plain", 5}).ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 0, "x").ok);
  ASSERT_TRUE(peer.sendRawTrailer(sid).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_TRUE(s.errored) << "a stream shorter than its declared total_length must fail";
  EXPECT_NE(s.error.find("incomplete"), std::string::npos) << s.error;
}

// Rust: v1_errors_when_too_many_bytes
TEST_F(DataStreamReceiveV1Test, V1ErrorsWhenTooManyBytes) {
  const auto topic = uniqueTopic("v1_too_many");
  const auto sid = uniqueStreamId("v1_too_many");
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerRawHeader header{sid, topic, "application/octet-stream", 3};
  header.is_text = false;
  ASSERT_TRUE(peer.sendRawHeader(header).ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 0, std::string("\x01\x02\x03\x04\x05", 5)).ok);
  ASSERT_TRUE(peer.sendRawTrailer(sid).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_TRUE(s.errored) << "a stream longer than its declared total_length must fail";
  EXPECT_NE(s.error.find("exceeded total length"), std::string::npos) << s.error;
}

// Rust: v1_duplicate_chunk_dropped
TEST_F(DataStreamReceiveV1Test, V1DuplicateChunkDropped) {
  const auto topic = uniqueTopic("v1_dup");
  const auto sid = uniqueStreamId("v1_dup");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  ASSERT_TRUE(peer.sendRawHeader({sid, topic, "text/plain", 10}).ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 0, "hello").ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 0, "hello").ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 1, "world").ok);
  ASSERT_TRUE(peer.sendRawTrailer(sid).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, "helloworld");
}

// Rust: empty_chunks_are_ignored
TEST_F(DataStreamReceiveV1Test, EmptyChunksAreIgnored) {
  const auto topic = uniqueTopic("v1_empty_chunk");
  const auto sid = uniqueStreamId("v1_empty_chunk");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  ASSERT_TRUE(peer.sendRawHeader({sid, topic, "text/plain", 11}).ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 0, "").ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 1, "hello world").ok);
  ASSERT_TRUE(peer.sendRawTrailer(sid).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, "hello world");
}

// Rust: error_close_emits_stream_closed (MissedChunk)
TEST_F(DataStreamReceiveV1Test, V1MissedChunkErrors) {
  const auto topic = uniqueTopic("v1_gap");
  const auto sid = uniqueStreamId("v1_gap");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  ASSERT_TRUE(peer.sendRawHeader({sid, topic, "text/plain", 10}).ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 5, "hello").ok);
  ASSERT_TRUE(peer.sendRawTrailer(sid).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_TRUE(s.errored) << "a chunk index gap must fail the stream";
  EXPECT_NE(s.error.find("chunk index"), std::string::npos) << s.error;
}

// Rust: trailer_with_reason_errors_abnormal_end
TEST_F(DataStreamReceiveV1Test, V1TrailerWithReasonErrorsAbnormalEnd) {
  const auto topic = uniqueTopic("v1_reason");
  const auto sid = uniqueStreamId("v1_reason");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  ASSERT_TRUE(peer.sendRawHeader({sid, topic, "text/plain", 10}).ok);
  ASSERT_TRUE(peer.sendRawChunk(sid, 0, "hello").ok);
  ASSERT_TRUE(peer.sendRawTrailer(sid, "cancelled").ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_TRUE(s.errored) << "a trailer with a reason ends the stream abnormally";
  EXPECT_NE(s.error.find("stream closed abnormally: cancelled"), std::string::npos)
      << "the reason takes precedence over Incomplete: " << s.error;
}

// Rust: chunk_for_an_unopened_stream_reports_no_topic,
//       trailer_for_unopened_stream_emits_no_stream_closed
TEST_F(DataStreamReceiveV1Test, OrphanChunkAndTrailerAreIgnored) {
  const auto topic = uniqueTopic("orphans");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  ASSERT_TRUE(peer.sendRawChunk(uniqueStreamId("never_opened"), 0, "hi").ok);
  ASSERT_TRUE(peer.sendRawTrailer(uniqueStreamId("never_opened")).ok);
  PeerTextOptions opts;
  opts.topic = topic;
  ASSERT_TRUE(peer.sendText("hi", opts).ok);

  ASSERT_TRUE(collector.waitFor(1));
  std::this_thread::sleep_for(kQuietPeriod);
  ASSERT_EQ(collector.streams().size(), 1U) << "orphan packets must not open a stream";
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, "hi");
}

// ---------------------------------------------------------------------------
// Remote stream writers (chunked, uncompressed, incremental)
// ---------------------------------------------------------------------------

// Rust: v1_merges_trailer_attributes via a writer (close_with_options),
//       progress_reports_completion_uncompressed_bytes (three writes)
TEST_F(DataStreamReceiveV1Test, RemoteTextWriterRoundTripsWithTrailerAttributes) {
  const auto topic = uniqueTopic("remote_writer");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerTextOptions opts;
  opts.topic = topic;
  opts.attributes = {{"foo", "bar"}};
  const auto writer = peer.openText(opts);
  ASSERT_TRUE(writer.ok) << writer.error;
  ASSERT_TRUE(peer.writeText(writer.writer_handle, "hello ").ok);
  ASSERT_TRUE(peer.writeText(writer.writer_handle, "streaming ").ok);
  ASSERT_TRUE(peer.writeText(writer.writer_handle, "world").ok);
  ASSERT_TRUE(peer.closeText(writer.writer_handle, "", {{"result", "ok"}, {"foo", "updated"}}).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_FALSE(s.errored) << s.error;
  EXPECT_EQ(s.content, "hello streaming world");
  EXPECT_EQ(s.info.stream_id, writer.stream_id);
  const std::map<std::string, std::string> expected{{"foo", "updated"}, {"result", "ok"}};
  EXPECT_EQ(s.info.attributes, expected);
}

// Rust: trailer_with_reason_errors_abnormal_end via a writer (close_with_reason)
TEST_F(DataStreamReceiveV1Test, RemoteWriterCloseWithReasonErrorsAbnormalEnd) {
  const auto topic = uniqueTopic("remote_reason");
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerByteOptions opts;
  opts.topic = topic;
  opts.name = "upload.bin";
  const auto writer = peer.openBytes(opts);
  ASSERT_TRUE(writer.ok) << writer.error;
  ASSERT_TRUE(peer.writeBytes(writer.writer_handle, "partial").ok);
  ASSERT_TRUE(peer.closeBytes(writer.writer_handle, "aborted", {{"cause", "cancelled"}}).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_TRUE(s.errored) << "a close reason ends the stream abnormally";
  EXPECT_NE(s.error.find("aborted"), std::string::npos) << s.error;
}

// Rust: v1_errors_when_too_few_bytes via a writer (declared total_length)
TEST_F(DataStreamReceiveV1Test, RemoteWriterTooFewBytesErrors) {
  const auto topic = uniqueTopic("remote_too_few");
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerByteOptions opts;
  opts.topic = topic;
  opts.name = "short.bin";
  opts.total_length = 5;
  const auto writer = peer.openBytes(opts);
  ASSERT_TRUE(writer.ok) << writer.error;
  ASSERT_TRUE(peer.writeBytes(writer.writer_handle, "x").ok);
  ASSERT_TRUE(peer.closeBytes(writer.writer_handle).ok);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_TRUE(s.errored) << "a stream shorter than its declared total_length must fail";
  EXPECT_NE(s.error.find("incomplete"), std::string::npos) << s.error;
}

// Rust: v1_errors_when_too_many_bytes via a writer (declared total_length)
TEST_F(DataStreamReceiveV1Test, RemoteWriterTooManyBytesErrors) {
  const auto topic = uniqueTopic("remote_too_many");
  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerByteOptions opts;
  opts.topic = topic;
  opts.name = "long.bin";
  opts.total_length = 3;
  const auto writer = peer.openBytes(opts);
  ASSERT_TRUE(writer.ok) << writer.error;
  ASSERT_TRUE(peer.writeBytes(writer.writer_handle, "12345").ok);
  (void)peer.closeBytes(writer.writer_handle);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_TRUE(s.errored) << "a stream longer than its declared total_length must fail";
  EXPECT_NE(s.error.find("exceeded total length"), std::string::npos) << s.error;
}

// ---------------------------------------------------------------------------
// Disconnects
// ---------------------------------------------------------------------------

// Rust: errors_open_streams_on_sender_disconnect, abort_emits_stream_closed
TEST_F(DataStreamReceiveV1Test, SenderDisconnectErrorsOpenStreams) {
  const auto topic = uniqueTopic("sender_gone");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerTextOptions opts;
  opts.topic = topic;
  const auto writer = peer.openText(opts);
  ASSERT_TRUE(writer.ok) << writer.error;
  ASSERT_TRUE(peer.writeText(writer.writer_handle, "hello").ok);
  ASSERT_TRUE(collector.waitForOpened(1));

  // The sender leaves mid-stream.
  rooms_[0].reset();

  ASSERT_TRUE(collector.waitFor(1)) << "a stream whose sender disconnected must end";
  const auto s = collector.only();
  EXPECT_TRUE(s.errored) << "a stream whose sender disconnected must end with an error";
  EXPECT_NE(s.error.find("unexpectedly disconnected"), std::string::npos) << s.error;
}

// Rust: shutdown_errors_open_streams, dropping_all_inputs_errors_open_streams
TEST_F(DataStreamReceiveV1Test, ReceiverDisconnectErrorsOpenStreams) {
  const auto text_topic = uniqueTopic("receiver_gone_text");
  const auto byte_topic = uniqueTopic("receiver_gone_bytes");
  TextStreamCollector text_collector;
  text_collector.registerOn(receiverRoom(), text_topic);
  ByteStreamCollector byte_collector;
  byte_collector.registerOn(receiverRoom(), byte_topic);
  FfiPeer peer(senderRoom());

  PeerTextOptions text_opts;
  text_opts.topic = text_topic;
  const auto text_writer = peer.openText(text_opts);
  ASSERT_TRUE(text_writer.ok) << text_writer.error;
  ASSERT_TRUE(peer.writeText(text_writer.writer_handle, "hello").ok);
  PeerByteOptions byte_opts;
  byte_opts.topic = byte_topic;
  byte_opts.name = "pending.bin";
  const auto byte_writer = peer.openBytes(byte_opts);
  ASSERT_TRUE(byte_writer.ok) << byte_writer.error;
  ASSERT_TRUE(peer.writeBytes(byte_writer.writer_handle, "hello").ok);
  ASSERT_TRUE(text_collector.waitForOpened(1));
  ASSERT_TRUE(byte_collector.waitForOpened(1));

  // The receiver leaves while both streams are open.
  (void)receiverRoom().disconnect();

  ASSERT_TRUE(text_collector.waitFor(1)) << "open text stream must end when the receiver disconnects";
  ASSERT_TRUE(byte_collector.waitFor(1)) << "open byte stream must end when the receiver disconnects";
  EXPECT_TRUE(text_collector.only().errored);
  EXPECT_TRUE(byte_collector.only().errored);

  (void)peer.closeText(text_writer.writer_handle);
  (void)peer.closeBytes(byte_writer.writer_handle);
}

// Rust: abort_only_affects_matching_sender
TEST_F(DataStreamReceiveV1Test, SenderDisconnectOnlyAffectsItsOwnStreams) {
  rooms_.clear();
  auto rooms = threeTestRooms();
  if (rooms.empty()) {
    GTEST_SKIP() << "LIVEKIT_TOKEN_C not set";
  }
  const auto topic = uniqueTopic("abort_scope");
  TextStreamCollector collector;
  collector.registerOn(*rooms[1], topic);
  FfiPeer leaving(*rooms[0]);
  FfiPeer staying(*rooms[2]);

  PeerTextOptions opts;
  opts.topic = topic;
  const auto leaving_writer = leaving.openText(opts);
  const auto staying_writer = staying.openText(opts);
  ASSERT_TRUE(leaving_writer.ok && staying_writer.ok);
  ASSERT_TRUE(leaving.writeText(leaving_writer.writer_handle, "doomed").ok);
  ASSERT_TRUE(staying.writeText(staying_writer.writer_handle, "hello").ok);
  ASSERT_TRUE(collector.waitForOpened(2));

  rooms[0].reset();
  ASSERT_TRUE(collector.waitFor(1));
  ASSERT_TRUE(staying.closeText(staying_writer.writer_handle).ok);
  ASSERT_TRUE(collector.waitFor(2, 2 * kStreamTimeout)) << "the remaining sender's stream must still complete";

  for (const auto& s : collector.streams()) {
    if (s.sender == staying.identity()) {
      EXPECT_FALSE(s.errored) << s.error;
      EXPECT_EQ(s.content, "hello");
    } else {
      EXPECT_TRUE(s.errored) << "the departed sender's stream must end with an error";
    }
  }
}

} // namespace livekit::test::ds
