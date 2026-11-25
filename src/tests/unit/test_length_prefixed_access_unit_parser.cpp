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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <livekit/length_prefixed_access_unit_parser.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace livekit::test {
namespace {

using Bytes = std::vector<std::uint8_t>;
using Parser = LengthPrefixedAccessUnitParser;

/// An H.265 stream laid out like streaming-tools' gstreamer-source:
/// prefix SEI, parameter sets on keyframes, one slice, trailing AUD.
const Bytes kHev1Stream = {
    0, 0, 0, 3, 0x4e, 0x01, 0x05,       // prefix SEI
    0, 0, 0, 3, 0x40, 0x01, 0x0c,       // VPS
    0, 0, 0, 3, 0x42, 0x01, 0x02,       // SPS
    0, 0, 0, 3, 0x44, 0x01, 0x03,       // PPS
    0, 0, 0, 4, 0x26, 0x01, 0xaf, 0x08, // IDR_W_RADL (ends at 36)
    0, 0, 0, 3, 0x46, 0x01, 0x50,       // AUD
    0, 0, 0, 3, 0x4e, 0x01, 0x05,       // prefix SEI
    0, 0, 0, 4, 0x02, 0x01, 0xd0, 0x09, // TRAIL_R
    0, 0, 0, 3, 0x46, 0x01, 0x50,       // AUD (ends at 65)
};

/// LKTS SEI as written by streaming-tools: user timestamp
/// 1'761'534'000'123'456, frame id 42.
const Bytes kLktsSeiH265 = {0x4e, 0x01, 0x05, 0x25, 0x3f, 0xa8, 0x5f, 0x64, 0x57, 0x17, 0x45, 0x62, 0xb3, 0xfc,
                            0x2c, 0x96, 0x3f, 0x66, 0xaf, 0xa6, 0xfe, 0xf7, 0xff, 0xf9, 0xbd, 0xe4, 0xe7, 0xa9,
                            0xb1, 0xbf, 0xfd, 0xfb, 0xff, 0xff, 0xff, 0xd5, 0xea, 0x4c, 0x4b, 0x54, 0x53, 0x80};

Bytes slice(const Bytes& bytes, std::size_t from, std::size_t to) {
  return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(from), bytes.begin() + static_cast<std::ptrdiff_t>(to));
}

void push(Parser& parser, const Bytes& bytes) { parser.push(bytes.data(), bytes.size()); }

Bytes lengthPrefixed(const std::vector<Bytes>& nals) {
  Bytes out;
  for (const auto& nal : nals) {
    const auto n = static_cast<std::uint32_t>(nal.size());
    out.insert(out.end(), {static_cast<std::uint8_t>(n >> 24), static_cast<std::uint8_t>(n >> 16),
                           static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n)});
    out.insert(out.end(), nal.begin(), nal.end());
  }
  return out;
}

std::vector<int> h265NalTypes(const Bytes& annex_b) {
  std::vector<int> types;
  for (std::size_t i = 0; i + 4 < annex_b.size(); ++i) {
    if (annex_b[i] == 0 && annex_b[i + 1] == 0 && annex_b[i + 2] == 0 && annex_b[i + 3] == 1) {
      types.push_back((annex_b[i + 4] >> 1) & 0x3f);
      i += 3;
    }
  }
  return types;
}

std::vector<Bytes> collect(Parser parser, const Bytes& stream, std::size_t chunk) {
  std::vector<Bytes> units;
  for (std::size_t i = 0; i < stream.size(); i += chunk) {
    push(parser, slice(stream, i, std::min(stream.size(), i + chunk)));
    while (auto frame = parser.next()) {
      units.push_back(frame->data);
    }
  }
  while (auto frame = parser.flush()) {
    units.push_back(frame->data);
  }
  return units;
}

} // namespace

TEST(LengthPrefixedAccessUnitParserTest, RejectsUnsupportedConfiguration) {
  EXPECT_THROW((void)Parser(VideoCodec::VP8), std::invalid_argument);
  Parser::Options options;
  options.nal_length_size = 5;
  EXPECT_THROW((void)Parser(VideoCodec::H265, options), std::invalid_argument);
}

