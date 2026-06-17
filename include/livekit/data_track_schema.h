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

#pragma once

#include <string>

namespace livekit {

/**
 * Encoding used to interpret a data track schema definition.
 *
 * Identifies the interface definition language the schema is written in (e.g. a
 * `.proto` file for \ref DataTrackSchemaEncoding::Protobuf), which in turn
 * dictates the wire format of the frames the schema describes.
 */
enum class DataTrackSchemaEncoding {
  /** Protocol Buffer IDL. */
  Protobuf,
  /** FlatBuffer IDL. */
  Flatbuffer,
  /** ROS 1 Message. */
  Ros1Msg,
  /** ROS 2 Message. */
  Ros2Msg,
  /** ROS 2 IDL. */
  Ros2Idl,
  /** OMG IDL. */
  OmgIdl,
  /** JSON Schema. */
  JsonSchema,
  /** Another encoding not known to this client version. */
  Other,
};

/**
 * Uniquely identifies a data track schema.
 *
 * A compound identifier with two components: a name and an encoding. Two IDs are
 * equal only if both components match; the same name with a different encoding
 * refers to a distinct schema.
 */
struct DataTrackSchemaId {
  /** Name component of the schema identifier. */
  std::string name;

  /** Encoding of the schema definition. */
  DataTrackSchemaEncoding encoding = DataTrackSchemaEncoding::Other;
};

} // namespace livekit
