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

#include <atomic>
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

namespace detail {
class StreamWriterRegistry;
} // namespace detail

/// Chunk size for data streams (matches Python STREAM_CHUNK_SIZE).
///
/// @deprecated The SDK core now chunks data streams itself (at 15 KB); this
/// constant no longer controls anything.
[[deprecated("kStreamChunkSize no longer controls chunking; the SDK core chunks data streams itself")]]
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

/// @brief Base class for sending data streams incrementally.
///
/// Concrete subclasses are TextStreamWriter and ByteStreamWriter; create them
/// with LocalParticipant::streamText() / LocalParticipant::streamBytes().
///
/// Thread safety: write() and close() are serialized internally, and each one
/// returns only after the SDK has processed it, so a close() always follows
/// every write() that returned before it. When the room disconnects, the
/// writer is closed and later write() / close() calls throw DataStreamError.
///
/// Destroying a writer that was opened but not closed closes the stream
/// normally (without waiting), so receivers don't wait forever.
class LIVEKIT_API BaseStreamWriter {
public:
  virtual ~BaseStreamWriter();

  BaseStreamWriter(const BaseStreamWriter&) = delete;
  BaseStreamWriter& operator=(const BaseStreamWriter&) = delete;

  /// @brief Stream id assigned to this writer.
  /// @return The stream id.
  const std::string& streamId() const noexcept { return stream_id_; }

  /// @brief Topic of this stream.
  /// @return The topic.
  const std::string& topic() const noexcept { return topic_; }

  /// @brief MIME type for this stream.
  /// @return The MIME type.
  const std::string& mimeType() const noexcept { return mime_type_; }

  /// @brief Timestamp (ms) when the stream was created.
  ///
  /// Updated to the SDK core's timestamp once the stream is opened.
  /// @return Creation timestamp in milliseconds since the Unix epoch.
  std::int64_t timestampMs() const noexcept { return timestamp_ms_; }

  /// @brief Whether the stream has been closed (explicitly or because the room
  /// disconnected).
  /// @return True if closed.
  bool isClosed() const noexcept { return closed_; }

  /// @brief Close the stream.
  ///
  /// Waits for any in-progress write() to finish first. A non-empty @p reason
  /// closes the stream abnormally: receivers' reads throw DataStreamError
  /// carrying the reason.
  ///
  /// @param reason     Empty for a normal close; otherwise the error reason.
  /// @param attributes Attributes merged into the receiver's stream info.
  /// @throws DataStreamError if the stream is already closed, the room has
  ///         disconnected, or the SDK reports an error.
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

  std::atomic<bool> closed_{false};
  /// True once the stream has been opened with the SDK core.
  bool header_sent_ = false;
  /// Unused; chunking is done by the SDK core.
  std::uint64_t next_chunk_index_ = 0;
  StreamKind kind_ = StreamKind::kUnknown;
  std::string reply_to_id_;
  std::string byte_name_; // Used by ByteStreamWriter

  /// @brief Open the stream if it isn't open yet.
  /// @deprecated The SDK opens the stream on the first write() or close().
  /// @throws DataStreamError on error.
  [[deprecated("BaseStreamWriter::ensureHeaderSent is deprecated; the stream opens on the first write or close")]]
  void ensureHeaderSent();

  /// @brief Write raw bytes to the stream (UTF-8 text for a text stream).
  /// @deprecated Use TextStreamWriter::write() or ByteStreamWriter::write() instead.
  /// @param content Payload to write.
  /// @throws std::runtime_error if the stream is closed; DataStreamError on error.
  [[deprecated(
      "BaseStreamWriter::sendChunk is deprecated; use TextStreamWriter::write or ByteStreamWriter::write "
      "instead")]]
  void sendChunk(const std::vector<std::uint8_t>& content);

  /// @brief Close the stream with the given reason and attributes.
  /// @deprecated Use close() instead.
  /// @param reason     Empty for a normal close; otherwise the error reason.
  /// @param attributes Attributes merged into the receiver's stream info.
  /// @throws DataStreamError on error.
  [[deprecated("BaseStreamWriter::sendTrailer is deprecated; use BaseStreamWriter::close instead")]]
  void sendTrailer(const std::string& reason, const std::map<std::string, std::string>& attributes);

  /// @brief Open the stream now (requires @c kind_ to be set). Serialized with
  /// write() and close().
  /// @throws DataStreamError on error.
  void open();

  /// @brief Write a payload (text for kText, bytes for kByte), opening first if
  /// needed. Serialized with other operations.
  /// @param data Payload bytes.
  /// @param size Payload size.
  /// @param closed_message Message thrown if the writer is already closed.
  /// @throws DataStreamError on error.
  void writePayload(const char* data, std::size_t size, const char* closed_message);

private:
  friend class detail::StreamWriterRegistry;

  void openLocked();
  void closeLocked(const std::string& reason, const std::map<std::string, std::string>& attributes);
  void applyOpenedInfo(std::int64_t timestamp_ms, const std::string& mime_type);
  std::uint64_t takeHandle() noexcept;
  void onParticipantDisconnected() noexcept;
  [[noreturn]] void throwNotConnected() const;