TEST(LengthPrefixedAccessUnitParserTest, WaitsForNextAccessUnitWithoutSingleSliceFlush) {
  Parser parser(VideoCodec::H265);
  push(parser, slice(kHev1Stream, 0, 36));
  EXPECT_FALSE(parser.next().has_value());

  // The terminating AUD starts the next access unit, completing this one.
  push(parser, slice(kHev1Stream, 36, 43));
  const auto frame = parser.next();
  ASSERT_TRUE(frame.has_value());
  EXPECT_TRUE(frame->is_keyframe);
  EXPECT_EQ(h265NalTypes(frame->data), (std::vector<int>{39, 32, 33, 34, 19}));
}

TEST(LengthPrefixedAccessUnitParserTest, SingleSliceFlushCompletesAtTheSlice) {
  Parser::Options options;
  options.single_slice_flush = true;
  Parser parser(VideoCodec::H265, options);

  push(parser, slice(kHev1Stream, 0, 35));
  EXPECT_FALSE(parser.next().has_value());
  push(parser, slice(kHev1Stream, 35, 36));
  auto frame = parser.next();
  ASSERT_TRUE(frame.has_value());
  EXPECT_TRUE(frame->is_keyframe);
  EXPECT_EQ(frame->width, 0U);
  EXPECT_EQ(frame->timestamp_us, 0);
  EXPECT_EQ(frame->data,
            (Bytes{0,    0,    0, 1, 0x4e, 0x01, 0x05, 0,    0,    0, 1, 0x40, 0x01, 0x0c, 0,    0,    0,   1, 0x42,
                   0x01, 0x02, 0, 0, 0,    1,    0x44, 0x01, 0x03, 0, 0, 0,    1,    0x26, 0x01, 0xaf, 0x08}));

  // The trailing AUD leads the next access unit.
  push(parser, slice(kHev1Stream, 36, kHev1Stream.size()));
  frame = parser.next();
  ASSERT_TRUE(frame.has_value());
  EXPECT_FALSE(frame->is_keyframe);
  EXPECT_EQ(h265NalTypes(frame->data), (std::vector<int>{35, 39, 1}));
  EXPECT_FALSE(parser.next().has_value());
}

TEST(LengthPrefixedAccessUnitParserTest, ChunkedPushesMatchOneShotParsing) {
  for (const bool single_slice_flush : {false, true}) {
    Parser::Options options;
    options.single_slice_flush = single_slice_flush;
    const Parser parser(VideoCodec::H265, options);
    const auto baseline = collect(parser, kHev1Stream, kHev1Stream.size());
    // The trailing AUD is dropped at the end of the stream.
    EXPECT_EQ(baseline.size(), 2U);
    for (const std::size_t chunk : {1, 7}) {
      EXPECT_EQ(collect(parser, kHev1Stream, chunk), baseline);
    }
  }
}

TEST(LengthPrefixedAccessUnitParserTest, FlushDropsTrailingAud) {
  for (const bool single_slice_flush : {false, true}) {
    Parser::Options options;
    options.single_slice_flush = single_slice_flush;
    Parser parser(VideoCodec::H265, options);
    // SEI, VPS, SPS, PPS, IDR, AUD: one picture, then a lone AUD.
    push(parser, slice(kHev1Stream, 0, 43));
    const auto frame = parser.next();
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->is_keyframe);
    EXPECT_FALSE(parser.flush().has_value());
  }
}

TEST(LengthPrefixedAccessUnitParserTest, ConvertsTwoByteLengthPrefixes) {
  Parser::Options options;
  options.nal_length_size = 2;
  Parser parser(VideoCodec::H264, options);
  push(parser, Bytes{0, 4, 0x67, 1, 2, 3, 0, 3, 0x65, 0x88, 5});
  const auto frame = parser.flush();
  ASSERT_TRUE(frame.has_value());
  EXPECT_TRUE(frame->is_keyframe);
  EXPECT_EQ(frame->data, (Bytes{0, 0, 0, 1, 0x67, 1, 2, 3, 0, 0, 0, 1, 0x65, 0x88, 5}));
}

