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
#include <stdexcept>
#include <string>
#include <vector>

#include "livekit/visibility.h"

namespace livekit {

class LocalParticipant;

/// Chunk size for data streams (matches Python STREAM_CHUNK_SIZE).
/// Chosen to balance throughput and latency, and to work well with WebRTC data
/// channels.
constexpr std::size_t kStreamChunkSize = 15'000; // 15 KB

/// @brief Error raised by data stream operations.
///
/// Thrown by the one-shot send APIs (for example
/// LocalParticipant::sendText()) when the send fails or the participant is no
/// longer connected, and by TextStreamReader / ByteStreamReader when an
/// incoming stream ends abnormally (for example the sender closed it with a
/// reason, the stream exceeded RoomOptions::data_stream limits, or the room
/// disconnected mid-stream). The message carries the SDK's error description.
///
/// Derives from std::runtime_error, so existing
/// `catch (const std::runtime_error&)` handlers keep working.
class LIVEKIT_API DataStreamError : public std::runtime_error {
public:
  /// @brief Construct an error with a description.
  /// @param message Human-readable error description.
  explicit DataStreamError(const std::string& message);
};

/// @brief Options for a one-shot text send or an incremental text stream.
///
/// Used by LocalParticipant::sendText().
struct StreamTextOptions {
  /// Application-defined topic. Required; receivers route streams to handlers
  /// registered with Room::registerTextStreamHandler() by topic.
  std::string topic;

  /// Arbitrary key/value attributes sent with the stream header.
  std::map<std::string, std::string> attributes;

  /// Identities of the participants that should receive the stream. Empty
  /// means every participant in the room.
  std::vector<std::string> destination_identities;

  /// Stream id to use. A unique id is generated when unset.
  std::optional<std::string> stream_id;

  /// Id of the stream this stream replies to, if any.
  std::optional<std::string> reply_to_stream_id;

  /// Ids of other streams (for example files) attached to this text stream.
  std::vector<std::string> attached_stream_ids;

  /// Compress the payload when every recipient supports compression.
  ///
  /// Only one-shot sends compress; incremental writers ignore this flag.
  bool compress = true;

  /// Identity to send as. Leave unset to send as the local participant;
  /// setting it requires the impersonation permission.
  std::optional<std::string> sender_identity;
};

/// @brief Options for a one-shot byte/file send or an incremental byte stream.
///
/// Used by LocalParticipant::sendBytes() and LocalParticipant::sendFile().
struct StreamByteOptions {
  /// Application-defined topic. Required; receivers route streams to handlers
  /// registered with Room::registerByteStreamHandler() by topic.
  std::string topic;

  /// Arbitrary key/value attributes sent with the stream header.
  std::map<std::string, std::string> attributes;

  /// Identities of the participants that should receive the stream. Empty
  /// means every participant in the room.
  std::vector<std::string> destination_identities;

  /// Stream id to use. A unique id is generated when unset.
  std::optional<std::string> stream_id;

  /// Name of the payload (for example a file name). When unset, sendBytes()
  /// uses the SDK default ("unknown") and sendFile() uses the file's basename.
  std::optional<std::string> name;

  /// MIME type of the payload. When unset, the SDK picks a default
  /// ("application/octet-stream" for bytes; derived from the file for
  /// sendFile()).
  std::optional<std::string> mime_type;

  /// Total payload size in bytes, if known. Only meaningful for incremental
  /// writers; one-shot sends always know their size.
  std::optional<std::uint64_t> total_length;

  /// Compress the payload when every recipient supports compression.
  ///
  /// Only one-shot sends compress; incremental writers ignore this flag. Turn
  /// it off for payloads that are already compressed (images, video, archives).
  bool compress = true;

  /// Identity to send as. Leave unset to send as the local participant;
  /// setting it requires the impersonation permission.
  std::optional<std::string> sender_identity;
};

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
  /// IDs of any attached streams (for attached files).
  std::vector<std::string> attachments;
  /// If this stream is a reply to another stream, this field holds its ID
  std::optional<std::string> reply_to_stream_id;
};

/// Metadata for a byte stream.
struct ByteStreamInfo : BaseStreamInfo {
  /// Optional name of the binary object (e.g. filename).
  std::string name;
};

// Readers
//   - TextStreamReader: yields UTF-8 text chunks (std::string)
//   - ByteStreamReader: yields raw bytes (std::vector<uint8_t>)

