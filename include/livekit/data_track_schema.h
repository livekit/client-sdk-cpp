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

/// @brief Encoding used to interpret a data track schema definition.
///
/// Identifies the interface definition language the schema is written in (for
/// example, a `.proto` file for @ref DataTrackSchemaEncoding::Protobuf), which
/// in turn dictates the wire format of the frames the schema describes.
///
/// Almost all schemas use a well-known encoding, which converts implicitly:
/// @code
///   DataTrackSchemaEncoding encoding = DataTrackSchemaEncoding::Protobuf;
/// @endcode
/// For the uncommon case of an encoding outside the well-known set, use
/// @ref DataTrackSchemaEncoding::custom.
class DataTrackSchemaEncoding {
public:
  /// @brief Well-known schema encodings.
  enum WellKnown {
    /// @brief Protocol Buffer IDL.
    Protobuf,
    /// @brief FlatBuffer IDL.
    Flatbuffer,
    /// @brief ROS 1 Message.
    Ros1Msg,
    /// @brief ROS 2 Message.
    Ros2Msg,
    /// @brief ROS 2 IDL.
    Ros2Idl,
    /// @brief OMG IDL.
    OmgIdl,
    /// @brief JSON Schema.
    JsonSchema,
    /// @brief Another well-known encoding not known to this client version.
    Other,
  };

  /// @brief Construct a well-known encoding.
  ///
  /// The constructor is intentionally implicit for the common well-known case.
  DataTrackSchemaEncoding(WellKnown wellKnown) : well_known_(wellKnown) {}

  /// @brief Construct a custom, application-defined encoding.
  ///
  /// Prefer a well-known encoding wherever one applies. The identifier must be
  /// non-empty and no longer than 25 characters.
  ///
  /// @param identifier Custom encoding identifier.
  /// @return Custom schema encoding.
  static DataTrackSchemaEncoding custom(std::string identifier) {
    DataTrackSchemaEncoding encoding;
    encoding.custom_ = std::move(identifier);
    return encoding;
  }

  /// @brief Check whether this is a custom encoding.
  ///
  /// @return true if this is a custom encoding rather than a well-known one.
  bool isCustom() const { return !custom_.empty(); }

  /// @brief Get the well-known encoding.
  ///
  /// @return The well-known encoding. Only meaningful when @ref isCustom is false.
  WellKnown wellKnown() const { return well_known_; }

  /// @brief Get the custom identifier.
  ///
  /// @return The custom identifier. Empty when @ref isCustom is false.
  const std::string& customIdentifier() const { return custom_; }

private:
  DataTrackSchemaEncoding() = default;

  WellKnown well_known_ = Other;
  std::string custom_;
};

inline bool operator==(const DataTrackSchemaEncoding& a, const DataTrackSchemaEncoding& b) {
  if (a.isCustom() || b.isCustom()) {
    return a.customIdentifier() == b.customIdentifier();
  }
  return a.wellKnown() == b.wellKnown();
}
inline bool operator!=(const DataTrackSchemaEncoding& a, const DataTrackSchemaEncoding& b) { return !(a == b); }

/// @brief Encoding used for frames sent on a data track.
///
/// The serialization format of the frame bytes (for example,
/// @ref DataTrackFrameEncoding::Protobuf); the structure of those bytes is
/// described by a schema (see @ref DataTrackSchemaEncoding).
///
/// Almost all tracks use a well-known encoding, which converts implicitly:
/// @code
///   options.frame_encoding = DataTrackFrameEncoding::Json;
/// @endcode
/// For the uncommon case of an encoding outside the well-known set, use
/// @ref DataTrackFrameEncoding::custom.
class DataTrackFrameEncoding {
public:
  /// @brief Well-known frame encodings.
  enum WellKnown {
    /// @brief ROS 1, described by a Ros1Msg schema.
    Ros1,
    /// @brief CDR, described by a Ros2Msg, Ros2Idl, or OmgIdl schema.
    Cdr,
    /// @brief Protocol Buffer, described by a Protobuf schema.
    Protobuf,
    /// @brief FlatBuffer, described by a Flatbuffer schema.
    Flatbuffer,
    /// @brief CBOR, self-describing.
    Cbor,
    /// @brief MessagePack, self-describing.
    Msgpack,
    /// @brief JSON, self-describing or described by a JsonSchema schema.
    Json,
    /// @brief Another well-known encoding not known to this client version.
    Other,
  };

  /// @brief Construct a well-known encoding.
  ///
  /// The constructor is intentionally implicit for the common well-known case.
  DataTrackFrameEncoding(WellKnown wellKnown) : well_known_(wellKnown) {}

  /// @brief Construct a custom, application-defined encoding.
  ///
  /// Prefer a well-known encoding wherever one applies. The identifier must be
  /// non-empty and no longer than 25 characters.
  ///
  /// @param identifier Custom encoding identifier.
  /// @return Custom frame encoding.
  static DataTrackFrameEncoding custom(std::string identifier) {
    DataTrackFrameEncoding encoding;
    encoding.custom_ = std::move(identifier);
    return encoding;
  }

  /// @brief Check whether this is a custom encoding.
  ///
  /// @return true if this is a custom encoding rather than a well-known one.
  bool isCustom() const { return !custom_.empty(); }

  /// @brief Get the well-known encoding.
  ///
  /// @return The well-known encoding. Only meaningful when @ref isCustom is false.
  WellKnown wellKnown() const { return well_known_; }

  /// @brief Get the custom identifier.
  ///
  /// @return The custom identifier. Empty when @ref isCustom is false.
  const std::string& customIdentifier() const { return custom_; }

private:
  DataTrackFrameEncoding() = default;

  WellKnown well_known_ = Other;
  std::string custom_;
};

inline bool operator==(const DataTrackFrameEncoding& a, const DataTrackFrameEncoding& b) {
  if (a.isCustom() || b.isCustom()) {
    return a.customIdentifier() == b.customIdentifier();
  }
  return a.wellKnown() == b.wellKnown();
}
inline bool operator!=(const DataTrackFrameEncoding& a, const DataTrackFrameEncoding& b) { return !(a == b); }

/// @brief Uniquely identifies a data track schema.
///
/// A compound identifier with two components: a name and an encoding. Two IDs
/// are equal only if both components match; the same name with a different
/// encoding refers to a distinct schema.
struct DataTrackSchemaId {
  /// @brief Name component of the schema identifier.
  std::string name;

  /// @brief Encoding of the schema definition.
  DataTrackSchemaEncoding encoding = DataTrackSchemaEncoding::Other;
};

inline bool operator==(const DataTrackSchemaId& a, const DataTrackSchemaId& b) {
  return a.name == b.name && a.encoding == b.encoding;
}
inline bool operator!=(const DataTrackSchemaId& a, const DataTrackSchemaId& b) { return !(a == b); }

} // namespace livekit
