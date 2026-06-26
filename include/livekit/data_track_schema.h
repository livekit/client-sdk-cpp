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
#include <utility>

namespace livekit {

/**
 * Encoding used to interpret a data track schema definition.
 *
 * Identifies the interface definition language the schema is written in (e.g. a
 * `.proto` file for \ref DataTrackSchemaEncoding::Protobuf), which in turn
 * dictates the wire format of the frames the schema describes.
 *
 * Almost all schemas use a well-known encoding, which converts implicitly:
 * \code
 *   DataTrackSchemaEncoding encoding = DataTrackSchemaEncoding::Protobuf;
 * \endcode
 * For the uncommon case of an encoding outside the well-known set, use
 * \ref DataTrackSchemaEncoding::Custom.
 */
class DataTrackSchemaEncoding {
public:
  /** Well-known schema encodings. */
  enum WellKnown {
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
    /** Another well-known encoding not known to this client version. */
    Other,
  };

  /** Constructs a well-known encoding. Implicit by design, for the common case. */
  DataTrackSchemaEncoding(WellKnown well_known) : well_known_(well_known) {}

  /**
   * Constructs a custom, application-defined encoding.
   *
   * Prefer a well-known encoding wherever one applies. The identifier must be
   * non-empty and no longer than 25 characters.
   */
  static DataTrackSchemaEncoding Custom(std::string identifier) {
    DataTrackSchemaEncoding encoding;
    encoding.custom_ = std::move(identifier);
    return encoding;
  }

  /** Whether this is a custom encoding rather than a well-known one. */
  bool is_custom() const { return !custom_.empty(); }

  /** The well-known encoding. Only meaningful when \ref is_custom is false. */
  WellKnown well_known() const { return well_known_; }

  /** The custom identifier. Empty when \ref is_custom is false. */
  const std::string& custom_identifier() const { return custom_; }

private:
  DataTrackSchemaEncoding() = default;

  WellKnown well_known_ = Other;
  std::string custom_;
};

inline bool operator==(const DataTrackSchemaEncoding& a, const DataTrackSchemaEncoding& b) {
  if (a.is_custom() || b.is_custom()) {
    return a.custom_identifier() == b.custom_identifier();
  }
  return a.well_known() == b.well_known();
}
inline bool operator!=(const DataTrackSchemaEncoding& a, const DataTrackSchemaEncoding& b) { return !(a == b); }

/**
 * Encoding used for frames sent on a data track.
 *
 * The serialization format of the frame bytes (e.g.
 * \ref DataTrackFrameEncoding::Protobuf); the structure of those bytes is
 * described by a schema (see \ref DataTrackSchemaEncoding).
 *
 * Almost all tracks use a well-known encoding, which converts implicitly:
 * \code
 *   options.frame_encoding = DataTrackFrameEncoding::Json;
 * \endcode
 * For the uncommon case of an encoding outside the well-known set, use
 * \ref DataTrackFrameEncoding::Custom.
 */
class DataTrackFrameEncoding {
public:
  /** Well-known frame encodings. */
  enum WellKnown {
    /** ROS 1, described by a Ros1Msg schema. */
    Ros1,
    /** CDR, described by a Ros2Msg, Ros2Idl, or OmgIdl schema. */
    Cdr,
    /** Protocol Buffer, described by a Protobuf schema. */
    Protobuf,
    /** FlatBuffer, described by a Flatbuffer schema. */
    Flatbuffer,
    /** CBOR, self-describing. */
    Cbor,
    /** MessagePack, self-describing. */
    Msgpack,
    /** JSON, self-describing or described by a JsonSchema schema. */
    Json,
    /** Another well-known encoding not known to this client version. */
    Other,
  };

  /** Constructs a well-known encoding. Implicit by design, for the common case. */
  DataTrackFrameEncoding(WellKnown well_known) : well_known_(well_known) {}

  /**
   * Constructs a custom, application-defined encoding.
   *
   * Prefer a well-known encoding wherever one applies. The identifier must be
   * non-empty and no longer than 25 characters.
   */
  static DataTrackFrameEncoding Custom(std::string identifier) {
    DataTrackFrameEncoding encoding;
    encoding.custom_ = std::move(identifier);
    return encoding;
  }

  /** Whether this is a custom encoding rather than a well-known one. */
  bool is_custom() const { return !custom_.empty(); }

  /** The well-known encoding. Only meaningful when \ref is_custom is false. */
  WellKnown well_known() const { return well_known_; }

  /** The custom identifier. Empty when \ref is_custom is false. */
  const std::string& custom_identifier() const { return custom_; }

private:
  DataTrackFrameEncoding() = default;

  WellKnown well_known_ = Other;
  std::string custom_;
};

inline bool operator==(const DataTrackFrameEncoding& a, const DataTrackFrameEncoding& b) {
  if (a.is_custom() || b.is_custom()) {
    return a.custom_identifier() == b.custom_identifier();
  }
  return a.well_known() == b.well_known();
}
inline bool operator!=(const DataTrackFrameEncoding& a, const DataTrackFrameEncoding& b) { return !(a == b); }

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
