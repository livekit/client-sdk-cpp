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

#include "livekit/length_prefixed_access_unit_parser.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace livekit {
namespace {

// Upper bound on bytes buffered while waiting for an access-unit boundary.
constexpr std::size_t kMaxPendingAccessUnitBytes = std::size_t{32} * 1024U * 1024U;

constexpr std::array<std::uint8_t, 4> kStartCode = {0, 0, 0, 1};

// UUID of the `user_data_unregistered` SEI carrying an LKTS packet trailer.
constexpr std::array<std::uint8_t, 16> kPacketTrailerSeiUuid = {0x3f, 0xa8, 0x5f, 0x64, 0x57, 0x17, 0x45, 0x62,
                                                                0xb3, 0xfc, 0x2c, 0x96, 0x3f, 0x66, 0xaf, 0xa6};
constexpr std::array<std::uint8_t, 4> kPacketTrailerMagic = {'L', 'K', 'T', 'S'};
constexpr std::uint8_t kTagUserTimestamp = 0x01;
constexpr std::uint8_t kTagFrameId = 0x02;

bool isH265(VideoCodec codec) { return codec == VideoCodec::H265; }

std::size_t headerLength(VideoCodec codec) { return isH265(codec) ? 2 : 1; }

std::uint8_t nalType(VideoCodec codec, const std::uint8_t* nal) {
  return isH265(codec) ? static_cast<std::uint8_t>((nal[0] >> 1) & 0x3f) : static_cast<std::uint8_t>(nal[0] & 0x1f);
}

bool isVcl(VideoCodec codec, std::uint8_t type) { return isH265(codec) ? type <= 31 : (type >= 1 && type <= 5); }

// Mirrors livekit-capture's access-unit boundary rules.
bool startsNewAccessUnit(VideoCodec codec, const std::uint8_t* nal, std::size_t size) {
  const std::uint8_t type = nalType(codec, nal);
  if (isH265(codec)) {
    // VPS(32), SPS(33), PPS(34), AUD(35), and prefix SEI(39).
    if ((type >= 32 && type <= 35) || type == 39) {
      return true;
    }
    // first_slice_segment_in_pic_flag, the bit after the 2-byte header.
    return type <= 31 && size >= 3 && (nal[2] & 0x80) != 0;
  }
  // Prefix SEI(6), SPS(7), PPS(8), and AUD(9).
  if (type >= 6 && type <= 9) {
    return true;
  }
  // first_mb_in_slice == 0: ue(v) 0 is a lone 1 bit.
  return type >= 1 && type <= 5 && size >= 2 && (nal[1] & 0x80) != 0;
}

std::vector<std::uint8_t> removeEmulationPrevention(const std::uint8_t* data, std::size_t size) {
  std::vector<std::uint8_t> rbsp;
  rbsp.reserve(size);
  int zeros = 0;
  for (std::size_t i = 0; i < size; ++i) {
    const std::uint8_t byte = data[i];
    if (zeros >= 2 && byte == 0x03) {
      zeros = 0;
      continue;
    }
    zeros = byte == 0 ? zeros + 1 : 0;
    rbsp.push_back(byte);
  }
  return rbsp;
}

std::optional<VideoFrameMetadata> parsePacketTrailer(const std::uint8_t* data, std::size_t size) {
  constexpr std::size_t kEnvelope = 1 + kPacketTrailerMagic.size();
  if (size < kEnvelope ||
      !std::equal(kPacketTrailerMagic.begin(), kPacketTrailerMagic.end(), data + size - kPacketTrailerMagic.size())) {
    return std::nullopt;
  }
  const std::size_t magic_start = size - kPacketTrailerMagic.size();
  const std::size_t trailer_len = static_cast<std::uint8_t>(data[magic_start - 1] ^ 0xff);
  if (trailer_len < kEnvelope || trailer_len > size) {
    return std::nullopt;
  }
  const std::uint8_t* tlvs = data + size - trailer_len;
  const std::size_t tlvs_len = trailer_len - kEnvelope;

  VideoFrameMetadata metadata;
  bool found = false;
  std::size_t pos = 0;
  while (pos + 2 <= tlvs_len) {
    const std::uint8_t tag = tlvs[pos] ^ 0xff;
    const std::size_t len = static_cast<std::uint8_t>(tlvs[pos + 1] ^ 0xff);
    pos += 2;
    if (pos + len > tlvs_len) {
      break;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < len && i < 8; ++i) {
      value = (value << 8) | static_cast<std::uint8_t>(tlvs[pos + i] ^ 0xff);
    }
    if (tag == kTagUserTimestamp && len == 8) {
      metadata.user_timestamp_us = value;
      found = true;
    } else if (tag == kTagFrameId && len == 4) {
      metadata.frame_id = static_cast<std::uint32_t>(value);
      found = true;
    }
    pos += len;
  }
  return found ? std::optional<VideoFrameMetadata>(metadata) : std::nullopt;
}

// Parses the SEI messages of one SEI NAL (after its header) for an LKTS
// `user_data_unregistered` payload.
std::optional<VideoFrameMetadata> frameMetadataFromSei(const std::uint8_t* ebsp, std::size_t size) {
  const std::vector<std::uint8_t> rbsp = removeEmulationPrevention(ebsp, size);
  std::size_t i = 0;
  const auto read_varlen = [&](std::size_t& out) {
    out = 0;
    while (i < rbsp.size() && rbsp[i] == 0xff) {
      out += 255;
      ++i;
    }
    if (i >= rbsp.size()) {
      return false;
    }
    out += rbsp[i++];
    return true;
  };
  // more_rbsp_data(): stop at the trailing stop bit.
  while (i + 1 < rbsp.size()) {
    std::size_t payload_type = 0;
    std::size_t payload_size = 0;
    if (!read_varlen(payload_type) || !read_varlen(payload_size) || payload_size > rbsp.size() - i) {
      return std::nullopt;
    }
    const std::uint8_t* payload = rbsp.data() + i;
    i += payload_size;
    if (payload_type == 5 && payload_size >= kPacketTrailerSeiUuid.size() &&
        std::equal(kPacketTrailerSeiUuid.begin(), kPacketTrailerSeiUuid.end(), payload)) {
      return parsePacketTrailer(payload + kPacketTrailerSeiUuid.size(), payload_size - kPacketTrailerSeiUuid.size());
    }
  }
  return std::nullopt;
}

} // namespace