/// @brief Reader for incoming text streams.
///
/// Created by the SDK when a remote participant opens a text stream on a topic
/// with a handler registered via Room::registerTextStreamHandler(). The SDK
/// decompresses and reassembles the stream; each chunk is valid UTF-8.
///
/// Reading blocks the calling thread. Stream handlers run on the SDK's event
/// thread, which also delivers the chunks, so never read from inside the
/// handler: move the reader to another thread first.
class LIVEKIT_API TextStreamReader {
public:
  /// @brief Construct a detached reader from stream metadata.
  ///
  /// A reader built this way is not connected to any incoming stream and
  /// never receives data. Readers for real streams are created by the SDK.
  /// @param info Stream metadata.
  explicit TextStreamReader(TextStreamInfo info);
  ~TextStreamReader();

  TextStreamReader(const TextStreamReader&) = delete;
  TextStreamReader& operator=(const TextStreamReader&) = delete;

  /// @brief Blocking read of the next text chunk.
  ///
  /// When the stream ends normally, any final attributes the sender attached
  /// on close are merged into info().attributes before this returns false.
  ///
  /// @param out Receives the next chunk.
  /// @return True if a chunk was read; false when the stream has ended or
  ///         close() was called.
  /// @throws DataStreamError if the stream ended abnormally (for example the
  ///         sender closed it with a reason, it exceeded the receive size
  ///         limit, or the room disconnected mid-stream). Chunks received
  ///         before the error are returned first.
  bool readNext(std::string& out);

  /// @brief Read the entire stream into a single string.
  ///
  /// Blocks until the stream ends.
  ///
  /// @return The concatenated text.
  /// @throws DataStreamError if the stream ended abnormally.
  std::string readAll();

  /// @brief Stop reading early.
  ///
  /// Discards buffered chunks and wakes any thread blocked in readNext() or
  /// readAll(), which then return the end of the stream. Idempotent.
  void close();

  /// @brief Metadata associated with this stream.
  ///
  /// Attributes the sender attached when closing the stream are merged in
  /// once readNext() or readAll() has reached the end of the stream, on the
  /// reading thread. Don't call this concurrently with readNext() from
  /// another thread.
  ///
  /// @return Stream metadata.
  const TextStreamInfo& info() const noexcept { return info_; }

private:
  friend class Room;
  friend struct DataStreamReaderTestAccess;
  struct Impl;

  /// Called by the Room when a chunk arrives.
  void onChunkReceived(std::string text);

  /// Called by the Room when the stream ends. @p error is set when the stream
  /// ended abnormally.
  void onStreamEnded(const std::map<std::string, std::string>& attributes, std::optional<std::string> error);

  TextStreamInfo info_;
  std::unique_ptr<Impl> impl_;
};

/// @brief Reader for incoming byte streams.
///
/// Created by the SDK when a remote participant opens a byte stream on a topic
/// with a handler registered via Room::registerByteStreamHandler(). The SDK
/// decompresses and reassembles the stream.
///
/// Reading blocks the calling thread. Stream handlers run on the SDK's event
/// thread, which also delivers the chunks, so never read from inside the
/// handler: move the reader to another thread first.
class LIVEKIT_API ByteStreamReader {
public:
  /// @brief Construct a detached reader from stream metadata.
  ///
  /// A reader built this way is not connected to any incoming stream and
  /// never receives data. Readers for real streams are created by the SDK.
  /// @param info Stream metadata.
  explicit ByteStreamReader(ByteStreamInfo info);
  ~ByteStreamReader();

  ByteStreamReader(const ByteStreamReader&) = delete;
  ByteStreamReader& operator=(const ByteStreamReader&) = delete;

  /// @brief Blocking read of the next byte chunk.
  ///
  /// When the stream ends normally, any final attributes the sender attached
  /// on close are merged into info().attributes before this returns false.
  ///
  /// @param out Receives the next chunk.
  /// @return True if a chunk was read; false when the stream has ended or
  ///         close() was called.
  /// @throws DataStreamError if the stream ended abnormally (for example the
  ///         sender closed it with a reason, it exceeded the receive size
  ///         limit, or the room disconnected mid-stream). Chunks received
  ///         before the error are returned first.
  bool readNext(std::vector<std::uint8_t>& out);

  /// @brief Read the entire stream into a single buffer.
  ///
  /// Blocks until the stream ends.
  ///
  /// @return The concatenated bytes.
  /// @throws DataStreamError if the stream ended abnormally.
  std::vector<std::uint8_t> readAll();

  /// @brief Stop reading early.
  ///
  /// Discards buffered chunks and wakes any thread blocked in readNext() or
  /// readAll(), which then return the end of the stream. Idempotent.
  void close();

  /// @brief Metadata associated with this stream.
  ///
  /// Attributes the sender attached when closing the stream are merged in
  /// once readNext() or readAll() has reached the end of the stream, on the
  /// reading thread. Don't call this concurrently with readNext() from
  /// another thread.
  ///
  /// @return Stream metadata.
  const ByteStreamInfo& info() const noexcept { return info_; }

private:
  friend class Room;
  friend struct DataStreamReaderTestAccess;
  struct Impl;

