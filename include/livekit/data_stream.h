/*
 * Copyright 2025 LiveKit
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

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace livekit {

class LocalParticipant;

// Same size as Python STREAM_CHUNK_SIZE
constexpr std::size_t kStreamChunkSize = 15'000;

/// Base metadata for any stream (text or bytes).
struct BaseStreamInfo {
  /// Unique identifier for this stream.
  std::string stream_id;

  /// MIME type of the stream (e.g. "text/plain", "application/octet-stream").
  std::string mime_type;

  /// Application-defined topic name.
  std::string topic;

  /// Timestamp in milliseconds when the stream was created.
  std::int64_t timestamp = 0;

  /// Total size of the stream in bytes, if known.
  std::optional<std::size_t> size;

  /// Arbitrary key–value attributes attached to the stream.
  std::map<std::string, std::string> attributes;
};

/// Metadata for a text stream.
struct TextStreamInfo : BaseStreamInfo {
  /// IDs of any attached streams (for replies / threads).
  std::vector<std::string> attachments;
};

/// Metadata for a byte stream.
struct ByteStreamInfo : BaseStreamInfo {
  /// Optional name of the binary object (e.g. filename).
  std::string name;
};

// ---------------------------------------------------------------------
// Readers
//   - TextStreamReader: yields UTF-8 text chunks (std::string)
//   - ByteStreamReader: yields raw bytes (std::vector<uint8_t>)
// ---------------------------------------------------------------------

/// Reader for incoming text streams.
/// Created internally by the SDK when a text stream header is received.
class TextStreamReader {
public:
  /// Construct a reader from initial stream metadata.
  explicit TextStreamReader(const TextStreamInfo &info);

  TextStreamReader(const TextStreamReader &) = delete;
  TextStreamReader &operator=(const TextStreamReader &) = delete;

  /// Blocking read of next text chunk.
  /// Returns false when the stream has ended.
  bool readNext(std::string &out);

  /// Convenience: read entire stream into a single string.
  /// Blocks until the stream is closed.
  std::string readAll();

  /// Metadata associated with this stream.
  const TextStreamInfo &info() const noexcept { return info_; }

private:
  friend class Room;

  /// Called by the Room when a new chunk arrives.
  void onChunkUpdate(const std::string &text);

  /// Called by the Room when the stream is closed.
  /// Additional trailer attributes are merged into info().attributes.
  void onStreamClose(const std::map<std::string, std::string> &trailer_attrs);

  TextStreamInfo info_;

  // Queue of text chunks; empty string with closed_==true means EOS.
  std::deque<std::string> queue_;
  bool closed_ = false;

  std::mutex mutex_;
  std::condition_variable cv_;
};

/// Reader for incoming byte streams.
class ByteStreamReader {
public:
  /// Construct a reader from initial stream metadata.
  explicit ByteStreamReader(const ByteStreamInfo &info);

  ByteStreamReader(const ByteStreamReader &) = delete;
  ByteStreamReader &operator=(const ByteStreamReader &) = delete;

  /// Blocking read of next byte chunk.
  /// Returns false when the stream has ended.
  bool readNext(std::vector<std::uint8_t> &out);

  /// Metadata associated with this stream.
  const ByteStreamInfo &info() const noexcept { return info_; }

private:
  friend class Room;

  /// Called by the Room when a new chunk arrives.
  void onChunkUpdate(const std::vector<std::uint8_t> &bytes);

  /// Called by the Room when the stream is closed.
  /// Additional trailer attributes are merged into info().attributes.
  void onStreamClose(const std::map<std::string, std::string> &trailer_attrs);

  ByteStreamInfo info_;

  std::deque<std::vector<std::uint8_t>> queue_;
  bool closed_ = false;

  std::mutex mutex_;
  std::condition_variable cv_;
};

// ---------------------------------------------------------------------
// Writers (sync API mirroring Python BaseStreamWriter/TextStreamWriter)
// ---------------------------------------------------------------------

/// Base class for sending data streams.
/// Concrete subclasses are TextStreamWriter and ByteStreamWriter.
class BaseStreamWriter {
public:
  virtual ~BaseStreamWriter() = default;

  /// Stream id assigned to this writer.
  const std::string &streamId() const noexcept { return stream_id_; }

  /// Topic of this stream.
  const std::string &topic() const noexcept { return topic_; }

  /// MIME type for this stream.
  const std::string &mimeType() const noexcept { return mime_type_; }

  /// Timestamp (ms) when the stream was created.
  std::int64_t timestampMs() const noexcept { return timestamp_ms_; }

  /// Whether the stream has been closed.
  bool isClosed() const noexcept { return closed_; }

  /// Close the stream with optional reason and attributes.
  /// Throws on FFI error or if already closed.
  void close(const std::string &reason = "",
             const std::map<std::string, std::string> &attributes = {});

protected:
  BaseStreamWriter(LocalParticipant &local_participant,
                   const std::string &topic = "",
                   const std::map<std::string, std::string> &attributes = {},
                   const std::string &stream_id = "",
                   std::optional<std::size_t> total_size = std::nullopt,
                   const std::string &mime_type = "",
                   const std::vector<std::string> &destination_identities = {},
                   const std::string &sender_identity = "");

  enum class StreamKind { kUnknown, kText, kByte };

  LocalParticipant &local_participant_;

  // Public-ish metadata (mirrors BaseStreamInfo, but kept simple here)
  std::string stream_id_;
  std::string mime_type_;
  std::string topic_;
  std::int64_t timestamp_ms_ = 0;
  std::optional<std::size_t> total_size_;
  std::map<std::string, std::string> attributes_;
  std::vector<std::string> destination_identities_;
  std::string sender_identity_;

  bool closed_ = false;
  bool header_sent_ = false;
  std::uint64_t next_chunk_index_ = 0;
  StreamKind kind_ = StreamKind::kUnknown;
  std::string reply_to_id_;
  std::string byte_name_; // Used by ByteStreamWriter

  /// Ensure the header has been sent once.
  /// Throws on error.
  void ensureHeaderSent();

  /// Send a raw chunk of bytes.
  /// Throws on error or if stream is closed.
  void sendChunk(const std::vector<std::uint8_t> &content);

  /// Send the trailer with given reason and attributes.
  /// Throws on error.
  void sendTrailer(const std::string &reason,
                   const std::map<std::string, std::string> &attributes);
};

/// Writer for outgoing text streams.
class TextStreamWriter : public BaseStreamWriter {
public:
  TextStreamWriter(LocalParticipant &local_participant,
                   const std::string &topic = "",
                   const std::map<std::string, std::string> &attributes = {},
                   const std::string &stream_id = "",
                   std::optional<std::size_t> total_size = std::nullopt,
                   const std::string &reply_to_id = "",
                   const std::vector<std::string> &destination_identities = {},
                   const std::string &sender_identity = "");

  /// Write a UTF-8 string to the stream.
  /// Data will be split into chunks of at most kStreamChunkSize bytes.
  /// Throws on error or if the stream is closed.
  void write(const std::string &text);

  /// Metadata associated with this stream.
  const TextStreamInfo &info() const noexcept { return info_; }

private:
  TextStreamInfo info_;
  std::mutex write_mutex_;
};

/// Writer for outgoing byte streams.
class ByteStreamWriter : public BaseStreamWriter {
public:
  ByteStreamWriter(LocalParticipant &local_participant, const std::string &name,
                   const std::string &topic = "",
                   const std::map<std::string, std::string> &attributes = {},
                   const std::string &stream_id = "",
                   std::optional<std::size_t> total_size = std::nullopt,
                   const std::string &mime_type = "application/octet-stream",
                   const std::vector<std::string> &destination_identities = {},
                   const std::string &sender_identity = "");

  /// Write binary data to the stream.
  /// Data will be chunked into kStreamChunkSize-sized chunks.
  /// Throws on error or if the stream is closed.
  void write(const std::vector<std::uint8_t> &data);

  /// Metadata associated with this stream.
  const ByteStreamInfo &info() const noexcept { return info_; }

private:
  ByteStreamInfo info_;
  std::mutex write_mutex_;
};

/* Callback invoked when a new incoming text stream is opened.
 *
 * The TextStreamReader is provided as a shared_ptr to ensure it remains
 * alive for the duration of asynchronous reads (for example, when the
 * user spawns a background thread to consume the stream).
 */
using TextStreamHandler =
    std::function<void(std::shared_ptr<TextStreamReader>,
                       const std::string &participant_identity)>;

/* Callback invoked when a new incoming byte stream is opened.
 *
 * The ByteStreamReader is provided as a shared_ptr to ensure it remains
 * alive for the duration of asynchronous reads (for example, file writes
 * or background processing).
 */
using ByteStreamHandler =
    std::function<void(std::shared_ptr<ByteStreamReader>,
                       const std::string &participant_identity)>;

} // namespace livekit
