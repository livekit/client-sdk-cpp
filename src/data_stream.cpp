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

#include "livekit/data_stream.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <random>
#include <stdexcept>

#include "data_stream_proto_converter.h"
#include "data_stream_writer_registry.h"
#include "ffi_client.h"
#include "livekit/local_participant.h"
#include "lk_log.h"
#include "room.pb.h"

namespace livekit {

namespace {

std::string generateRandomId(std::size_t bytes = 16) {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist(0, 255);

  std::string out;
  out.reserve(bytes * 2);
  const char* hex = "0123456789abcdef";
  for (std::size_t i = 0; i < bytes; ++i) {
    const int v = dist(rng);
    out.push_back(hex[(v >> 4) & 0xF]);
    out.push_back(hex[v & 0xF]);
  }
  return out;
}

void fillBaseInfo(BaseStreamInfo& dst, const std::string& stream_id, const std::string& mime_type,
                  const std::string& topic, std::int64_t timestamp_ms, const std::optional<std::size_t>& total_size,
                  const std::map<std::string, std::string>& attrs) {
  dst.stream_id = stream_id;
  dst.mime_type = mime_type;
  dst.topic = topic;
  dst.timestamp = timestamp_ms;
  dst.size = total_size;
  dst.attributes = attrs;
}

} // namespace

// =====================================================================
// Errors
// =====================================================================

DataStreamError::DataStreamError(const std::string& message) : std::runtime_error(message) {}

// =====================================================================
// Reader implementation
// =====================================================================

namespace {

/// Shared state for TextStreamReader / ByteStreamReader.
///
/// The FFI callback thread pushes chunks and the end-of-stream marker; a user
/// thread pops them. Final attributes are merged into the reader's info on the
/// reading thread so info() is never mutated behind a single-threaded reader.
template <typename Chunk>
class ReaderState {
public:
  void push(Chunk chunk) {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (ended_ || closed_) {
        return;
      }
      queue_.push_back(std::move(chunk));
    }
    cv_.notify_one();
  }

  void end(const std::map<std::string, std::string>& attributes, std::optional<std::string> error) {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (ended_) {
        return;
      }
      ended_ = true;
      end_attributes_ = attributes;
      error_ = std::move(error);
    }
    cv_.notify_all();
  }

  void close() {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      closed_ = true;
      queue_.clear();
    }
    cv_.notify_all();
  }

  /// Pops the next chunk into @p out. Returns false at the end of the stream.
  /// Throws DataStreamError once the queue is drained if the stream failed.
  template <typename Info>
  bool next(Chunk& out, Info& info, const char* reader_name) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!closed_ && !ended_ && queue_.empty() && !warned_ffi_thread_ && isOnFfiCallbackThread()) {
      warned_ffi_thread_ = true;
      LK_LOG_WARN(
          "{}::readNext called on the SDK event thread; this blocks event delivery and can deadlock. "
          "Read from a separate thread instead.",
          reader_name);
    }
    cv_.wait(lock, [this] { return closed_ || ended_ || !queue_.empty(); });

    if (closed_) {
      return false;
    }
    if (!queue_.empty()) {
      out = std::move(queue_.front());
      queue_.pop_front();
      return true;
    }

    // Ended and drained.
    if (!attributes_merged_) {
      attributes_merged_ = true;
      for (const auto& kv : end_attributes_) {
        info.attributes[kv.first] = kv.second;
      }
    }
    if (error_) {
      throw DataStreamError(*error_);
    }
    return false;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Chunk> queue_;
  std::map<std::string, std::string> end_attributes_;
  std::optional<std::string> error_;
  bool ended_ = false;
  bool closed_ = false;
  bool attributes_merged_ = false;
  bool warned_ffi_thread_ = false;
};

} // namespace

struct TextStreamReader::Impl : ReaderState<std::string> {};
struct ByteStreamReader::Impl : ReaderState<std::vector<std::uint8_t>> {};