LengthPrefixedAccessUnitParser::LengthPrefixedAccessUnitParser(VideoCodec codec)
    : LengthPrefixedAccessUnitParser(codec, Options{}) {}

LengthPrefixedAccessUnitParser::LengthPrefixedAccessUnitParser(VideoCodec codec, Options options)
    : codec_(codec), options_(options) {
  if (codec != VideoCodec::H264 && codec != VideoCodec::H265) {
    throw std::invalid_argument("LengthPrefixedAccessUnitParser: codec must be H.264 or H.265");
  }
  if (options.nal_length_size < 1 || options.nal_length_size > 4) {
    throw std::invalid_argument("LengthPrefixedAccessUnitParser: nal_length_size must be 1 to 4");
  }
}

void LengthPrefixedAccessUnitParser::push(const std::uint8_t* data, std::size_t size) {
  pending_.insert(pending_.end(), data, data + size);
}

void LengthPrefixedAccessUnitParser::scanPending() {
  const std::size_t prefix = options_.nal_length_size;
  while (pending_.size() - scan_cursor_ >= prefix) {
    std::size_t len = 0;
    for (std::size_t i = 0; i < prefix; ++i) {
      len = (len << 8) | pending_[scan_cursor_ + i];
    }
    if (len == 0) {
      throw std::runtime_error("LengthPrefixedAccessUnitParser: empty NAL unit");
    }
    const std::size_t start = scan_cursor_ + prefix;
    if (len > pending_.size() - start) {
      break; // NAL still streaming in
    }
    nals_.push_back({start, start + len});
    scan_cursor_ = start + len;
  }
}

std::optional<std::size_t> LengthPrefixedAccessUnitParser::splitIndex() const {
  bool seen_vcl = false;
  for (std::size_t i = 0; i < nals_.size(); ++i) {
    const std::uint8_t* nal = pending_.data() + nals_[i].start;
    const std::size_t size = nals_[i].end - nals_[i].start;
    if (size < headerLength(codec_)) {
      throw std::runtime_error("LengthPrefixedAccessUnitParser: NAL unit shorter than its header");
    }
    const bool vcl = isVcl(codec_, nalType(codec_, nal));
    if (options_.single_slice_flush) {
      // Scanned NALs are complete, so the first slice holds the picture.
      if (vcl) {
        return i + 1;
      }
      continue;
    }
    if (seen_vcl && startsNewAccessUnit(codec_, nal, size)) {
      return i;
    }
    seen_vcl = seen_vcl || vcl;
  }
  return std::nullopt;
}