  /// Serializes open/write/close. Held across FFI waits.
  std::mutex op_mutex_;
  /// Guards writer_handle_ and participant_gone_. Never held across FFI calls.
  mutable std::mutex state_mutex_;
  /// Owned FFI writer handle; 0 before open and after close consumes it.
  std::uint64_t writer_handle_ = 0;
  bool participant_gone_ = false;
  std::shared_ptr<detail::StreamWriterRegistry> registry_;
};

/// @brief Writer for outgoing text streams.
///
/// Create one with LocalParticipant::streamText(). Text is sent as UTF-8; the
/// SDK splits it into chunks on code point boundaries.
class LIVEKIT_API TextStreamWriter : public BaseStreamWriter {
public:
  /// @brief Construct a text stream writer.
  ///
  /// The stream is opened lazily on the first write() or close().
  ///
  /// @deprecated Use LocalParticipant::streamText() instead.
  /// @param local_participant      Participant that sends the stream; must outlive the writer.
  /// @param topic                  Topic receivers register handlers on.
  /// @param attributes             Attributes sent with the stream header.
  /// @param stream_id              Stream id; generated when empty.
  /// @param total_size             Recorded locally in info() only; text streams can't transmit a size.
  /// @param reply_to_id            Id of the stream this one replies to, or empty.
  /// @param destination_identities Recipients; empty for everyone.
  /// @param sender_identity        Identity to send as; defaults to the local participant.
  [[deprecated("Constructing TextStreamWriter directly is deprecated; use LocalParticipant::streamText instead")]]
  TextStreamWriter(LocalParticipant& local_participant, const std::string& topic = "",
                   const std::map<std::string, std::string>& attributes = {}, const std::string& stream_id = "",
                   std::optional<std::size_t> total_size = std::nullopt, const std::string& reply_to_id = "",
                   const std::vector<std::string>& destination_identities = {},
                   const std::string& sender_identity = "");

  /// @brief Write a UTF-8 string to the stream.
  ///
  /// Blocks until the SDK has sent it.
  ///
  /// @param text UTF-8 text to append.
  /// @throws DataStreamError if the stream is closed, the room has
  ///         disconnected, or the SDK reports an error.
  void write(const std::string& text);

  /// @brief Metadata associated with this stream.
  /// @return Stream metadata; updated with the SDK core's info once opened.
  const TextStreamInfo& info() const noexcept { return info_; }

private:
  friend class BaseStreamWriter;
  friend class LocalParticipant;
  struct FactoryTag {};

  TextStreamWriter(FactoryTag, LocalParticipant& local_participant, const StreamTextOptions& options);

  TextStreamInfo info_;
  std::vector<std::string> attached_stream_ids_;
};

/// @brief Writer for outgoing byte streams.
///
/// Create one with LocalParticipant::streamBytes().
class LIVEKIT_API ByteStreamWriter : public BaseStreamWriter {
public:
  /// @brief Construct a byte stream writer.
  ///
  /// The stream is opened lazily on the first write() or close().
  ///
  /// @deprecated Use LocalParticipant::streamBytes() instead.
  /// @param local_participant      Participant that sends the stream; must outlive the writer.
  /// @param name                   Name of the payload (for example a file name).
  /// @param topic                  Topic receivers register handlers on.
  /// @param attributes             Attributes sent with the stream header.
  /// @param stream_id              Stream id; generated when empty.
  /// @param total_size             Total payload size, if known.
  /// @param mime_type              MIME type of the payload.
  /// @param destination_identities Recipients; empty for everyone.
  /// @param sender_identity        Identity to send as; defaults to the local participant.
  [[deprecated("Constructing ByteStreamWriter directly is deprecated; use LocalParticipant::streamBytes instead")]]
  ByteStreamWriter(LocalParticipant& local_participant, const std::string& name, const std::string& topic = "",
                   const std::map<std::string, std::string>& attributes = {}, const std::string& stream_id = "",
                   std::optional<std::size_t> total_size = std::nullopt,
                   const std::string& mime_type = "application/octet-stream",
                   const std::vector<std::string>& destination_identities = {},
                   const std::string& sender_identity = "");

  /// @brief Write binary data to the stream.
  ///
  /// Blocks until the SDK has sent it.
  ///
  /// @param data Bytes to append.
  /// @throws DataStreamError if the stream is closed, the room has
  ///         disconnected, or the SDK reports an error.
  void write(const std::vector<std::uint8_t>& data);

  /// @brief Metadata associated with this stream.
  /// @return Stream metadata; updated with the SDK core's info once opened.
  const ByteStreamInfo& info() const noexcept { return info_; }

private:
  friend class BaseStreamWriter;
  friend class LocalParticipant;
  struct FactoryTag {};

  ByteStreamWriter(FactoryTag, LocalParticipant& local_participant, const StreamByteOptions& options);

  ByteStreamInfo info_;
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