TextStreamReader::TextStreamReader(TextStreamInfo info) : info_(std::move(info)), impl_(std::make_unique<Impl>()) {}

TextStreamReader::~TextStreamReader() = default;

void TextStreamReader::onChunkReceived(std::string text) { impl_->push(std::move(text)); }

void TextStreamReader::onStreamEnded(const std::map<std::string, std::string>& attributes,
                                     std::optional<std::string> error) {
  impl_->end(attributes, std::move(error));
}

bool TextStreamReader::readNext(std::string& out) { return impl_->next(out, info_, "TextStreamReader"); }

std::string TextStreamReader::readAll() {
  std::string result;
  std::string chunk;
  while (readNext(chunk)) {
    result += chunk;
  }
  return result;
}

void TextStreamReader::close() { impl_->close(); }

ByteStreamReader::ByteStreamReader(ByteStreamInfo info) : info_(std::move(info)), impl_(std::make_unique<Impl>()) {}

ByteStreamReader::~ByteStreamReader() = default;

void ByteStreamReader::onChunkReceived(std::vector<std::uint8_t> bytes) { impl_->push(std::move(bytes)); }

void ByteStreamReader::onStreamEnded(const std::map<std::string, std::string>& attributes,
                                     std::optional<std::string> error) {
  impl_->end(attributes, std::move(error));
}

bool ByteStreamReader::readNext(std::vector<std::uint8_t>& out) { return impl_->next(out, info_, "ByteStreamReader"); }

std::vector<std::uint8_t> ByteStreamReader::readAll() {
  std::vector<std::uint8_t> result;
  std::vector<std::uint8_t> chunk;
  while (readNext(chunk)) {
    result.insert(result.end(), chunk.begin(), chunk.end());
  }
  return result;
}

void ByteStreamReader::close() { impl_->close(); }

// =====================================================================
// Writer registry
// =====================================================================

namespace detail {

bool StreamWriterRegistry::add(BaseStreamWriter* writer) {
  const std::scoped_lock<std::mutex> lock(mutex_);
  if (closed_) {
    return false;
  }
  writers_.insert(writer);
  return true;
}

void StreamWriterRegistry::remove(BaseStreamWriter* writer) {
  const std::scoped_lock<std::mutex> lock(mutex_);
  writers_.erase(writer);
}

std::uint64_t StreamWriterRegistry::participantHandle() const {
  const std::scoped_lock<std::mutex> lock(mutex_);
  if (closed_) {
    throw DataStreamError("stream writer: not connected");
  }
  return participant_handle_;
}

void StreamWriterRegistry::closeAll() noexcept {
  // Held while notifying writers so a writer being destroyed (which calls
  // remove()) stays alive until it has been notified.
  const std::scoped_lock<std::mutex> lock(mutex_);
  closed_ = true;
  for (auto* writer : writers_) {
    writer->onParticipantDisconnected();
  }
  writers_.clear();
}

} // namespace detail

// =====================================================================
// Writer implementation
// =====================================================================

namespace {

constexpr char kWriterNotConnected[] = "stream writer: not connected";

} // namespace

BaseStreamWriter::BaseStreamWriter(LocalParticipant& local_participant, std::string topic,
                                   std::map<std::string, std::string> attributes, std::string stream_id,
                                   std::optional<std::size_t> total_size, std::string mime_type,
                                   std::vector<std::string> destination_identities, std::string sender_identity)
    : local_participant_(local_participant),
      stream_id_(stream_id.empty() ? generateRandomId() : std::move(stream_id)),
      mime_type_(std::move(mime_type)),
      topic_(std::move(topic)),
      timestamp_ms_(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count()),
      total_size_(total_size),
      attributes_(std::move(attributes)),
      destination_identities_(std::move(destination_identities)),
      sender_identity_(std::move(sender_identity)),
      registry_(local_participant.stream_writers_) {
  if (sender_identity_.empty()) {
    sender_identity_ = local_participant_.identity();
  }
  if (!registry_->add(this)) {
    // Created after the room disconnected: usable only to fail cleanly.
    participant_gone_ = true;
    closed_ = true;
  }
}

