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

// Shared harness for the data stream conformance tests, which port the Rust
// `livekit-data-stream` crate's unit tests to end-to-end tests against a real
// SFU. Three pieces:
//
//   * FfiPeer        - drives a connected Room's local participant through the
//                      raw FFI, bypassing the C++ data stream API. It sends
//                      exactly what another FFI-based SDK (Python, Unity, ...)
//                      would: one-shot sends and stream writers go through the
//                      Rust outgoing manager (so they use data streams v2:
//                      inline / compressed packets), and raw header / chunk /
//                      trailer requests send hand-built v1 packets.
//   * RawPacketSniffer - records the raw stream packets a room receives
//                      (`stream_{header,chunk,trailer}_received` room events).
//                      The FFI forwards neither `inline_content` nor
//                      `compression` on the header, so an inline stream shows
//                      up as a header with no chunk or trailer, and compression
//                      shows up as chunk bytes that differ from the plaintext.
//   * Text/ByteStreamCollector - receives streams through the public C++ API
//                      and records content, info, and any error.
//
// Everything here uses only API that predates the FFI reader/writer port, so
// receive-side tests built on it compile both before and after that port.

#pragma once

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "data_stream.pb.h"
#include "ffi.pb.h"
#include "ffi_client.h"
#include "room.pb.h"
#include "room_test_access.h"
#include "test_common.h"

namespace livekit::test::ds {

using namespace std::chrono_literals;

/// How long to wait for something that should happen.
constexpr auto kStreamTimeout = 10s;
/// How long to wait to be confident something did NOT happen (e.g. no chunk
/// packets for an inline stream, or no stream for a non-targeted participant).
constexpr auto kQuietPeriod = 1500ms;

/// Chunk size the Rust core splits outgoing streams at (`STREAM_CHUNK_SIZE`).
constexpr std::size_t kMtu = 15'000;

inline std::string uniqueTopic(const std::string& name) {
  static std::atomic<int> counter{0};
  return "ds_conf_" + name + "_" + std::to_string(getTimestampUs()) + "_" + std::to_string(counter++);
}

inline std::string uniqueStreamId(const std::string& name) {
  static std::atomic<int> counter{0};
  return "sid_" + name + "_" + std::to_string(getTimestampUs()) + "_" + std::to_string(counter++);
}

// ---------------------------------------------------------------------------
// Payload generators
// ---------------------------------------------------------------------------

/// `n` bytes of a short repeated phrase: deflate shrinks this enormously.
inline std::string compressibleText(std::size_t n) {
  static const std::string phrase = "the quick brown fox jumps over the lazy dog. ";
  std::string out;
  out.reserve(n);
  while (out.size() < n) {
    out += phrase;
  }
  out.resize(n);
  return out;
}

/// Deterministic pseudo-random bytes (xorshift64*): deflate cannot shrink it.
inline std::string randomBytes(std::size_t n, std::uint64_t seed = 0x9E3779B97F4A7C15ULL) {
  std::string out(n, '\0');
  std::uint64_t x = seed;
  for (std::size_t i = 0; i < n; ++i) {
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    out[i] = static_cast<char>((x * 0x2545F4914F6CDD1DULL) >> 56);
  }
  return out;
}

/// Deterministic pseudo-random text over 64 ASCII symbols: valid UTF-8 that
/// deflate shrinks only modestly (6 bits of entropy per 8-bit byte).
inline std::string randomText(std::size_t n, std::uint64_t seed = 0xD1B54A32D192ED03ULL) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const std::string bytes = randomBytes(n, seed);
  std::string out(n, '\0');
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = alphabet[static_cast<unsigned char>(bytes[i]) % 64];
  }
  return out;
}

inline std::vector<std::uint8_t> toBytes(const std::string& s) { return {s.begin(), s.end()}; }
inline std::string fromBytes(const std::vector<std::uint8_t>& v) { return {v.begin(), v.end()}; }

/// True if `s` is well-formed UTF-8 (no truncated or stray sequences).
inline bool isValidUtf8(const std::string& s) {
  std::size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    std::size_t len = 0;
    if (c < 0x80) {
      len = 1;
    } else if ((c >> 5) == 0x6) {
      len = 2;
    } else if ((c >> 4) == 0xE) {
      len = 3;
    } else if ((c >> 3) == 0x1E) {
      len = 4;
    } else {
      return false;
    }
    if (i + len > s.size()) {
      return false;
    }
    for (std::size_t k = 1; k < len; ++k) {
      if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x2) {
        return false;
      }
    }
    i += len;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Rooms
