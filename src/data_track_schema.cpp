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

proto::DataTrackSchemaEncoding toProto(const DataTrackSchemaEncoding& in) {
  proto::DataTrackSchemaEncoding out;
  if (in.isCustom()) {
    out.set_custom(in.customIdentifier());
    return out;
  }
  switch (in.wellKnown()) {
    case DataTrackSchemaEncoding::Protobuf:
      out.set_well_known(proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_PROTOBUF);
      break;
    case DataTrackSchemaEncoding::Flatbuffer:
      out.set_well_known(proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_FLATBUFFER);
      break;
    case DataTrackSchemaEncoding::Ros1Msg:
      out.set_well_known(proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_ROS1_MSG);
      break;
    case DataTrackSchemaEncoding::Ros2Msg:
      out.set_well_known(proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_ROS2_MSG);
      break;
    case DataTrackSchemaEncoding::Ros2Idl:
      out.set_well_known(proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_ROS2_IDL);
      break;
    case DataTrackSchemaEncoding::OmgIdl:
      out.set_well_known(proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_OMG_IDL);
      break;
    case DataTrackSchemaEncoding::JsonSchema:
      out.set_well_known(proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_JSON_SCHEMA);
      break;
    case DataTrackSchemaEncoding::Other:
      out.set_well_known(proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_UNSPECIFIED);
      break;
  }
  return out;
}

DataTrackSchemaEncoding fromProto(const proto::DataTrackSchemaEncoding& in) {
  if (in.encoding_case() == proto::DataTrackSchemaEncoding::kCustom) {
    return DataTrackSchemaEncoding::custom(in.custom());
  }
  switch (in.well_known()) {
    case proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_PROTOBUF:
      return DataTrackSchemaEncoding::Protobuf;
    case proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_FLATBUFFER:
      return DataTrackSchemaEncoding::Flatbuffer;
    case proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_ROS1_MSG:
      return DataTrackSchemaEncoding::Ros1Msg;
    case proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_ROS2_MSG:
      return DataTrackSchemaEncoding::Ros2Msg;
    case proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_ROS2_IDL:
      return DataTrackSchemaEncoding::Ros2Idl;
    case proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_OMG_IDL:
      return DataTrackSchemaEncoding::OmgIdl;
    case proto::DataTrackSchemaEncoding::WELL_KNOWN_SCHEMA_ENCODING_JSON_SCHEMA:
      return DataTrackSchemaEncoding::JsonSchema;
    default:
      return DataTrackSchemaEncoding::Other;
  }
}

proto::DataTrackFrameEncoding toProto(const DataTrackFrameEncoding& in) {
  proto::DataTrackFrameEncoding out;
  if (in.isCustom()) {
    out.set_custom(in.customIdentifier());
    return out;
  }
  switch (in.wellKnown()) {
    case DataTrackFrameEncoding::Ros1:
      out.set_well_known(proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_ROS1);
      break;
    case DataTrackFrameEncoding::Cdr:
      out.set_well_known(proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_CDR);
      break;
    case DataTrackFrameEncoding::Protobuf:
      out.set_well_known(proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_PROTOBUF);
      break;
    case DataTrackFrameEncoding::Flatbuffer:
      out.set_well_known(proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_FLATBUFFER);
      break;
    case DataTrackFrameEncoding::Cbor:
      out.set_well_known(proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_CBOR);
      break;
    case DataTrackFrameEncoding::Msgpack:
      out.set_well_known(proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_MSGPACK);
      break;
    case DataTrackFrameEncoding::Json:
      out.set_well_known(proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_JSON);
      break;
    case DataTrackFrameEncoding::Other:
      out.set_well_known(proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_UNSPECIFIED);
      break;
  }
  return out;
}

DataTrackFrameEncoding fromProto(const proto::DataTrackFrameEncoding& in) {
  if (in.encoding_case() == proto::DataTrackFrameEncoding::kCustom) {
    return DataTrackFrameEncoding::custom(in.custom());
  }
  switch (in.well_known()) {
    case proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_ROS1:
      return DataTrackFrameEncoding::Ros1;
    case proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_CDR:
      return DataTrackFrameEncoding::Cdr;
    case proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_PROTOBUF:
      return DataTrackFrameEncoding::Protobuf;
    case proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_FLATBUFFER:
      return DataTrackFrameEncoding::Flatbuffer;
    case proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_CBOR:
      return DataTrackFrameEncoding::Cbor;
    case proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_MSGPACK:
      return DataTrackFrameEncoding::Msgpack;
    case proto::DataTrackFrameEncoding::WELL_KNOWN_FRAME_ENCODING_JSON:
      return DataTrackFrameEncoding::Json;
    default:
      return DataTrackFrameEncoding::Other;
  }
}

proto::DataTrackSchemaId toProto(const DataTrackSchemaId& in) {
  proto::DataTrackSchemaId out;
  out.set_name(in.name);
  *out.mutable_encoding() = toProto(in.encoding);
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
