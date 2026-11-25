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

#include <gtest/gtest.h>
#include <livekit/data_stream.h>
#include <livekit/local_participant.h>
#include <livekit/room.h>

#include <chrono>
#include <future>
#include <thread>
#include <type_traits>

#include "data_stream_proto_converter.h"
#include "ffi_client.h"
#include "room_proto_converter.h"

namespace livekit {

/// Feeds synthetic FFI reader events into detached readers.
struct DataStreamReaderTestAccess {
  static void chunk(TextStreamReader& reader, std::string text) { reader.onChunkReceived(std::move(text)); }
  static void chunk(ByteStreamReader& reader, std::vector<std::uint8_t> bytes) {
    reader.onChunkReceived(std::move(bytes));
  }
  template <typename Reader>
  static void end(Reader& reader, const std::map<std::string, std::string>& attributes = {},
                  std::optional<std::string> error = std::nullopt) {
    reader.onStreamEnded(attributes, std::move(error));
  }
};

} // namespace livekit

namespace livekit::test {

using namespace std::chrono_literals;
using Access = DataStreamReaderTestAccess;

namespace {

// The proto is built inside liblivekit, which carries its own protobuf copy
// with a different hash seed; iterate instead of using Map::at() / find().
std::map<std::string, std::string> attrs(const google::protobuf::Map<std::string, std::string>& in) {
  std::map<std::string, std::string> out;
  for (const auto& kv : in) {
    out.emplace(kv.first, kv.second);
  }
  return out;
}

} // namespace

TEST(DataStreamTest, TextStreamReaderConstructAndInfo) {
  TextStreamInfo info;
  info.stream_id = "stream-1";
  info.mime_type = "text/plain";
  info.topic = "chat";
  info.timestamp = 42;
  info.attachments = {"a.txt"};

  TextStreamReader reader(info);
  EXPECT_EQ(reader.info().stream_id, "stream-1");
  EXPECT_EQ(reader.info().mime_type, "text/plain");
  EXPECT_EQ(reader.info().topic, "chat");
  EXPECT_EQ(reader.info().timestamp, 42);
  ASSERT_EQ(reader.info().attachments.size(), 1u);
  EXPECT_EQ(reader.info().attachments.front(), "a.txt");
}

TEST(DataStreamTest, ByteStreamReaderConstructAndInfo) {
  ByteStreamInfo info;
  info.stream_id = "stream-2";
  info.mime_type = "application/octet-stream";
  info.topic = "files";
  info.name = "data.bin";

  ByteStreamReader reader(info);
  EXPECT_EQ(reader.info().stream_id, "stream-2");
  EXPECT_EQ(reader.info().name, "data.bin");
}

TEST(DataStreamTest, WriterTypesAreDerivedFromBase) {
  static_assert(std::is_base_of_v<BaseStreamWriter, TextStreamWriter>);
  static_assert(std::is_base_of_v<BaseStreamWriter, ByteStreamWriter>);
  // Deprecated, but must keep compiling for existing callers.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  EXPECT_GT(kStreamChunkSize, 0u);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
}

// ---------------------------------------------------------------------------
// Reader state machine (detached readers fed synthetic events)
// ---------------------------------------------------------------------------

TEST(DataStreamReaderTest, TextReaderDeliversChunksThenEnds) {
  TextStreamReader reader(TextStreamInfo{});
  Access::chunk(reader, "hello, ");
  Access::chunk(reader, "world");
  Access::end(reader);
  EXPECT_EQ(reader.readAll(), "hello, world");

  std::string out;
  EXPECT_FALSE(reader.readNext(out)) << "readNext after the end keeps returning false";
}

TEST(DataStreamReaderTest, ByteReaderReadAllConcatenates) {
  ByteStreamReader reader(ByteStreamInfo{});
  Access::chunk(reader, std::vector<std::uint8_t>{1, 2});
  Access::chunk(reader, std::vector<std::uint8_t>{3});
  Access::end(reader);
  EXPECT_EQ(reader.readAll(), (std::vector<std::uint8_t>{1, 2, 3}));
}

TEST(DataStreamReaderTest, EndAttributesAreMergedAfterEnd) {
  TextStreamInfo info;
  info.attributes = {{"header", "h"}, {"shared", "old"}};
  TextStreamReader reader(info);
  Access::chunk(reader, "x");
  Access::end(reader, {{"trailer", "t"}, {"shared", "new"}});

  std::string out;
  ASSERT_TRUE(reader.readNext(out));
  EXPECT_EQ(reader.info().attributes.count("trailer"), 0u) << "attributes are merged only once the end is reached";
  EXPECT_FALSE(reader.readNext(out));
  EXPECT_EQ(reader.info().attributes.at("header"), "h");
  EXPECT_EQ(reader.info().attributes.at("trailer"), "t");
  EXPECT_EQ(reader.info().attributes.at("shared"), "new");
}

TEST(DataStreamReaderTest, ErrorIsThrownAfterBufferedChunks) {
  ByteStreamReader reader(ByteStreamInfo{});
  Access::chunk(reader, std::vector<std::uint8_t>{42});
  Access::end(reader, {}, std::string("stream closed abnormally: boom"));

  std::vector<std::uint8_t> out;
  ASSERT_TRUE(reader.readNext(out));
  EXPECT_EQ(out, std::vector<std::uint8_t>{42});
  try {
    (void)reader.readNext(out);
    FAIL() << "expected DataStreamError";
  } catch (const DataStreamError& e) {
    EXPECT_NE(std::string(e.what()).find("boom"), std::string::npos);
  }
  EXPECT_THROW((void)reader.readNext(out), DataStreamError) << "the error is sticky";
}

TEST(DataStreamReaderTest, ReadAllThrowsOnError) {
  TextStreamReader reader(TextStreamInfo{});
  Access::chunk(reader, "partial");
  Access::end(reader, {}, std::string("Disconnected while receiving"));
  EXPECT_THROW((void)reader.readAll(), DataStreamError);
}

TEST(DataStreamReaderTest, DataStreamErrorIsARuntimeError) {
  TextStreamReader reader(TextStreamInfo{});
  Access::end(reader, {}, std::string("boom"));
  EXPECT_THROW((void)reader.readAll(), std::runtime_error);
}

TEST(DataStreamReaderTest, CloseWakesBlockedRead) {
  auto reader = std::make_shared<TextStreamReader>(TextStreamInfo{});
  auto result = std::async(std::launch::async, [reader] {
    std::string out;
    return reader->readNext(out);
  });
  EXPECT_EQ(result.wait_for(100ms), std::future_status::timeout) << "readNext should block with no data";

  reader->close();
  ASSERT_EQ(result.wait_for(5s), std::future_status::ready);
  EXPECT_FALSE(result.get());
}

TEST(DataStreamReaderTest, CloseDiscardsBufferedDataAndIsIdempotent) {
  ByteStreamReader reader(ByteStreamInfo{});
  Access::chunk(reader, std::vector<std::uint8_t>{1});
  reader.close();
  reader.close();
  Access::chunk(reader, std::vector<std::uint8_t>{2});
  Access::end(reader, {}, std::string("ignored after close"));

  std::vector<std::uint8_t> out;
  EXPECT_FALSE(reader.readNext(out));
  EXPECT_TRUE(reader.readAll().empty());
}

TEST(DataStreamReaderTest, TerminalErrorWakesBlockedRead) {
  auto reader = std::make_shared<ByteStreamReader>(ByteStreamInfo{});
  auto result = std::async(std::launch::async, [reader] { return reader->readAll(); });
  EXPECT_EQ(result.wait_for(100ms), std::future_status::timeout);

  Access::end(*reader, {}, std::string("Disconnected while receiving"));
  ASSERT_EQ(result.wait_for(5s), std::future_status::ready);
  EXPECT_THROW((void)result.get(), DataStreamError);
}

// ---------------------------------------------------------------------------
// Option / info conversion
// ---------------------------------------------------------------------------

TEST(DataStreamConversionTest, TextOptionsDefaultsToProto) {
  StreamTextOptions options;
  options.topic = "chat";
  const auto proto = toProto(options);
  EXPECT_EQ(proto.topic(), "chat");
  ASSERT_TRUE(proto.has_compress()) << "compress is always sent explicitly";
  EXPECT_TRUE(proto.compress());
  EXPECT_FALSE(proto.has_id());
  EXPECT_FALSE(proto.has_reply_to_stream_id());
  EXPECT_FALSE(proto.has_sender_identity());
  EXPECT_FALSE(proto.has_operation_type()) << "Create is the wire default";
  EXPECT_FALSE(proto.has_version());
  EXPECT_FALSE(proto.has_generated());
  EXPECT_EQ(proto.destination_identities_size(), 0);
}

TEST(DataStreamConversionTest, TextOptionsAllFieldsToProto) {
  StreamTextOptions options;
  options.topic = "chat";
  options.attributes = {{"k", "v"}};
  options.destination_identities = {"alice", "bob"};
  options.stream_id = "stream-1";
  options.reply_to_stream_id = "stream-0";
  options.attached_stream_ids = {"file-1"};
  options.operation_type = TextStreamOperationType::Update;
  options.version = 3;
  options.generated = true;
  options.compress = false;
  options.sender_identity = "agent";

  const auto proto = toProto(options);
  EXPECT_EQ(proto.operation_type(), proto::TextStreamInfo::UPDATE);
  EXPECT_EQ(proto.version(), 3);
  EXPECT_TRUE(proto.generated());
  EXPECT_EQ(attrs(proto.attributes()), (std::map<std::string, std::string>{{"k", "v"}}));
  ASSERT_EQ(proto.destination_identities_size(), 2);
  EXPECT_EQ(proto.destination_identities(1), "bob");
  EXPECT_EQ(proto.id(), "stream-1");
  EXPECT_EQ(proto.reply_to_stream_id(), "stream-0");
  ASSERT_EQ(proto.attached_stream_ids_size(), 1);
  EXPECT_EQ(proto.attached_stream_ids(0), "file-1");
  ASSERT_TRUE(proto.has_compress());
  EXPECT_FALSE(proto.compress());
  EXPECT_EQ(proto.sender_identity(), "agent");
}

TEST(DataStreamConversionTest, ByteOptionsToProto) {
  StreamByteOptions defaults;
  defaults.topic = "files";
  const auto unset = toProto(defaults);
  EXPECT_TRUE(unset.has_compress());
  EXPECT_TRUE(unset.compress());
  EXPECT_FALSE(unset.has_name());
  EXPECT_FALSE(unset.has_mime_type());
  EXPECT_FALSE(unset.has_total_length());
  EXPECT_FALSE(unset.has_id());
  EXPECT_FALSE(unset.has_sender_identity());

  StreamByteOptions options;
  options.topic = "files";
  options.attributes = {{"k", "v"}};
  options.destination_identities = {"alice"};
  options.stream_id = "stream-2";
  options.name = "photo.jpg";
  options.mime_type = "image/jpeg";
  options.total_length = 1234;
  options.compress = false;
  options.sender_identity = "agent";
  const auto proto = toProto(options);
  EXPECT_EQ(proto.topic(), "files");
  EXPECT_EQ(attrs(proto.attributes()), (std::map<std::string, std::string>{{"k", "v"}}));
  EXPECT_EQ(proto.destination_identities(0), "alice");
  EXPECT_EQ(proto.id(), "stream-2");
  EXPECT_EQ(proto.name(), "photo.jpg");
  EXPECT_EQ(proto.mime_type(), "image/jpeg");
  EXPECT_EQ(proto.total_length(), 1234u);
  EXPECT_FALSE(proto.compress());
  EXPECT_EQ(proto.sender_identity(), "agent");
}

TEST(DataStreamConversionTest, TextInfoFromProto) {
  proto::TextStreamInfo in;
  in.set_stream_id("s1");
  in.set_timestamp(99);
  in.set_mime_type("text/plain");
  in.set_topic("chat");
  (*in.mutable_attributes())["k"] = "v";
  in.add_attached_stream_ids("a1");
  in.set_reply_to_stream_id("s0");

  auto out = fromProto(in);
  EXPECT_EQ(out.stream_id, "s1");
  EXPECT_EQ(out.timestamp, 99);
  EXPECT_EQ(out.mime_type, "text/plain");
  EXPECT_EQ(out.topic, "chat");
  EXPECT_EQ(out.attributes.at("k"), "v");
  EXPECT_EQ(out.attachments, std::vector<std::string>{"a1"});
  ASSERT_TRUE(out.reply_to_stream_id.has_value());
  EXPECT_EQ(*out.reply_to_stream_id, "s0");
  EXPECT_FALSE(out.size.has_value()) << "an absent total_length stays unset";

  in.set_total_length(10);
  in.clear_reply_to_stream_id();
  out = fromProto(in);
  ASSERT_TRUE(out.size.has_value());
  EXPECT_EQ(*out.size, 10u);
  EXPECT_FALSE(out.reply_to_stream_id.has_value());
  EXPECT_EQ(out.operation_type, TextStreamOperationType::Create);
  EXPECT_FALSE(out.version.has_value());
  EXPECT_FALSE(out.generated);

  in.set_operation_type(proto::TextStreamInfo::REACTION);
  in.set_version(7);
  in.set_generated(true);
  out = fromProto(in);
  EXPECT_EQ(out.operation_type, TextStreamOperationType::Reaction);
  ASSERT_TRUE(out.version.has_value());
  EXPECT_EQ(*out.version, 7);
  EXPECT_TRUE(out.generated);
}

TEST(DataStreamConversionTest, TextOperationTypeRoundTrips) {
  for (const auto type : {TextStreamOperationType::Create, TextStreamOperationType::Update,
                          TextStreamOperationType::Delete, TextStreamOperationType::Reaction}) {
    EXPECT_EQ(fromProto(toProto(type)), type);
  }
}

TEST(DataStreamConversionTest, ByteInfoFromProto) {
  proto::ByteStreamInfo in;
  in.set_stream_id("s2");
  in.set_timestamp(5);
  in.set_mime_type("image/png");
  in.set_topic("files");
  in.set_name("a.png");
  in.set_total_length(2048);

  const auto out = fromProto(in);
  EXPECT_EQ(out.stream_id, "s2");
  EXPECT_EQ(out.mime_type, "image/png");
  EXPECT_EQ(out.topic, "files");
  EXPECT_EQ(out.name, "a.png");
  ASSERT_TRUE(out.size.has_value());
  EXPECT_EQ(*out.size, 2048u);
}

TEST(DataStreamConversionTest, RoomOptionsDataStreamToProto) {
  RoomOptions options;
  EXPECT_FALSE(toProto(options).has_data_stream()) << "unset limit leaves the SDK default";

  options.data_stream.max_payload_byte_length = 1000;
  const auto proto = toProto(options);
  ASSERT_TRUE(proto.has_data_stream());
  EXPECT_EQ(proto.data_stream().max_payload_byte_length(), 1000u);
}

// ---------------------------------------------------------------------------
// One-shot sends refuse to reach an uninitialized FFI
// ---------------------------------------------------------------------------

TEST(DataStreamSendTest, SendsThrowWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  LocalParticipant participant(FfiHandle(0), "sid", "name", "identity", "", {}, ParticipantKind::Standard,
                               DisconnectReason::Unknown);
  StreamTextOptions text_options;
  text_options.topic = "t";
  StreamByteOptions byte_options;
  byte_options.topic = "t";
  EXPECT_THROW((void)participant.sendText("hi", text_options), DataStreamError);
  EXPECT_THROW((void)participant.sendBytes({1, 2}, byte_options), DataStreamError);
  EXPECT_THROW((void)participant.sendFile("/tmp/does-not-matter", byte_options), DataStreamError);
}

TEST(DataStreamSendTest, FfiHelpersThrowWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  EXPECT_THROW(FfiClient::instance().sendTextAsync(1, "hi", proto::StreamTextOptions{}), std::runtime_error);
  EXPECT_THROW(FfiClient::instance().sendBytesAsync(1, {1}, proto::StreamByteOptions{}), std::runtime_error);
  EXPECT_THROW(FfiClient::instance().sendFileAsync(1, "/tmp/x", proto::StreamByteOptions{}), std::runtime_error);
  EXPECT_THROW(FfiClient::instance().textStreamReadIncremental(1), std::runtime_error);
  EXPECT_THROW(FfiClient::instance().byteStreamReadIncremental(1), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Writers without a live room
// ---------------------------------------------------------------------------

namespace {

std::shared_ptr<LocalParticipant> detachedParticipant() {
  return std::make_shared<LocalParticipant>(FfiHandle(0), "sid", "name", "identity", "",
                                            std::unordered_map<std::string, std::string>{}, ParticipantKind::Standard,
                                            DisconnectReason::Unknown);
}

} // namespace

TEST(DataStreamWriterTest, FactoriesThrowWhenNotInitialized) {
  ASSERT_FALSE(FfiClient::instance().isInitialized());
  auto participant = detachedParticipant();
  StreamTextOptions text_options;
  text_options.topic = "t";
  StreamByteOptions byte_options;
  byte_options.topic = "t";
  EXPECT_THROW((void)participant->streamText(text_options), DataStreamError);
  EXPECT_THROW((void)participant->streamBytes(byte_options), DataStreamError);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

TEST(DataStreamWriterTest, DeprecatedConstructorsKeepMetadata) {
  auto participant = detachedParticipant();
  TextStreamWriter text(*participant, "chat", {{"k", "v"}}, "my-id", 42, "reply-id");
  EXPECT_EQ(text.streamId(), "my-id");
  EXPECT_EQ(text.topic(), "chat");
  EXPECT_EQ(text.mimeType(), "text/plain");
  EXPECT_FALSE(text.isClosed());
  EXPECT_EQ(text.info().attributes.at("k"), "v");
  ASSERT_TRUE(text.info().size.has_value());
  EXPECT_EQ(*text.info().size, 42u);
  EXPECT_EQ(text.info().reply_to_stream_id.value_or(""), "reply-id");

  ByteStreamWriter bytes(*participant, "file.bin", "files");
  EXPECT_EQ(bytes.streamId().size(), 32u) << "a stream id is generated when none is given";
  EXPECT_EQ(bytes.mimeType(), "application/octet-stream");
  EXPECT_EQ(bytes.info().name, "file.bin");
}

TEST(DataStreamWriterTest, WritersCloseWhenParticipantGoes) {
  auto participant = detachedParticipant();
  auto writer = std::make_unique<TextStreamWriter>(*participant, "chat");
  ASSERT_FALSE(writer->isClosed());

  participant.reset(); // Closes registered writers.
  EXPECT_TRUE(writer->isClosed());
  EXPECT_THROW(writer->write("late"), DataStreamError);
  EXPECT_THROW(writer->close(), DataStreamError);
  writer.reset(); // Must not touch the destroyed participant.
}

TEST(DataStreamWriterTest, WriteToClosedWriterThrowsWithoutReachingFfi) {
  auto participant = detachedParticipant();
  ByteStreamWriter writer(*participant, "file.bin", "files");
  // An empty write is a no-op and must not open the stream.
  EXPECT_NO_THROW(writer.write({}));
  // Without an initialized FFI the open fails, and the writer stays usable
  // only to report errors.
  EXPECT_THROW(writer.write({1, 2, 3}), std::runtime_error);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace livekit::test