// ---------------------------------------------------------------------------

/// Connects three rooms (LIVEKIT_TOKEN_A/B/C). Returns an empty vector when
/// LIVEKIT_TOKEN_C is not set so callers can skip.
inline std::vector<std::unique_ptr<Room>> threeTestRooms() {
  const char* token_c = std::getenv("LIVEKIT_TOKEN_C");
  if (token_c == nullptr || token_c[0] == '\0') {
    return {};
  }
  const auto tokens = getDataTrackTestTokens();
  const std::vector<std::string> all_tokens{tokens[0], tokens[1], token_c};
  std::vector<std::unique_ptr<Room>> rooms;
  for (std::size_t i = 0; i < all_tokens.size(); ++i) {
    auto room = std::make_unique<Room>();
    if (!room->connect(kLocalTestLiveKitUrl, all_tokens[i], RoomOptions{})) {
      throw std::runtime_error("Failed to connect test room " + std::to_string(i));
    }
    rooms.push_back(std::move(room));
  }
  waitForParticipantVisibility(rooms);
  return rooms;
}

inline std::string identityOf(const Room& room) { return lockLocalParticipant(room)->identity(); }

// ---------------------------------------------------------------------------
// Raw packet sniffer
// ---------------------------------------------------------------------------

struct RawHeader {
  std::string sender;
  std::string stream_id;
  std::string topic;
  std::string mime_type;
  std::int64_t timestamp = 0;
  std::optional<std::uint64_t> total_length;
  std::map<std::string, std::string> attributes;
  bool is_text = false;
  // Text header
  int operation_type = 0;
  int version = 0;
  std::string reply_to_stream_id;
  std::vector<std::string> attached_stream_ids;
  bool generated = false;
  // Byte header
  std::string name;
};

struct RawChunk {
  std::string sender;
  std::string stream_id;
  std::uint64_t chunk_index = 0;
  std::string content;
};

struct RawTrailer {
  std::string sender;
  std::string stream_id;
  std::string reason;
  std::map<std::string, std::string> attributes;
};

/// Records the raw data stream packets one room receives. Packets are copied
/// field by field into plain structs (never as protobuf messages: the test
/// binary links its own copy of the generated protobuf code).
class RawPacketSniffer {
public:
  explicit RawPacketSniffer(const Room& room) : state_(std::make_shared<State>()) {
    state_->room_handle = RoomTestAccess::roomHandleId(room);
    // The listener owns a reference to the state, so a callback that races
    // with destruction never touches freed memory.
    listener_id_ = FfiClient::instance().addListener(
        [state = state_](const proto::FfiEvent& event) { State::onEvent(*state, event); });
  }

  ~RawPacketSniffer() { FfiClient::instance().removeListener(listener_id_); }

  RawPacketSniffer(const RawPacketSniffer&) = delete;
  RawPacketSniffer& operator=(const RawPacketSniffer&) = delete;
  RawPacketSniffer(RawPacketSniffer&&) = delete;
  RawPacketSniffer& operator=(RawPacketSniffer&&) = delete;

  /// Waits for the first header on `topic`.
  std::optional<RawHeader> waitForHeader(const std::string& topic,
                                         std::chrono::milliseconds timeout = kStreamTimeout) const {
    std::unique_lock<std::mutex> lock(state_->mutex);
    std::optional<RawHeader> found;
    state_->cv.wait_for(lock, timeout, [&] {
      for (const auto& h : state_->headers) {
        if (h.topic == topic) {
          found = h;
          return true;
        }
      }
      return false;
    });
    return found;
  }

  /// Waits for the trailer of `stream_id`.
  std::optional<RawTrailer> waitForTrailer(const std::string& stream_id,
                                           std::chrono::milliseconds timeout = kStreamTimeout) const {
    std::unique_lock<std::mutex> lock(state_->mutex);
    std::optional<RawTrailer> found;
    state_->cv.wait_for(lock, timeout, [&] {
      for (const auto& t : state_->trailers) {
        if (t.stream_id == stream_id) {
          found = t;
          return true;
        }
      }
      return false;
    });
    return found;
  }

  /// Waits until at least `count` chunks of `stream_id` have arrived.
  bool waitForChunks(const std::string& stream_id, std::size_t count,
                     std::chrono::milliseconds timeout = kStreamTimeout) const {
    std::unique_lock<std::mutex> lock(state_->mutex);
    return state_->cv.wait_for(lock, timeout, [&] { return countChunksLocked(stream_id) >= count; });
  }

