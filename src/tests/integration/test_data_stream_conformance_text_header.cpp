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

// Text stream header fields (operation type, version, generated, reply-to)
// in both directions: another SDK's text stream must surface them on the C++
// TextStreamInfo, and C++ text streams must put them on the wire. Agents, for
// example, mark transcriptions with `generated`.
//
// Run with:
//   ./build-debug/bin/livekit_integration_tests --gtest_filter=DataStreamTextHeader*

#include <livekit/data_stream.h>

#include <string>

#include "../common/data_stream_test_utils.h"

namespace livekit::test::ds {

class DataStreamTextHeaderTest : public DataStreamConformanceTest {};

TEST_F(DataStreamTextHeaderTest, RemoteHeaderFieldsSurfaceOnInfo) {
  const auto topic = uniqueTopic("hdr_recv");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  FfiPeer peer(senderRoom());

  PeerTextOptions opts;
  opts.topic = topic;
  opts.operation_type = static_cast<int>(TextStreamOperationType::Update);
  opts.version = 3;
  opts.generated = true;
  opts.reply_to_stream_id = "original";
  ASSERT_TRUE(peer.sendText("edited", opts).ok); // one-shot (inline)
  const auto writer = peer.openText(opts);       // writer (multi-packet)
  ASSERT_TRUE(writer.ok) << writer.error;
  ASSERT_TRUE(peer.writeText(writer.writer_handle, "edited again").ok);
  ASSERT_TRUE(peer.closeText(writer.writer_handle).ok);

  ASSERT_TRUE(collector.waitFor(2));
  for (const auto& s : collector.streams()) {
    EXPECT_FALSE(s.errored) << s.error;
    EXPECT_EQ(s.info.operation_type, TextStreamOperationType::Update);
    ASSERT_TRUE(s.info.version.has_value());
    EXPECT_EQ(*s.info.version, 3);
    EXPECT_TRUE(s.info.generated);
    ASSERT_TRUE(s.info.reply_to_stream_id.has_value());
    EXPECT_EQ(*s.info.reply_to_stream_id, "original");
  }
}

TEST_F(DataStreamTextHeaderTest, LocalHeaderFieldsAreSent) {
  const auto topic = uniqueTopic("hdr_send");
  RawPacketSniffer sniffer(receiverRoom());
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  auto sender = lockLocalParticipant(senderRoom());

  StreamTextOptions opts;
  opts.topic = topic;
  opts.operation_type = TextStreamOperationType::Reaction;
  opts.version = 5;
  opts.generated = true;
  (void)sender->sendText("+1", opts);
  auto writer = sender->streamText(opts);
  EXPECT_EQ(writer->info().operation_type, TextStreamOperationType::Reaction);
  writer->write("+1 again");
  writer->close();

  ASSERT_TRUE(collector.waitFor(2));
  for (const auto& s : collector.streams()) {
    EXPECT_FALSE(s.errored) << s.error;
    EXPECT_EQ(s.info.operation_type, TextStreamOperationType::Reaction);
    ASSERT_TRUE(s.info.version.has_value());
    EXPECT_EQ(*s.info.version, 5);
    EXPECT_TRUE(s.info.generated);
  }
  const auto headers = sniffer.headers(topic);
  ASSERT_EQ(headers.size(), 2U);
  for (const auto& h : headers) {
    EXPECT_EQ(h.operation_type, static_cast<int>(TextStreamOperationType::Reaction));
    EXPECT_EQ(h.version, 5);
    EXPECT_TRUE(h.generated);
  }
}

TEST_F(DataStreamTextHeaderTest, DefaultsAreCreateUnversionedNotGenerated) {
  const auto topic = uniqueTopic("hdr_default");
  TextStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);

  StreamTextOptions opts;
  opts.topic = topic;
  (void)lockLocalParticipant(senderRoom())->sendText("plain", opts);

  ASSERT_TRUE(collector.waitFor(1));
  const auto s = collector.only();
  EXPECT_EQ(s.info.operation_type, TextStreamOperationType::Create);
  EXPECT_FALSE(s.info.generated);
}

} // namespace livekit::test::ds
