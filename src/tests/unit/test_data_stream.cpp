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

#include <type_traits>

namespace livekit::test {

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
  EXPECT_GT(kStreamChunkSize, 0u);
}

} // namespace livekit::test