  std::vector<RawHeader> headers(const std::string& topic) const {
    const std::scoped_lock<std::mutex> lock(state_->mutex);
    std::vector<RawHeader> out;
    for (const auto& h : state_->headers) {
      if (h.topic == topic) {
        out.push_back(h);
      }
    }
    return out;
  }

  std::vector<RawChunk> chunks(const std::string& stream_id) const {
    const std::scoped_lock<std::mutex> lock(state_->mutex);
    std::vector<RawChunk> out;
    for (const auto& c : state_->chunks) {
      if (c.stream_id == stream_id) {
        out.push_back(c);
      }
    }
    return out;
  }

  std::vector<RawTrailer> trailers(const std::string& stream_id) const {
    const std::scoped_lock<std::mutex> lock(state_->mutex);
    std::vector<RawTrailer> out;
    for (const auto& t : state_->trailers) {
      if (t.stream_id == stream_id) {
        out.push_back(t);
      }
    }
    return out;
  }

  /// Concatenated raw chunk content of `stream_id`, in arrival order.
  std::string rawContent(const std::string& stream_id) const {
    std::string out;
    for (const auto& c : chunks(stream_id)) {
      out += c.content;
    }
    return out;
  }

private:
  struct State {
    std::uint64_t room_handle = 0;
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<RawHeader> headers;
    std::vector<RawChunk> chunks;
    std::vector<RawTrailer> trailers;

    static void onEvent(State& state, const proto::FfiEvent& event) {
      if (event.message_case() != proto::FfiEvent::kRoomEvent) {
        return;
      }
      const auto& re = event.room_event();
      if (re.room_handle() != state.room_handle) {
        return;
      }
      switch (re.message_case()) {
        case proto::RoomEvent::kStreamHeaderReceived: {
          const auto& ev = re.stream_header_received();
          const auto& h = ev.header();
          RawHeader out;
          out.sender = ev.participant_identity();
          out.stream_id = h.stream_id();
          out.topic = h.topic();
          out.mime_type = h.mime_type();
          out.timestamp = h.timestamp();
          if (h.has_total_length()) {
            out.total_length = h.total_length();
          }
          for (const auto& kv : h.attributes()) {
            out.attributes.emplace(kv.first, kv.second);
          }
          if (h.has_text_header()) {
            const auto& th = h.text_header();
            out.is_text = true;
            out.operation_type = static_cast<int>(th.operation_type());
            out.version = th.version();
            out.reply_to_stream_id = th.reply_to_stream_id();
            out.attached_stream_ids.assign(th.attached_stream_ids().begin(), th.attached_stream_ids().end());
            out.generated = th.generated();
          } else if (h.has_byte_header()) {
            out.name = h.byte_header().name();
          }
          push(state, [&] { state.headers.push_back(std::move(out)); });
          break;
        }
        case proto::RoomEvent::kStreamChunkReceived: {
          const auto& ev = re.stream_chunk_received();
          RawChunk out;
          out.sender = ev.participant_identity();
          out.stream_id = ev.chunk().stream_id();
          out.chunk_index = ev.chunk().chunk_index();
          out.content = ev.chunk().content();
          push(state, [&] { state.chunks.push_back(std::move(out)); });
          break;
        }
        case proto::RoomEvent::kStreamTrailerReceived: {
          const auto& ev = re.stream_trailer_received();
          RawTrailer out;
          out.sender = ev.participant_identity();
          out.stream_id = ev.trailer().stream_id();
          out.reason = ev.trailer().reason();
          for (const auto& kv : ev.trailer().attributes()) {
            out.attributes.emplace(kv.first, kv.second);
          }
          push(state, [&] { state.trailers.push_back(std::move(out)); });
          break;
        }
        default:
          break;
      }
    }

    template <typename F>
    static void push(State& state, F&& f) {
      {
        const std::scoped_lock<std::mutex> lock(state.mutex);
        f();
      }
      state.cv.notify_all();
    }
  };

  std::size_t countChunksLocked(const std::string& stream_id) const {
    std::size_t n = 0;
    for (const auto& c : state_->chunks) {
      if (c.stream_id == stream_id) {
        ++n;
      }
    }
    return n;
  }

  std::shared_ptr<State> state_;
  FfiClient::ListenerId listener_id_ = 0;
};

// ---------------------------------------------------------------------------
// FfiPeer: a remote participant driven through the raw FFI
// ---------------------------------------------------------------------------