BaseStreamWriter::~BaseStreamWriter() {
  // After remove() returns, closeAll() can no longer reach this writer.
  registry_->remove(this);

  const std::uint64_t handle = takeHandle();
  if (handle == 0) {
    return;
  }
  // Opened but never closed. Rust's writer sends a trailer when dropped, but
  // only from a Tokio thread; dropping the handle here (a C++ thread) would
  // skip it and leave receivers waiting. Close explicitly, without waiting.
  try {
    if (kind_ == StreamKind::kText) {
      (void)FfiClient::instance().textStreamCloseAsync(handle, std::nullopt, {});
    } else {
      (void)FfiClient::instance().byteStreamCloseAsync(handle, std::nullopt, {});
    }
  } catch (const std::exception& e) {
    LK_LOG_DEBUG("Stream writer {} could not be closed on destruction: {}", stream_id_, e.what());
    (void)dropFfiHandle(handle);
  } catch (...) {
    (void)dropFfiHandle(handle);
  }
}

std::uint64_t BaseStreamWriter::takeHandle() noexcept {
  const std::scoped_lock<std::mutex> lock(state_mutex_);
  const std::uint64_t handle = writer_handle_;
  writer_handle_ = 0;
  return handle;
}

void BaseStreamWriter::onParticipantDisconnected() noexcept {
  std::uint64_t handle = 0;
  {
    const std::scoped_lock<std::mutex> lock(state_mutex_);
    participant_gone_ = true;
    closed_ = true;
    handle = writer_handle_;
    writer_handle_ = 0;
  }
  // The room is gone; there's nobody to send a trailer to.
  if (handle != 0) {
    (void)dropFfiHandle(handle);
  }
}

void BaseStreamWriter::throwNotConnected() const { throw DataStreamError(kWriterNotConnected); }

void BaseStreamWriter::applyOpenedInfo(std::int64_t timestamp_ms, const std::string& mime_type) {
  timestamp_ms_ = timestamp_ms;
  mime_type_ = mime_type;
  header_sent_ = true;
}

void BaseStreamWriter::openLocked() {
  {
    const std::scoped_lock<std::mutex> lock(state_mutex_);
    if (participant_gone_) {
      throwNotConnected();
    }
    if (writer_handle_ != 0) {
      return;
    }
  }

  const std::uint64_t participant_handle = registry_->participantHandle();
  std::uint64_t handle = 0;
  if (kind_ == StreamKind::kText) {
    auto* self = static_cast<TextStreamWriter*>(this);
    proto::StreamTextOptions options;
    options.set_topic(topic_);
    for (const auto& kv : attributes_) {
      (*options.mutable_attributes())[kv.first] = kv.second;
    }
    for (const auto& identity : destination_identities_) {
      options.add_destination_identities(identity);
    }
    options.set_id(stream_id_);
    if (!reply_to_id_.empty()) {
      options.set_reply_to_stream_id(reply_to_id_);
    }
    for (const auto& id : self->attached_stream_ids_) {
      options.add_attached_stream_ids(id);
    }
    options.set_sender_identity(sender_identity_);

    const auto owned = FfiClient::instance().textStreamOpenAsync(participant_handle, options).get();
    handle = owned.handle().id();
    TextStreamInfo info = fromProto(owned.info());
    info.size = total_size_; // Text streams can't transmit a size; keep the local one.
    self->info_ = std::move(info);
    applyOpenedInfo(self->info_.timestamp, self->info_.mime_type);
  } else if (kind_ == StreamKind::kByte) {
    auto* self = static_cast<ByteStreamWriter*>(this);
    proto::StreamByteOptions options;
    options.set_topic(topic_);
    for (const auto& kv : attributes_) {
      (*options.mutable_attributes())[kv.first] = kv.second;
    }
    for (const auto& identity : destination_identities_) {
      options.add_destination_identities(identity);
    }
    options.set_id(stream_id_);
    if (!byte_name_.empty()) {
      options.set_name(byte_name_);
    }
    if (!mime_type_.empty()) {
      options.set_mime_type(mime_type_);
    }
    if (total_size_) {
      options.set_total_length(static_cast<std::uint64_t>(*total_size_));
    }
    options.set_sender_identity(sender_identity_);

    const auto owned = FfiClient::instance().byteStreamOpenAsync(participant_handle, options).get();
    handle = owned.handle().id();
    self->info_ = fromProto(owned.info());
    applyOpenedInfo(self->info_.timestamp, self->info_.mime_type);
  } else {
    throw DataStreamError("stream writer: unknown stream kind");
  }

  bool gone = false;
  {
    const std::scoped_lock<std::mutex> lock(state_mutex_);
    gone = participant_gone_;
    if (!gone) {
      writer_handle_ = handle;
    }
  }
  if (gone) {
    // The room disconnected while the open was in flight.
    (void)dropFfiHandle(handle);
    throwNotConnected();
  }
}