  /// Called by the Room when a chunk arrives.
  void onChunkReceived(std::vector<std::uint8_t> bytes);

  /// Called by the Room when the stream ends. @p error is set when the stream
  /// ended abnormally.
  void onStreamEnded(const std::map<std::string, std::string>& attributes, std::optional<std::string> error);

  ByteStreamInfo info_;
  std::unique_ptr<Impl> impl_;
};

/// Base class for sending data streams.
/// Concrete subclasses are TextStreamWriter and ByteStreamWriter.
class LIVEKIT_API BaseStreamWriter {
public:
  virtual ~BaseStreamWriter() = default;

  /// Stream id assigned to this writer.
  const std::string& streamId() const noexcept { return stream_id_; }

  /// Topic of this stream.
  const std::string& topic() const noexcept { return topic_; }

  /// MIME type for this stream.
  const std::string& mimeType() const noexcept { return mime_type_; }

  /// Timestamp (ms) when the stream was created.
  std::int64_t timestampMs() const noexcept { return timestamp_ms_; }

  /// Whether the stream has been closed.
  bool isClosed() const noexcept { return closed_; }

  /// Close the stream with optional reason and attributes.
  /// Throws on FFI error or if already closed.
  void close(const std::string& reason = "", const std::map<std::string, std::string>& attributes = {});

protected:
  BaseStreamWriter(LocalParticipant& local_participant, std::string topic = "",
                   std::map<std::string, std::string> attributes = {}, std::string stream_id = "",
                   std::optional<std::size_t> total_size = std::nullopt, std::string mime_type = "",
                   std::vector<std::string> destination_identities = {}, std::string sender_identity = "");

  enum class StreamKind { kUnknown, kText, kByte };

  LocalParticipant& local_participant_;

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
  void sendChunk(const std::vector<std::uint8_t>& content);

  /// Send the trailer with given reason and attributes.
  /// Throws on error.
  void sendTrailer(const std::string& reason, const std::map<std::string, std::string>& attributes);
};

/// Writer for outgoing text streams.
class LIVEKIT_API TextStreamWriter : public BaseStreamWriter {
public:
  TextStreamWriter(LocalParticipant& local_participant, const std::string& topic = "",
                   const std::map<std::string, std::string>& attributes = {}, const std::string& stream_id = "",
                   std::optional<std::size_t> total_size = std::nullopt, const std::string& reply_to_id = "",
                   const std::vector<std::string>& destination_identities = {},
                   const std::string& sender_identity = "");

  /// Write a UTF-8 string to the stream.
  /// Data will be split into chunks of at most kStreamChunkSize bytes.
  /// Throws on error or if the stream is closed.
  void write(const std::string& text);

  /// Metadata associated with this stream.
  const TextStreamInfo& info() const noexcept { return info_; }

private:
  TextStreamInfo info_;
  std::mutex write_mutex_;
};

/// Writer for outgoing byte streams.
class LIVEKIT_API ByteStreamWriter : public BaseStreamWriter {
public:
  ByteStreamWriter(LocalParticipant& local_participant, const std::string& name, const std::string& topic = "",
                   const std::map<std::string, std::string>& attributes = {}, const std::string& stream_id = "",
                   std::optional<std::size_t> total_size = std::nullopt,
                   const std::string& mime_type = "application/octet-stream",
                   const std::vector<std::string>& destination_identities = {},
                   const std::string& sender_identity = "");

  /// Write binary data to the stream.
  /// Data will be chunked into kStreamChunkSize-sized chunks.
  /// Throws on error or if the stream is closed.
  void write(const std::vector<std::uint8_t>& data);

  /// Metadata associated with this stream.
  const ByteStreamInfo& info() const noexcept { return info_; }

private:
  ByteStreamInfo info_;
  std::mutex write_mutex_;
};

/// Callback invoked when a new incoming text stream is opened.
///
/// The TextStreamReader is provided as a shared_ptr to ensure it remains
/// alive for the duration of asynchronous reads (for example, when the
/// user spawns a background thread to consume the stream). The handler runs on
/// the SDK event thread and must not read from the stream itself.
using TextStreamHandler =
    std::function<void(std::shared_ptr<TextStreamReader>, const std::string& participant_identity)>;

/// Callback invoked when a new incoming byte stream is opened.
///
/// The ByteStreamReader is provided as a shared_ptr to ensure it remains
/// alive for the duration of asynchronous reads (for example, file writes
/// or background processing). The handler runs on the SDK event thread and
/// must not read from the stream itself.
using ByteStreamHandler =
    std::function<void(std::shared_ptr<ByteStreamReader>, const std::string& participant_identity)>;

} // namespace livekit