EncodedVideoSource::Frame LengthPrefixedAccessUnitParser::takeAccessUnit(std::size_t nal_count) {
  const std::size_t prefix = options_.nal_length_size;
  const std::size_t au_end = nals_[nal_count - 1].end;

  EncodedVideoSource::Frame frame;
  bool has_vps = false;
  bool has_sps = false;
  bool has_pps = false;
  bool has_idr = false;
  bool seen_vcl = false;
  for (std::size_t i = 0; i < nal_count; ++i) {
    const std::uint8_t* nal = pending_.data() + nals_[i].start;
    const std::size_t size = nals_[i].end - nals_[i].start;
    const std::uint8_t type = nalType(codec_, nal);
    if (isH265(codec_)) {
      has_vps = has_vps || type == 32;
      has_sps = has_sps || type == 33;
      has_pps = has_pps || type == 34;
      has_idr = has_idr || type == 19 || type == 20;
    } else {
      has_idr = has_idr || type == 5;
    }
    if (options_.frame_metadata_from_sei && !seen_vcl && !frame.metadata && type == (isH265(codec_) ? 39 : 6)) {
      frame.metadata = frameMetadataFromSei(nal + headerLength(codec_), size - headerLength(codec_));
    }
    seen_vcl = seen_vcl || isVcl(codec_, type);
  }
  // Same rule as livekit-capture: H.265 needs parameter sets with the IDR.
  frame.is_keyframe = isH265(codec_) ? (has_vps && has_sps && has_pps && has_idr) : has_idr;

  if (prefix == kStartCode.size()) {
    // Same size as the start code: convert while copying.
    frame.data.assign(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(au_end));
    for (std::size_t i = 0; i < nal_count; ++i) {
      std::memcpy(frame.data.data() + nals_[i].start - prefix, kStartCode.data(), kStartCode.size());
    }
  } else {
    std::size_t total = 0;
    for (std::size_t i = 0; i < nal_count; ++i) {
      total += kStartCode.size() + nals_[i].end - nals_[i].start;
    }
    frame.data.reserve(total);
    for (std::size_t i = 0; i < nal_count; ++i) {
      frame.data.insert(frame.data.end(), kStartCode.begin(), kStartCode.end());
      frame.data.insert(frame.data.end(), pending_.begin() + static_cast<std::ptrdiff_t>(nals_[i].start),
                        pending_.begin() + static_cast<std::ptrdiff_t>(nals_[i].end));
    }
  }

  pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(au_end));
  nals_.erase(nals_.begin(), nals_.begin() + static_cast<std::ptrdiff_t>(nal_count));
  for (auto& nal : nals_) {
    nal.start -= au_end;
    nal.end -= au_end;
  }
  scan_cursor_ -= au_end;
  return frame;
}

std::optional<EncodedVideoSource::Frame> LengthPrefixedAccessUnitParser::next() {
  scanPending();
  if (const auto split = splitIndex(); split && *split > 0) {
    if (nals_[*split - 1].end > kMaxPendingAccessUnitBytes) {
      throw std::runtime_error("LengthPrefixedAccessUnitParser: access unit exceeds maximum buffered size");
    }
    return takeAccessUnit(*split);
  }
  if (pending_.size() > kMaxPendingAccessUnitBytes) {
    throw std::runtime_error("LengthPrefixedAccessUnitParser: access unit exceeds maximum buffered size");
  }
  return std::nullopt;
}

std::optional<EncodedVideoSource::Frame> LengthPrefixedAccessUnitParser::flush() {
  if (auto frame = next()) {
    return frame;
  }
  if (scan_cursor_ != pending_.size()) {
    throw std::runtime_error("LengthPrefixedAccessUnitParser: truncated NAL unit at end of stream");
  }
  // A tail without a slice (e.g. a trailing AUD) is not a picture.
  const bool has_vcl = std::any_of(nals_.begin(), nals_.end(), [this](const NalRange& nal) {
    return isVcl(codec_, nalType(codec_, pending_.data() + nal.start));
  });
  if (!has_vcl) {
    pending_.clear();
    nals_.clear();
    scan_cursor_ = 0;
    return std::nullopt;
  }
  return takeAccessUnit(nals_.size());
}

} // namespace livekit
