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

#include "data_track_proto_converter.h"

namespace livekit {

proto::DataTrackSchemaEncoding toProto(DataTrackSchemaEncoding in) {
  switch (in) {
  case DataTrackSchemaEncoding::Protobuf:
    return proto::DATA_TRACK_SCHEMA_ENCODING_PROTOBUF;
  case DataTrackSchemaEncoding::Flatbuffer:
    return proto::DATA_TRACK_SCHEMA_ENCODING_FLATBUFFER;
  case DataTrackSchemaEncoding::Ros1Msg:
    return proto::DATA_TRACK_SCHEMA_ENCODING_ROS1_MSG;
  case DataTrackSchemaEncoding::Ros2Msg:
    return proto::DATA_TRACK_SCHEMA_ENCODING_ROS2_MSG;
  case DataTrackSchemaEncoding::Ros2Idl:
    return proto::DATA_TRACK_SCHEMA_ENCODING_ROS2_IDL;
  case DataTrackSchemaEncoding::OmgIdl:
    return proto::DATA_TRACK_SCHEMA_ENCODING_OMG_IDL;
  case DataTrackSchemaEncoding::JsonSchema:
    return proto::DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA;
  case DataTrackSchemaEncoding::Other:
    return proto::DATA_TRACK_SCHEMA_ENCODING_OTHER;
  }
  return proto::DATA_TRACK_SCHEMA_ENCODING_OTHER;
}

DataTrackSchemaEncoding fromProto(proto::DataTrackSchemaEncoding in) {
  switch (in) {
  case proto::DATA_TRACK_SCHEMA_ENCODING_PROTOBUF:
    return DataTrackSchemaEncoding::Protobuf;
  case proto::DATA_TRACK_SCHEMA_ENCODING_FLATBUFFER:
    return DataTrackSchemaEncoding::Flatbuffer;
  case proto::DATA_TRACK_SCHEMA_ENCODING_ROS1_MSG:
    return DataTrackSchemaEncoding::Ros1Msg;
  case proto::DATA_TRACK_SCHEMA_ENCODING_ROS2_MSG:
    return DataTrackSchemaEncoding::Ros2Msg;
  case proto::DATA_TRACK_SCHEMA_ENCODING_ROS2_IDL:
    return DataTrackSchemaEncoding::Ros2Idl;
  case proto::DATA_TRACK_SCHEMA_ENCODING_OMG_IDL:
    return DataTrackSchemaEncoding::OmgIdl;
  case proto::DATA_TRACK_SCHEMA_ENCODING_JSON_SCHEMA:
    return DataTrackSchemaEncoding::JsonSchema;
  case proto::DATA_TRACK_SCHEMA_ENCODING_OTHER:
    return DataTrackSchemaEncoding::Other;
  }
  return DataTrackSchemaEncoding::Other;
}

proto::DataTrackSchemaId toProto(const DataTrackSchemaId& in) {
  proto::DataTrackSchemaId out;
  out.set_name(in.name);
  out.set_encoding(toProto(in.encoding));
  return out;
}

} // namespace livekit