struct PeerTextOptions {
  std::string topic;
  std::map<std::string, std::string> attributes;
  std::vector<std::string> destination_identities;
  std::optional<std::string> stream_id;
  std::optional<std::string> reply_to_stream_id;
  std::vector<std::string> attached_stream_ids;
  std::optional<int> operation_type;
  std::optional<int> version;
  std::optional<bool> generated;
  std::optional<bool> compress;
  std::optional<std::string> sender_identity;
};

struct PeerByteOptions {
  std::string topic;
  std::map<std::string, std::string> attributes;
  std::vector<std::string> destination_identities;
  std::optional<std::string> stream_id;
  std::optional<std::string> name;
  std::optional<std::string> mime_type;
  std::optional<std::uint64_t> total_length;
  std::optional<bool> compress;
  std::optional<std::string> sender_identity;
};

/// A raw v1 header. Field meaning matches `DataStream.Header` on the wire.
struct PeerRawHeader {
  std::string stream_id;
  std::string topic;
  std::string mime_type = "text/plain";
  std::optional<std::uint64_t> total_length;
  std::map<std::string, std::string> attributes;
  bool is_text = true;
  std::string name; // byte streams only
};

/// Outcome of an FFI request that completes with an async callback.
struct PeerResult {
  bool ok = false;
  std::string error;
  std::string stream_id;
  std::uint64_t writer_handle = 0;
};

class FfiPeer {
public:
  explicit FfiPeer(const Room& room)
      : participant_handle_(lockLocalParticipant(room)->ffiHandleId()), identity_(identityOf(room)) {}

  const std::string& identity() const { return identity_; }

  // ---- One-shot sends (Rust outgoing manager: v2 inline / compressed) ----

  PeerResult sendText(const std::string& text, const PeerTextOptions& opts) const {
    proto::FfiRequest req;
    auto* r = req.mutable_send_text();
    const auto id = nextAsyncId();
    r->set_local_participant_handle(participant_handle_);
    r->set_text(text);
    r->set_request_async_id(id);
    fillTextOptions(*r->mutable_options(), opts);
    return call(req, id, [](const proto::FfiEvent& e, std::uint64_t async_id) -> std::optional<PeerResult> {
      if (e.message_case() != proto::FfiEvent::kSendText || e.send_text().async_id() != async_id) {
        return std::nullopt;
      }
      PeerResult res;
      if (e.send_text().has_info()) {
        res.ok = true;
        res.stream_id = e.send_text().info().stream_id();
      } else {
        res.error = e.send_text().error().description();
      }
      return res;
    });
  }

  PeerResult sendBytes(const std::string& bytes, const PeerByteOptions& opts) const {
    proto::FfiRequest req;
    auto* r = req.mutable_send_bytes();
    const auto id = nextAsyncId();
    r->set_local_participant_handle(participant_handle_);
    r->set_bytes(bytes);
    r->set_request_async_id(id);
    fillByteOptions(*r->mutable_options(), opts);
    return call(req, id, [](const proto::FfiEvent& e, std::uint64_t async_id) -> std::optional<PeerResult> {
      if (e.message_case() != proto::FfiEvent::kSendBytes || e.send_bytes().async_id() != async_id) {
        return std::nullopt;
      }
      PeerResult res;
      if (e.send_bytes().has_info()) {
        res.ok = true;
        res.stream_id = e.send_bytes().info().stream_id();
      } else {
        res.error = e.send_bytes().error().description();
      }
      return res;
    });
  }

  // ---- Stream writers (Rust outgoing manager: chunked, never inline) ----

  PeerResult openText(const PeerTextOptions& opts) const {
    proto::FfiRequest req;
    auto* r = req.mutable_text_stream_open();
    const auto id = nextAsyncId();
    r->set_local_participant_handle(participant_handle_);
    r->set_request_async_id(id);
    fillTextOptions(*r->mutable_options(), opts);
    return call(req, id, [](const proto::FfiEvent& e, std::uint64_t async_id) -> std::optional<PeerResult> {
      if (e.message_case() != proto::FfiEvent::kTextStreamOpen || e.text_stream_open().async_id() != async_id) {
        return std::nullopt;
      }
      PeerResult res;
      if (e.text_stream_open().has_writer()) {
        res.ok = true;
        res.writer_handle = e.text_stream_open().writer().handle().id();
        res.stream_id = e.text_stream_open().writer().info().stream_id();
      } else {
        res.error = e.text_stream_open().error().description();
      }
      return res;
    });
  }