void BaseStreamWriter::open() {
  const std::scoped_lock<std::mutex> op(op_mutex_);
  openLocked();
}

void BaseStreamWriter::writePayload(const char* data, std::size_t size, const char* closed_message) {
  const std::scoped_lock<std::mutex> op(op_mutex_);
  if (closed_) {
    const std::scoped_lock<std::mutex> lock(state_mutex_);
    if (participant_gone_) {
      throwNotConnected();
    }
    throw DataStreamError(closed_message);
  }
  if (size == 0) {
    return;
  }
  openLocked();

  std::uint64_t handle = 0;
  {
    const std::scoped_lock<std::mutex> lock(state_mutex_);
    if (participant_gone_) {
      throwNotConnected();
    }
    handle = writer_handle_;
  }
  // Wait for each write before the next one: the SDK core runs every write
  // request as its own task, so unawaited writes could reorder or land after
  // the trailer.
  if (kind_ == StreamKind::kText) {
    FfiClient::instance().textStreamWriteAsync(handle, std::string(data, size)).get();
  } else {
    FfiClient::instance().byteStreamWriteAsync(handle, reinterpret_cast<const std::uint8_t*>(data), size).get();
  }
}

void BaseStreamWriter::closeLocked(const std::string& reason, const std::map<std::string, std::string>& attributes) {
  openLocked();
  // close consumes the handle; it must not be dropped afterwards.
  const std::uint64_t handle = takeHandle();
  if (handle == 0) {
    throwNotConnected();
  }
  const std::optional<std::string> close_reason = reason.empty() ? std::nullopt : std::optional<std::string>(reason);
  if (kind_ == StreamKind::kText) {
    FfiClient::instance().textStreamCloseAsync(handle, close_reason, attributes).get();
  } else {
    FfiClient::instance().byteStreamCloseAsync(handle, close_reason, attributes).get();
  }
}

void BaseStreamWriter::close(const std::string& reason, const std::map<std::string, std::string>& attributes) {
  // Taking op_mutex_ orders the close after any in-flight write.
  const std::scoped_lock<std::mutex> op(op_mutex_);
  {
    const std::scoped_lock<std::mutex> lock(state_mutex_);
    if (participant_gone_) {
      throwNotConnected();
    }
  }
  if (closed_.exchange(true)) {
    throw DataStreamError("Stream already closed");
  }
  closeLocked(reason, attributes);
}

void BaseStreamWriter::ensureHeaderSent() { open(); }

