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

#include "data_track_proto_converter.h"

namespace livekit::test {

TEST(DataTrackSchemaEncodingTest, WellKnownValuesRoundTrip) {
  const DataTrackSchemaEncoding encoding = DataTrackSchemaEncoding::JsonSchema;

  const auto proto_encoding = toProto(encoding);
  ASSERT_EQ(proto_encoding.encoding_case(), proto::DataTrackSchemaEncoding::kWellKnown);
  EXPECT_EQ(proto_encoding.well_known(), proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_JSON_SCHEMA);
  EXPECT_EQ(fromProto(proto_encoding), encoding);
}

TEST(DataTrackSchemaEncodingTest, CustomValueRoundTrips) {
  const auto encoding = DataTrackSchemaEncoding::custom("custom-schema");

  const auto proto_encoding = toProto(encoding);
  ASSERT_EQ(proto_encoding.encoding_case(), proto::DataTrackSchemaEncoding::kCustom);
  EXPECT_EQ(proto_encoding.custom(), "custom-schema");
  EXPECT_EQ(fromProto(proto_encoding), encoding);
}

TEST(DataTrackSchemaEncodingTest, UnspecifiedAndMissingValuesMapToOther) {
  EXPECT_EQ(fromProto(proto::DataTrackSchemaEncoding{}), DataTrackSchemaEncoding::Other);

  proto::DataTrackSchemaEncoding proto_encoding;
  proto_encoding.set_well_known(proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_UNSPECIFIED);
  EXPECT_EQ(fromProto(proto_encoding), DataTrackSchemaEncoding::Other);
}

TEST(DataTrackFrameEncodingTest, WellKnownValuesRoundTrip) {
  const DataTrackFrameEncoding encoding = DataTrackFrameEncoding::Msgpack;

  const auto proto_encoding = toProto(encoding);
  ASSERT_EQ(proto_encoding.encoding_case(), proto::DataTrackFrameEncoding::kWellKnown);
  EXPECT_EQ(proto_encoding.well_known(), proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_MSGPACK);
  EXPECT_EQ(fromProto(proto_encoding), encoding);
}

TEST(DataTrackFrameEncodingTest, CustomValueRoundTrips) {
  const auto encoding = DataTrackFrameEncoding::custom("custom-frame");

  const auto proto_encoding = toProto(encoding);
  ASSERT_EQ(proto_encoding.encoding_case(), proto::DataTrackFrameEncoding::kCustom);
  EXPECT_EQ(proto_encoding.custom(), "custom-frame");
  EXPECT_EQ(fromProto(proto_encoding), encoding);
}

TEST(DataTrackFrameEncodingTest, UnspecifiedAndMissingValuesMapToOther) {
  EXPECT_EQ(fromProto(proto::DataTrackFrameEncoding{}), DataTrackFrameEncoding::Other);

  proto::DataTrackFrameEncoding proto_encoding;
  proto_encoding.set_well_known(proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_UNSPECIFIED);
  EXPECT_EQ(fromProto(proto_encoding), DataTrackFrameEncoding::Other);
}

TEST(DataTrackSchemaIdTest, RoundTripsThroughProto) {
  const DataTrackSchemaId id{"robot.telemetry", DataTrackSchemaEncoding::custom("custom-schema")};

  const auto proto_id = toProto(id);
  EXPECT_EQ(proto_id.name(), id.name);
  ASSERT_TRUE(proto_id.has_encoding());
  EXPECT_EQ(proto_id.encoding().custom(), "custom-schema");

  EXPECT_EQ(fromProto(proto_id), id);
}

TEST(DataTrackSchemaIdTest, MissingEncodingMapsToOther) {
  proto::DataTrackSchemaId proto_id;
  proto_id.set_name("robot.telemetry");

  const auto id = fromProto(proto_id);
  EXPECT_EQ(id.name, "robot.telemetry");
  EXPECT_EQ(id.encoding, DataTrackSchemaEncoding::Other);
}

TEST(DataTrackInfoTest, SchemaMetadataRoundTripsFromProto) {
  proto::DataTrackInfo proto_info;
  proto_info.set_name("telemetry");
  proto_info.set_sid("TR_data");
  proto_info.set_uses_e2ee(true);
  proto_info.mutable_schema()->set_name("robot.telemetry");
  *proto_info.mutable_schema()->mutable_encoding() = toProto(DataTrackSchemaEncoding::JsonSchema);
  *proto_info.mutable_frame_encoding() = toProto(DataTrackFrameEncoding::Json);

  const auto info = fromProto(proto_info);
  EXPECT_EQ(info.name, "telemetry");
  EXPECT_EQ(info.sid, "TR_data");
  EXPECT_TRUE(info.uses_e2ee);
  ASSERT_TRUE(info.schema.has_value());
  EXPECT_EQ(info.schema->name, "robot.telemetry");
  EXPECT_EQ(info.schema->encoding, DataTrackSchemaEncoding::JsonSchema);
  ASSERT_TRUE(info.frame_encoding.has_value());
  EXPECT_EQ(*info.frame_encoding, DataTrackFrameEncoding::Json);
}

TEST(DataTrackInfoTest, MissingSchemaMetadataStaysEmpty) {
  proto::DataTrackInfo proto_info;
  proto_info.set_name("telemetry");
  proto_info.set_sid("TR_data");
  proto_info.set_uses_e2ee(false);

  const auto info = fromProto(proto_info);
  EXPECT_FALSE(info.schema.has_value());
  EXPECT_FALSE(info.frame_encoding.has_value());
}

} // namespace livekit::test