  PeerResult writeText(std::uint64_t writer_handle, const std::string& text) const {
    proto::FfiRequest req;
    auto* r = req.mutable_text_stream_write();
    const auto id = nextAsyncId();
    r->set_writer_handle(writer_handle);
    r->set_text(text);
    r->set_request_async_id(id);
    return call(req, id, [](const proto::FfiEvent& e, std::uint64_t async_id) -> std::optional<PeerResult> {
      if (e.message_case() != proto::FfiEvent::kTextStreamWriterWrite ||
          e.text_stream_writer_write().async_id() != async_id) {
        return std::nullopt;
      }
      return errorResult(e.text_stream_writer_write().has_error(), e.text_stream_writer_write().error().description());
    });
  }

  PeerResult closeText(std::uint64_t writer_handle, const std::string& reason = "",
                       const std::map<std::string, std::string>& attributes = {}) const {
    proto::FfiRequest req;
    auto* r = req.mutable_text_stream_close();
    const auto id = nextAsyncId();
    r->set_writer_handle(writer_handle);
    if (!reason.empty()) {
      r->set_reason(reason);
    }
    for (const auto& kv : attributes) {
      (*r->mutable_attributes())[kv.first] = kv.second;
    }
    r->set_request_async_id(id);
    return call(req, id, [](const proto::FfiEvent& e, std::uint64_t async_id) -> std::optional<PeerResult> {
      if (e.message_case() != proto::FfiEvent::kTextStreamWriterClose ||
          e.text_stream_writer_close().async_id() != async_id) {
        return std::nullopt;
      }
      return errorResult(e.text_stream_writer_close().has_error(), e.text_stream_writer_close().error().description());
    });
  }

  PeerResult openBytes(const PeerByteOptions& opts) const {
    proto::FfiRequest req;
    auto* r = req.mutable_byte_stream_open();
    const auto id = nextAsyncId();
    r->set_local_participant_handle(participant_handle_);
    r->set_request_async_id(id);
    fillByteOptions(*r->mutable_options(), opts);
    return call(req, id, [](const proto::FfiEvent& e, std::uint64_t async_id) -> std::optional<PeerResult> {
      if (e.message_case() != proto::FfiEvent::kByteStreamOpen || e.byte_stream_open().async_id() != async_id) {
        return std::nullopt;
      }
      PeerResult res;
      if (e.byte_stream_open().has_writer()) {
        res.ok = true;
        res.writer_handle = e.byte_stream_open().writer().handle().id();
        res.stream_id = e.byte_stream_open().writer().info().stream_id();
      } else {
        res.error = e.byte_stream_open().error().description();
      }
      return res;
    });
  }

  PeerResult writeBytes(std::uint64_t writer_handle, const std::string& bytes) const {
    proto::FfiRequest req;
    auto* r = req.mutable_byte_stream_write();
    const auto id = nextAsyncId();
    r->set_writer_handle(writer_handle);
    r->set_bytes(bytes);
    r->set_request_async_id(id);
    return call(req, id, [](const proto::FfiEvent& e, std::uint64_t async_id) -> std::optional<PeerResult> {
      if (e.message_case() != proto::FfiEvent::kByteStreamWriterWrite ||
          e.byte_stream_writer_write().async_id() != async_id) {
        return std::nullopt;
      }
      return errorResult(e.byte_stream_writer_write().has_error(), e.byte_stream_writer_write().error().description());
    });
  }

  PeerResult closeBytes(std::uint64_t writer_handle, const std::string& reason = "",
                        const std::map<std::string, std::string>& attributes = {}) const {
    proto::FfiRequest req;
    auto* r = req.mutable_byte_stream_close();
    const auto id = nextAsyncId();
    r->set_writer_handle(writer_handle);
    if (!reason.empty()) {
      r->set_reason(reason);
    }
    for (const auto& kv : attributes) {
      (*r->mutable_attributes())[kv.first] = kv.second;
    }
    r->set_request_async_id(id);
    return call(req, id, [](const proto::FfiEvent& e, std::uint64_t async_id) -> std::optional<PeerResult> {
      if (e.message_case() != proto::FfiEvent::kByteStreamWriterClose ||
          e.byte_stream_writer_close().async_id() != async_id) {
        return std::nullopt;
      }
      return errorResult(e.byte_stream_writer_close().has_error(), e.byte_stream_writer_close().error().description());
    });
  }

  // ---- Hand-built v1 packets ----

