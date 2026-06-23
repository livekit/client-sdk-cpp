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

proto::DataTrackFrameEncoding toProto(DataTrackFrameEncoding in) {
  switch (in) {
  case DataTrackFrameEncoding::Ros1:
    return proto::DATA_TRACK_FRAME_ENCODING_ROS1;
  case DataTrackFrameEncoding::Cdr:
    return proto::DATA_TRACK_FRAME_ENCODING_CDR;
  case DataTrackFrameEncoding::Protobuf:
    return proto::DATA_TRACK_FRAME_ENCODING_PROTOBUF;
  case DataTrackFrameEncoding::Flatbuffer:
    return proto::DATA_TRACK_FRAME_ENCODING_FLATBUFFER;
  case DataTrackFrameEncoding::Cbor:
    return proto::DATA_TRACK_FRAME_ENCODING_CBOR;
  case DataTrackFrameEncoding::Msgpack:
    return proto::DATA_TRACK_FRAME_ENCODING_MSGPACK;
  case DataTrackFrameEncoding::Json:
    return proto::DATA_TRACK_FRAME_ENCODING_JSON;
  case DataTrackFrameEncoding::Other:
    return proto::DATA_TRACK_FRAME_ENCODING_OTHER;
  }
  return proto::DATA_TRACK_FRAME_ENCODING_OTHER;
}

DataTrackFrameEncoding fromProto(proto::DataTrackFrameEncoding in) {
  switch (in) {
  case proto::DATA_TRACK_FRAME_ENCODING_ROS1:
    return DataTrackFrameEncoding::Ros1;
  case proto::DATA_TRACK_FRAME_ENCODING_CDR:
    return DataTrackFrameEncoding::Cdr;
  case proto::DATA_TRACK_FRAME_ENCODING_PROTOBUF:
    return DataTrackFrameEncoding::Protobuf;
  case proto::DATA_TRACK_FRAME_ENCODING_FLATBUFFER:
    return DataTrackFrameEncoding::Flatbuffer;
  case proto::DATA_TRACK_FRAME_ENCODING_CBOR:
    return DataTrackFrameEncoding::Cbor;
  case proto::DATA_TRACK_FRAME_ENCODING_MSGPACK:
    return DataTrackFrameEncoding::Msgpack;
  case proto::DATA_TRACK_FRAME_ENCODING_JSON:
    return DataTrackFrameEncoding::Json;
  case proto::DATA_TRACK_FRAME_ENCODING_OTHER:
    return DataTrackFrameEncoding::Other;
  }
  return DataTrackFrameEncoding::Other;
}

proto::DataTrackSchemaId toProto(const DataTrackSchemaId& in) {
  proto::DataTrackSchemaId out;
  out.set_name(in.name);
  out.set_encoding(toProto(in.encoding));
  return out;
}

DataTrackSchemaId fromProto(const proto::DataTrackSchemaId& in) {
  DataTrackSchemaId out;
  out.name = in.name();
  out.encoding = fromProto(in.encoding());
  return out;
}

DataTrackInfo fromProto(const proto::DataTrackInfo& in) {
  DataTrackInfo out;
  out.name = in.name();
  out.sid = in.sid();
  out.uses_e2ee = in.uses_e2ee();
  if (in.has_schema()) {
    out.schema = fromProto(in.schema());
  }
  if (in.has_frame_encoding()) {
    out.frame_encoding = fromProto(in.frame_encoding());
  }
  return out;
}

} // namespace livekit