TEST(LengthPrefixedAccessUnitParserTest, ReadsFrameMetadataFromLktsSei) {
  Parser::Options options;
  options.single_slice_flush = true;
  options.frame_metadata_from_sei = true;
  Parser parser(VideoCodec::H265, options);
  push(parser, lengthPrefixed({{0x46, 0x01, 0x50}, kLktsSeiH265, {0x02, 0x01, 0xd0, 0x09}}));
  const auto frame = parser.next();
  ASSERT_TRUE(frame.has_value());
  ASSERT_TRUE(frame->metadata.has_value());
  EXPECT_EQ(frame->metadata->user_timestamp_us, 1'761'534'000'123'456ULL);
  EXPECT_EQ(frame->metadata->frame_id, 42U);
}

TEST(LengthPrefixedAccessUnitParserTest, ReadsFrameMetadataThroughEmulationPrevention) {
  // The masked timestamp 0x0000'ffff'fc00'0000 contains 00 00 03.
  const Bytes sei = {0x4e, 0x01, 0x05, 0x25, 0x3f, 0xa8, 0x5f, 0x64, 0x57, 0x17, 0x45, 0x62, 0xb3, 0xfc, 0x2c,
                     0x96, 0x3f, 0x66, 0xaf, 0xa6, 0xfe, 0xf7, 0xff, 0xff, 0x00, 0x00, 0x03, 0x03, 0xff, 0xff,
                     0xff, 0xfd, 0xfb, 0xff, 0xff, 0xff, 0xf8, 0xea, 0x4c, 0x4b, 0x54, 0x53, 0x80};
  Parser::Options options;
  options.single_slice_flush = true;
  options.frame_metadata_from_sei = true;
  Parser parser(VideoCodec::H265, options);
  push(parser, lengthPrefixed({sei, {0x02, 0x01, 0xd0, 0x09}}));
  const auto frame = parser.next();
  ASSERT_TRUE(frame.has_value());
  ASSERT_TRUE(frame->metadata.has_value());
  EXPECT_EQ(frame->metadata->user_timestamp_us, 0x0000'ffff'fc00'0000ULL);
  EXPECT_EQ(frame->metadata->frame_id, 7U);
}

TEST(LengthPrefixedAccessUnitParserTest, IgnoresSeiMetadataUnlessEnabled) {
  Parser::Options options;
  options.single_slice_flush = true;
  Parser parser(VideoCodec::H265, options);
  push(parser, lengthPrefixed({kLktsSeiH265, {0x02, 0x01, 0xd0, 0x09}}));
  const auto frame = parser.next();
  ASSERT_TRUE(frame.has_value());
  EXPECT_FALSE(frame->metadata.has_value());
}

TEST(LengthPrefixedAccessUnitParserTest, RejectsTruncatedStreamAndOversizedAccessUnit) {
  Parser parser(VideoCodec::H265);
  push(parser, Bytes{0, 0, 0, 4, 0x26, 0x01});
  EXPECT_FALSE(parser.next().has_value());
  EXPECT_THROW((void)parser.flush(), std::runtime_error);

  Parser big(VideoCodec::H264);
  const std::uint32_t len = 33U * 1024U * 1024U;
  push(big, Bytes{static_cast<std::uint8_t>(len >> 24), static_cast<std::uint8_t>(len >> 16),
                  static_cast<std::uint8_t>(len >> 8), static_cast<std::uint8_t>(len)});
  push(big, Bytes(32U * 1024U * 1024U, 0x41));
  EXPECT_THROW((void)big.next(), std::runtime_error);

  // A complete oversized access unit is rejected too, not just a pending one.
  Parser complete(VideoCodec::H264);
  Bytes idr(33U * 1024U * 1024U, 0x41);
  idr[0] = 0x65;
  idr[1] = 0x88;
  push(complete, lengthPrefixed({idr, {0x41, 0x88}}));
  EXPECT_THROW((void)complete.next(), std::runtime_error);
}

} // namespace livekit::test