  PeerResult sendRawHeader(const PeerRawHeader& h, const std::vector<std::string>& destinations = {}) const {
    proto::FfiRequest req;
    auto* r = req.mutable_send_stream_header();
    const auto id = nextAsyncId();
    r->set_local_participant_handle(participant_handle_);
    r->set_sender_identity(identity_);
    r->set_request_async_id(id);
    for (const auto& d : destinations) {
      r->add_destination_identities(d);
    }
    auto* header = r->mutable_header();
    header->set_stream_id(h.stream_id);
    header->set_timestamp(static_cast<std::int64_t>(getTimestampUs() / 1000));
    header->set_mime_type(h.mime_type);
    header->set_topic(h.topic);
    if (h.total_length) {
      header->set_total_length(*h.total_length);
    }
    for (const auto& kv : h.attributes) {
      (*header->mutable_attributes())[kv.first] = kv.second;
    }
    if (h.is_text) {
      header->mutable_text_header()->set_operation_type(proto::DataStream::CREATE);
    } else {
      header->mutable_byte_header()->set_name(h.name);
    }
    return call(req, id, [](const proto::FfiEvent& e, std::uint64_t async_id) -> std::optional<PeerResult> {
      if (e.message_case() != proto::FfiEvent::kSendStreamHeader || e.send_stream_header().async_id() != async_id) {
        return std::nullopt;
      }
      return errorResult(e.send_stream_header().has_error(), e.send_stream_header().error());
    });
  }

  PeerResult sendRawChunk(const std::string& stream_id, std::uint64_t chunk_index, const std::string& content,
                          const std::vector<std::string>& destinations = {}) const {
    proto::FfiRequest req;
    auto* r = req.mutable_send_stream_chunk();
    const auto id = nextAsyncId();
    r->set_local_participant_handle(participant_handle_);
    r->set_sender_identity(identity_);
    r->set_request_async_id(id);
    for (const auto& d : destinations) {
      r->add_destination_identities(d);
    }
    auto* chunk = r->mutable_chunk();
    chunk->set_stream_id(stream_id);
    chunk->set_chunk_index(chunk_index);
    chunk->set_content(content);
    return call(req, id, [](const proto::FfiEvent& e, std::uint64_t async_id) -> std::optional<PeerResult> {
      if (e.message_case() != proto::FfiEvent::kSendStreamChunk || e.send_stream_chunk().async_id() != async_id) {
        return std::nullopt;
      }
      return errorResult(e.send_stream_chunk().has_error(), e.send_stream_chunk().error());
    });
  }

  PeerResult sendRawTrailer(const std::string& stream_id, const std::string& reason = "",
                            const std::map<std::string, std::string>& attributes = {},
                            const std::vector<std::string>& destinations = {}) const {
    proto::FfiRequest req;
    auto* r = req.mutable_send_stream_trailer();
    const auto id = nextAsyncId();
    r->set_local_participant_handle(participant_handle_);
    r->set_sender_identity(identity_);
    r->set_request_async_id(id);
    for (const auto& d : destinations) {
      r->add_destination_identities(d);
    }
    auto* trailer = r->mutable_trailer();
    trailer->set_stream_id(stream_id);
    trailer->set_reason(reason);
    for (const auto& kv : attributes) {
      (*trailer->mutable_attributes())[kv.first] = kv.second;
    }
    return call(req, id, [](const proto::FfiEvent& e, std::uint64_t async_id) -> std::optional<PeerResult> {
      if (e.message_case() != proto::FfiEvent::kSendStreamTrailer || e.send_stream_trailer().async_id() != async_id) {
        return std::nullopt;
      }
      return errorResult(e.send_stream_trailer().has_error(), e.send_stream_trailer().error());
    });
  }

private:
  using Extract = std::function<std::optional<PeerResult>(const proto::FfiEvent&, std::uint64_t)>;

  static std::uint64_t nextAsyncId() {
    // Far above FfiClient's own counter (which starts at 1) so ids never collide.
    static std::atomic<std::uint64_t> next{std::uint64_t{1} << 62};
    return next++;
  }

  static PeerResult errorResult(bool has_error, const std::string& error) {
    PeerResult res;
    res.ok = !has_error;
    if (has_error) {
      res.error = error;
    }
    return res;
  }

