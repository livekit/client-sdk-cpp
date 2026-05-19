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
#include <livekit/data_track_stream.h>

#include <memory>
#include <optional>

#include "data_track.pb.h"
#include "ffi.pb.h"

namespace livekit {

class DataTrackStreamTest : public ::testing::Test {
protected:
  static std::unique_ptr<DataTrackStream> makeStream() {
    return std::unique_ptr<DataTrackStream>(new DataTrackStream());
  }

  static void pushEvent(DataTrackStream& stream, const proto::FfiEvent& event) { stream.onFfiEvent(event); }

  static void handleReadResponse(DataTrackStream& stream, const proto::DataTrackStreamReadResponse& response) {
    stream.handleReadResponse(response);
  }

  static void failProtocolError(DataTrackStream& stream, const char* message) { stream.failProtocolError(message); }

  static proto::FfiEvent makeEosEvent(std::optional<proto::SubscribeDataTrackErrorCode> code = std::nullopt,
                                      const std::string& message = {}) {
    proto::FfiEvent event;
    auto* stream_event = event.mutable_data_track_stream_event();
    stream_event->set_stream_handle(0);
    auto* eos = stream_event->mutable_eos();
    if (code.has_value()) {
      auto* error = eos->mutable_error();
      error->set_code(code.value());
      error->set_message(message);
    }
    return event;
  }

  static proto::DataTrackStreamReadResponse makeEosReadResponse(
      std::optional<proto::SubscribeDataTrackErrorCode> code = std::nullopt, const std::string& message = {}) {
    proto::DataTrackStreamReadResponse response;
    auto* eos = response.mutable_eos_event();
    if (code.has_value()) {
      auto* error = eos->mutable_error();
      error->set_code(code.value());
      error->set_message(message);
    }
    return response;
  }

  static proto::FfiEvent makeAudioStreamEvent() {
    proto::FfiEvent event;
    event.mutable_audio_stream_event()->set_stream_handle(0);
    return event;
  }

  static void expectTerminalError(const DataTrackStream& stream, SubscribeDataTrackErrorCode expected_code,
                                  const std::string& expected_message) {
    const auto error = stream.terminalError();
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->code, expected_code);
    EXPECT_EQ(error->message, expected_message);
  }
};

TEST_F(DataTrackStreamTest, TerminalErrorEmptyForNormalEos) {
  auto stream = makeStream();
  pushEvent(*stream, makeEosEvent());

  DataTrackFrame frame;
  EXPECT_FALSE(stream->read(frame));
  EXPECT_FALSE(stream->terminalError().has_value());
}

TEST_F(DataTrackStreamTest, ReadResponseNormalEosEndsStreamWithoutTerminalError) {
  auto stream = makeStream();
  handleReadResponse(*stream, makeEosReadResponse());

  DataTrackFrame frame;
  EXPECT_FALSE(stream->read(frame));
  EXPECT_FALSE(stream->terminalError().has_value());
}

TEST_F(DataTrackStreamTest, TerminalErrorStoredForSubscribeFailureEos) {
  auto stream = makeStream();
  pushEvent(*stream, makeEosEvent(proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_UNPUBLISHED,
                                  "track unpublished before subscription completed"));

  DataTrackFrame frame;
  EXPECT_FALSE(stream->read(frame));

  const auto error = stream->terminalError();
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code, SubscribeDataTrackErrorCode::UNPUBLISHED);
  EXPECT_EQ(error->message, "track unpublished before subscription completed");
}

TEST_F(DataTrackStreamTest, ReadResponseSubscribeFailureEosStoresTerminalError) {
  auto stream = makeStream();
  handleReadResponse(*stream, makeEosReadResponse(proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_UNPUBLISHED,
                                                  "track unpublished before read completed"));

  DataTrackFrame frame;
  EXPECT_FALSE(stream->read(frame));
  expectTerminalError(*stream, SubscribeDataTrackErrorCode::UNPUBLISHED, "track unpublished before read completed");
}

TEST_F(DataTrackStreamTest, ProtocolErrorClosesStreamAndStoresTerminalError) {
  auto stream = makeStream();

  EXPECT_NO_THROW(failProtocolError(*stream, "malformed FFI response"));

  DataTrackFrame frame;
  EXPECT_FALSE(stream->read(frame));
  expectTerminalError(*stream, SubscribeDataTrackErrorCode::PROTOCOL_ERROR, "malformed FFI response");
}

TEST_F(DataTrackStreamTest, CloseBeforeEosSuppressesLaterTerminalError) {
  auto stream = makeStream();
  stream->close();

  pushEvent(*stream,
            makeEosEvent(proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_DISCONNECTED, "disconnected after local close"));

  DataTrackFrame frame;
  EXPECT_FALSE(stream->read(frame));
  EXPECT_FALSE(stream->terminalError().has_value());
}

TEST_F(DataTrackStreamTest, CloseAfterEosPreservesTerminalError) {
  auto stream = makeStream();
  pushEvent(*stream, makeEosEvent(proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_TIMEOUT, "subscription timed out"));

  stream->close();

  DataTrackFrame frame;
  EXPECT_FALSE(stream->read(frame));
  expectTerminalError(*stream, SubscribeDataTrackErrorCode::TIMEOUT, "subscription timed out");
}

TEST_F(DataTrackStreamTest, DuplicateEosKeepsFirstTerminalError) {
  auto stream = makeStream();
  pushEvent(*stream, makeEosEvent(proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_UNPUBLISHED, "first terminal error"));
  pushEvent(*stream, makeEosEvent(proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_TIMEOUT, "second terminal error"));

  expectTerminalError(*stream, SubscribeDataTrackErrorCode::UNPUBLISHED, "first terminal error");
}

TEST_F(DataTrackStreamTest, DuplicateEosDoesNotReplaceNormalEndWithError) {
  auto stream = makeStream();
  pushEvent(*stream, makeEosEvent());
  pushEvent(*stream, makeEosEvent(proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_INTERNAL, "late terminal error"));

  DataTrackFrame frame;
  EXPECT_FALSE(stream->read(frame));
  EXPECT_FALSE(stream->terminalError().has_value());
}

TEST_F(DataTrackStreamTest, EventsAfterCloseDoNotReplaceExistingTerminalError) {
  auto stream = makeStream();
  pushEvent(*stream, makeEosEvent(proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_PROTOCOL_ERROR, "protocol error"));
  stream->close();
  pushEvent(*stream, makeEosEvent(proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_INTERNAL, "late error after close"));

  expectTerminalError(*stream, SubscribeDataTrackErrorCode::PROTOCOL_ERROR, "protocol error");
}

TEST_F(DataTrackStreamTest, NonDataTrackEventsAreIgnored) {
  auto stream = makeStream();
  pushEvent(*stream, makeAudioStreamEvent());

  EXPECT_FALSE(stream->terminalError().has_value());

  pushEvent(*stream,
            makeEosEvent(proto::SUBSCRIBE_DATA_TRACK_ERROR_CODE_TIMEOUT, "still accepts later data track eos"));

  expectTerminalError(*stream, SubscribeDataTrackErrorCode::TIMEOUT, "still accepts later data track eos");
}

} // namespace livekit