void BaseStreamWriter::sendChunk(const std::vector<std::uint8_t>& content) {
  writePayload(reinterpret_cast<const char*>(content.data()), content.size(),
               "Cannot send chunk after stream is closed");
}

void BaseStreamWriter::sendTrailer(const std::string& reason, const std::map<std::string, std::string>& attributes) {
  const std::scoped_lock<std::mutex> op(op_mutex_);
  closed_ = true;
  closeLocked(reason, attributes);
}

TextStreamWriter::TextStreamWriter(LocalParticipant& local_participant, const std::string& topic,
                                   const std::map<std::string, std::string>& attributes, const std::string& stream_id,
                                   std::optional<std::size_t> total_size, const std::string& reply_to_id,
                                   const std::vector<std::string>& destination_identities,
                                   const std::string& sender_identity)
    : BaseStreamWriter(local_participant, topic, attributes, stream_id, total_size,
                       /*mime_type=*/"text/plain", destination_identities, sender_identity) {
  kind_ = StreamKind::kText;
  reply_to_id_ = reply_to_id;
  fillBaseInfo(info_, stream_id_, mime_type_, topic_, timestamp_ms_, total_size_, attributes_);
  if (!reply_to_id.empty()) {
    info_.reply_to_stream_id = reply_to_id;
  }
}

TextStreamWriter::TextStreamWriter(FactoryTag /*tag*/, LocalParticipant& local_participant,
                                   const StreamTextOptions& options)
    : BaseStreamWriter(local_participant, options.topic, options.attributes, options.stream_id.value_or(""),
                       std::nullopt, /*mime_type=*/"text/plain", options.destination_identities,
                       options.sender_identity.value_or("")),
      attached_stream_ids_(options.attached_stream_ids) {
  kind_ = StreamKind::kText;
  reply_to_id_ = options.reply_to_stream_id.value_or("");
  fillBaseInfo(info_, stream_id_, mime_type_, topic_, timestamp_ms_, total_size_, attributes_);
  info_.reply_to_stream_id = options.reply_to_stream_id;
  info_.attachments = attached_stream_ids_;
}

void TextStreamWriter::write(const std::string& text) {
  writePayload(text.data(), text.size(), "Cannot write to closed TextStreamWriter");
}

ByteStreamWriter::ByteStreamWriter(LocalParticipant& local_participant, const std::string& name,
                                   const std::string& topic, const std::map<std::string, std::string>& attributes,
                                   const std::string& stream_id, std::optional<std::size_t> total_size,
                                   const std::string& mime_type, const std::vector<std::string>& destination_identities,
                                   const std::string& sender_identity)
    : BaseStreamWriter(local_participant, topic, attributes, stream_id, total_size, mime_type, destination_identities,
                       sender_identity) {
  kind_ = StreamKind::kByte;
  byte_name_ = name;
  fillBaseInfo(info_, stream_id_, mime_type_, topic_, timestamp_ms_, total_size_, attributes_);
  info_.name = name;
}

ByteStreamWriter::ByteStreamWriter(FactoryTag /*tag*/, LocalParticipant& local_participant,
                                   const StreamByteOptions& options)
    : BaseStreamWriter(local_participant, options.topic, options.attributes, options.stream_id.value_or(""),
                       options.total_length
                           ? std::optional<std::size_t>(static_cast<std::size_t>(*options.total_length))
                           : std::nullopt,
                       options.mime_type.value_or("application/octet-stream"), options.destination_identities,
                       options.sender_identity.value_or("")) {
  kind_ = StreamKind::kByte;
  byte_name_ = options.name.value_or("");
  fillBaseInfo(info_, stream_id_, mime_type_, topic_, timestamp_ms_, total_size_, attributes_);
  info_.name = byte_name_;
}

void ByteStreamWriter::write(const std::vector<std::uint8_t>& data) {
  writePayload(reinterpret_cast<const char*>(data.data()), data.size(), "Cannot write to closed ByteStreamWriter");
}

} // namespace livekit