  static void fillTextOptions(proto::StreamTextOptions& o, const PeerTextOptions& opts) {
    o.set_topic(opts.topic);
    for (const auto& kv : opts.attributes) {
      (*o.mutable_attributes())[kv.first] = kv.second;
    }
    for (const auto& d : opts.destination_identities) {
      o.add_destination_identities(d);
    }
    if (opts.stream_id) {
      o.set_id(*opts.stream_id);
    }
    if (opts.reply_to_stream_id) {
      o.set_reply_to_stream_id(*opts.reply_to_stream_id);
    }
    for (const auto& a : opts.attached_stream_ids) {
      o.add_attached_stream_ids(a);
    }
    if (opts.operation_type) {
      o.set_operation_type(static_cast<proto::TextStreamInfo::OperationType>(*opts.operation_type));
    }
    if (opts.version) {
      o.set_version(*opts.version);
    }
    if (opts.generated) {
      o.set_generated(*opts.generated);
    }
    if (opts.compress) {
      o.set_compress(*opts.compress);
    }
    if (opts.sender_identity) {
      o.set_sender_identity(*opts.sender_identity);
    }
  }

  static void fillByteOptions(proto::StreamByteOptions& o, const PeerByteOptions& opts) {
    o.set_topic(opts.topic);
    for (const auto& kv : opts.attributes) {
      (*o.mutable_attributes())[kv.first] = kv.second;
    }
    for (const auto& d : opts.destination_identities) {
      o.add_destination_identities(d);
    }
    if (opts.stream_id) {
      o.set_id(*opts.stream_id);
    }
    if (opts.name) {
      o.set_name(*opts.name);
    }
    if (opts.mime_type) {
      o.set_mime_type(*opts.mime_type);
    }
    if (opts.total_length) {
      o.set_total_length(*opts.total_length);
    }
    if (opts.compress) {
      o.set_compress(*opts.compress);
    }
    if (opts.sender_identity) {
      o.set_sender_identity(*opts.sender_identity);
    }
  }

  /// Sends `req` and waits for the callback `extract` recognizes.
  static PeerResult call(const proto::FfiRequest& req, std::uint64_t async_id, const Extract& extract) {
    struct Waiter {
      std::mutex mutex;
      std::condition_variable cv;
      std::optional<PeerResult> result;
    };
    auto waiter = std::make_shared<Waiter>();
    const auto listener = FfiClient::instance().addListener([waiter, extract, async_id](const proto::FfiEvent& e) {
      auto res = extract(e, async_id);
      if (!res) {
        return;
      }
      {
        const std::scoped_lock<std::mutex> lock(waiter->mutex);
        waiter->result = std::move(res);
      }
      waiter->cv.notify_all();
    });

    PeerResult out;
    try {
      (void)FfiClient::instance().sendRequest(req);
      std::unique_lock<std::mutex> lock(waiter->mutex);
      if (waiter->cv.wait_for(lock, 30s, [&] { return waiter->result.has_value(); })) {
        out = *waiter->result;
      } else {
        out.error = "timed out waiting for the FFI callback";
      }
    } catch (const std::exception& e) {
      out.error = e.what();
    }
    FfiClient::instance().removeListener(listener);
    return out;
  }

  std::uint64_t participant_handle_;
  std::string identity_;
};

// ---------------------------------------------------------------------------
// Receive side: public C++ API
// ---------------------------------------------------------------------------

/// One stream as received through the public C++ API.
template <typename Info>
struct ReceivedStream {
  std::string sender;
  Info info;
  std::string content;
  std::size_t chunk_count = 0;
  bool errored = false;
  std::string error;
};

/// Registers a stream handler on `topic` and reads every stream that arrives
/// on its own thread. Readers that never end (e.g. a stream the SDK failed to
/// decode) leave their thread blocked; such threads are detached on
/// destruction, since the reader cannot be unblocked without API that only
/// exists after the FFI reader port.
template <typename Reader, typename Info>
class StreamCollector {
public:
  StreamCollector() : state_(std::make_shared<State>()) {}

  ~StreamCollector() {
    if (room_ != nullptr) {
      unregister(*room_, topic_);
    }
    std::vector<std::thread> threads;
    {
      const std::scoped_lock<std::mutex> lock(state_->mutex);
      threads.swap(state_->threads);
    }
    for (auto& t : threads) {
      // Give readers a moment to finish, then detach the stragglers.
      if (waitDone(t.get_id(), 2s)) {
        t.join();
      } else {
        t.detach();
      }
    }
  }

  StreamCollector(const StreamCollector&) = delete;
  StreamCollector& operator=(const StreamCollector&) = delete;
  StreamCollector(StreamCollector&&) = delete;
  StreamCollector& operator=(StreamCollector&&) = delete;

  void registerOn(Room& room, const std::string& topic) {
    room_ = &room;
    topic_ = topic;
    registerHandler(room, topic, [state = state_](std::shared_ptr<Reader> reader, const std::string& identity) {
      const std::scoped_lock<std::mutex> lock(state->mutex);
      state->threads.emplace_back([state, reader, identity] { read(*state, reader, identity); });
    });
  }

