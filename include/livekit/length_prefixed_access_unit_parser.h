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

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "livekit/encoded_video_source.h"
#include "livekit/video_codec.h"
#include "livekit/visibility.h"

namespace livekit {

/// @brief Splits a length-prefixed H.264 (AVC) or H.265 (HVCC) NAL unit
/// stream into access units ready for EncodedVideoSource::captureFrame.
///
/// This is the framing of `stream-format=avc3` (H.264) and `hev1` (H.265),
/// with parameter sets in-band. Each access unit is converted to Annex-B,
/// which the pre-encoded passthrough requires.
///
/// Push bytes as they arrive (any chunking), then drain complete access
/// units with next(). Call flush() once at the end of the stream. Not
/// thread-safe.
class LIVEKIT_API LengthPrefixedAccessUnitParser final {
public:
  /// @brief Parser configuration.
  struct Options {
    /// Size of each big-endian NAL length prefix, 1 to 4 bytes
    /// (`lengthSizeMinusOne + 1` from `avcC`/`hvcC`; 4 for most encoders).
    std::uint8_t nal_length_size = 4;
    /// Complete each access unit as soon as its first slice arrives, instead
    /// of when the next access unit begins.
    ///
    /// The end of an access unit is otherwise only known from the NAL that
    /// starts the next one, which holds every picture for a full frame
    /// interval unless the encoder terminates access units with an AUD.
    /// Enable this only for streams with one slice per picture, which is
    /// what hardware encoders typically emit. NAL units that follow the
    /// slice, such as an H.265 suffix SEI, are sent with the next picture.
    bool single_slice_flush = false;
    /// Fill Frame::metadata from an LKTS `user_data_unregistered` SEI ahead
    /// of the slice (the packet-trailer convention of the LiveKit Go SDK).
    bool frame_metadata_from_sei = false;
  };

  /// @brief Creates a parser with default options.
  /// @param codec VideoCodec::H264 or VideoCodec::H265.
  /// @throws std::invalid_argument if the codec is not H.264 or H.265.
  explicit LengthPrefixedAccessUnitParser(VideoCodec codec);
  /// @brief Creates a parser.
  /// @param codec VideoCodec::H264 or VideoCodec::H265.
  /// @param options Parser configuration.
  /// @throws std::invalid_argument if the codec is not H.264 or H.265, or
  ///         nal_length_size is not 1 to 4.
  LengthPrefixedAccessUnitParser(VideoCodec codec, Options options);

  /// @brief Appends stream bytes.
  /// @param data Bytes to append.
  /// @param size Number of bytes at data.
  void push(const std::uint8_t* data, std::size_t size);

  /// @brief Returns the next complete access unit, if one is buffered.
  ///
  /// Frame::width/height are left at zero (the source resolution) and
  /// Frame::timestamp_us at zero (capture time).
  /// @return The access unit, or nullopt if none is complete yet.
  /// @throws std::runtime_error on malformed input, or when an access unit
  ///         exceeds 32 MiB.
  std::optional<EncodedVideoSource::Frame> next();

  /// @brief Returns the remaining buffered NAL units as the final access unit.
  /// Call repeatedly until it returns nullopt. A trailing group of NAL units
  /// without a slice, such as a lone AUD, is dropped.
  /// @return The next access unit, or nullopt once the stream is drained.
  /// @throws std::runtime_error on a truncated final NAL unit.
  std::optional<EncodedVideoSource::Frame> flush();

private:
  struct NalRange {
    std::size_t start;
    std::size_t end;
  };

  void scanPending();
  std::optional<std::size_t> splitIndex() const;
  EncodedVideoSource::Frame takeAccessUnit(std::size_t nal_count);

  VideoCodec codec_;
  Options options_;
  std::vector<std::uint8_t> pending_;
  // Complete NAL units of the access unit being assembled, as payload
  // ranges (after the length prefix) into pending_.
  std::vector<NalRange> nals_;
  // Offset of the first unparsed length prefix in pending_.
  std::size_t scan_cursor_ = 0;
};

} // namespace livekit