  /// Waits until `count` streams have finished (successfully or with an error).
  bool waitFor(std::size_t count, std::chrono::milliseconds timeout = kStreamTimeout) const {
    std::unique_lock<std::mutex> lock(state_->mutex);
    return state_->cv.wait_for(lock, timeout, [&] { return state_->finished.size() >= count; });
  }

  /// Waits until `count` streams have been opened (handler invoked).
  bool waitForOpened(std::size_t count, std::chrono::milliseconds timeout = kStreamTimeout) const {
    std::unique_lock<std::mutex> lock(state_->mutex);
    return state_->cv.wait_for(lock, timeout, [&] { return state_->opened >= count; });
  }

  std::size_t openedCount() const {
    const std::scoped_lock<std::mutex> lock(state_->mutex);
    return state_->opened;
  }

  std::vector<ReceivedStream<Info>> streams() const {
    const std::scoped_lock<std::mutex> lock(state_->mutex);
    return state_->finished;
  }

  /// The single finished stream; fails the test if there is not exactly one.
  ReceivedStream<Info> only() const {
    auto all = streams();
    EXPECT_EQ(all.size(), 1U);
    return all.empty() ? ReceivedStream<Info>{} : all.front();
  }

private:
  struct State {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::thread> threads;
    std::vector<std::thread::id> done_threads;
    std::vector<ReceivedStream<Info>> finished;
    std::size_t opened = 0;
  };

  static void read(State& state, const std::shared_ptr<Reader>& reader, const std::string& identity) {
    {
      const std::scoped_lock<std::mutex> lock(state.mutex);
      ++state.opened;
    }
    state.cv.notify_all();

    ReceivedStream<Info> out;
    out.sender = identity;
    try {
      Chunk chunk;
      while (reader->readNext(chunk)) {
        out.content.append(chunk.begin(), chunk.end());
        ++out.chunk_count;
      }
    } catch (const std::exception& e) {
      out.errored = true;
      out.error = e.what();
    }
    // Read info after the stream ends so trailer attributes are included.
    out.info = reader->info();
    {
      const std::scoped_lock<std::mutex> lock(state.mutex);
      state.finished.push_back(std::move(out));
      state.done_threads.push_back(std::this_thread::get_id());
    }
    state.cv.notify_all();
  }

  bool waitDone(std::thread::id id, std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(state_->mutex);
    return state_->cv.wait_for(lock, timeout, [&] {
      for (const auto& d : state_->done_threads) {
        if (d == id) {
          return true;
        }
      }
      return false;
    });
  }

  using Chunk = std::conditional_t<std::is_same_v<Reader, TextStreamReader>, std::string, std::vector<std::uint8_t>>;

  template <typename H>
  static void registerHandler(Room& room, const std::string& topic, H handler) {
    if constexpr (std::is_same_v<Reader, TextStreamReader>) {
      room.registerTextStreamHandler(topic, std::move(handler));
    } else {
      room.registerByteStreamHandler(topic, std::move(handler));
    }
  }

  static void unregister(Room& room, const std::string& topic) {
    if constexpr (std::is_same_v<Reader, TextStreamReader>) {
      room.unregisterTextStreamHandler(topic);
    } else {
      room.unregisterByteStreamHandler(topic);
    }
  }

  std::shared_ptr<State> state_;
  Room* room_ = nullptr;
  std::string topic_;
};

using TextStreamCollector = StreamCollector<TextStreamReader, TextStreamInfo>;
using ByteStreamCollector = StreamCollector<ByteStreamReader, ByteStreamInfo>;

/// Base fixture: two connected rooms (A sends, B receives) per test.
class DataStreamConformanceTest : public LiveKitTestBase {
protected:
  void SetUp() override {
    LiveKitTestBase::SetUp();
    skipIfNotConfigured();
    if (IsSkipped()) {
      return;
    }
    rooms_ = testRooms(2);
  }

  void TearDown() override {
    rooms_.clear();
    LiveKitTestBase::TearDown();
  }

  Room& senderRoom() { return *rooms_[0]; }
  Room& receiverRoom() { return *rooms_[1]; }
  std::string senderIdentity() { return identityOf(senderRoom()); }
  std::string receiverIdentity() { return identityOf(receiverRoom()); }

  std::vector<std::unique_ptr<Room>> rooms_;
};

} // namespace livekit::test::ds
