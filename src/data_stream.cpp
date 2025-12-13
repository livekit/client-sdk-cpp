#include "livekit/data_stream.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <stdexcept>

#include "livekit/ffi_client.h"
#include "livekit/local_participant.h"
#include "room.pb.h"

namespace livekit {

namespace {

std::string generateRandomId(std::size_t bytes = 16) {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist(0, 255);

  std::string out;
  out.reserve(bytes * 2);
  const char *hex = "0123456789abcdef";
  for (std::size_t i = 0; i < bytes; ++i) {
    int v = dist(rng);
    out.push_back(hex[(v >> 4) & 0xF]);
    out.push_back(hex[v & 0xF]);
  }
  return out;
}

// Split UTF-8 string into chunks of at most max_bytes, not breaking codepoints.
// Mimics Python split_utf8 helper.
std::vector<std::string> splitUtf8(const std::string &s,
                                   std::size_t max_bytes) {
  std::vector<std::string> result;
  if (s.empty())
    return result;

  std::size_t i = 0;
  const std::size_t n = s.size();

  while (i < n) {
    std::size_t end = std::min(i + max_bytes, n);

    // If end points into a UTF-8 continuation byte, walk back to a lead byte.
    // NOTE: end is an index into the string; ensure end < n before indexing.
    while (end > i && end < n &&
           (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) {
      --end;
    }
    // If end==n, we may still have landed on a continuation byte due to min().
    while (end > i && end == n &&
           (static_cast<unsigned char>(s[end - 1]) & 0xC0) == 0x80) {
      // Walk back until we hit a lead byte boundary.
      // This isn't perfect but is consistent with your earlier intent.
      --end;
      // If we backed up too far, fall through to fallback.
    }

    if (end == i) {
      // Fallback: avoid infinite loop if bytes are pathological.
      end = std::min(i + max_bytes, n);
    }

    result.emplace_back(s.substr(i, end - i));
    i = end;
  }

  return result;
}

std::map<std::string, std::string>
toMap(const google::protobuf::Map<std::string, std::string> &m) {
  std::map<std::string, std::string> out;
  for (const auto &kv : m)
    out.emplace(kv.first, kv.second);
  return out;
}

void fillBaseInfo(BaseStreamInfo &dst, const std::string &stream_id,
                  const std::string &mime_type, const std::string &topic,
                  std::int64_t timestamp_ms,
                  const std::optional<std::size_t> &total_size,
                  const std::map<std::string, std::string> &attrs) {
  dst.stream_id = stream_id;
  dst.mime_type = mime_type;
  dst.topic = topic;
  dst.timestamp = timestamp_ms;
  dst.size = total_size;
  dst.attributes = attrs;
}

} // namespace

// =====================================================================
// Reader implementation
// =====================================================================

TextStreamReader::TextStreamReader(const TextStreamInfo &info) : info_(info) {}

void TextStreamReader::onChunkUpdate(const std::string &text) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_)
      return;
    queue_.push_back(text);
  }
  cv_.notify_one();
}

void TextStreamReader::onStreamClose(
    const std::map<std::string, std::string> &trailer_attrs) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &kv : trailer_attrs) {
      info_.attributes[kv.first] = kv.second;
    }
    closed_ = true;
  }
  cv_.notify_all();
}

bool TextStreamReader::readNext(std::string &out) {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return !queue_.empty() || closed_; });

  if (!queue_.empty()) {
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }
  return false; // closed_ and empty
}

std::string TextStreamReader::readAll() {
  std::string result;
  std::string chunk;
  while (readNext(chunk))
    result += chunk;
  return result;
}

ByteStreamReader::ByteStreamReader(const ByteStreamInfo &info) : info_(info) {}

void ByteStreamReader::onChunkUpdate(const std::vector<std::uint8_t> &bytes) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_)
      return;
    queue_.push_back(bytes);
  }
  cv_.notify_one();
}

void ByteStreamReader::onStreamClose(
    const std::map<std::string, std::string> &trailer_attrs) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &kv : trailer_attrs) {
      info_.attributes[kv.first] = kv.second;
    }
    closed_ = true;
  }
  cv_.notify_all();
}

bool ByteStreamReader::readNext(std::vector<std::uint8_t> &out) {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return !queue_.empty() || closed_; });

  if (!queue_.empty()) {
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }
  return false;
}

// =====================================================================
// Writer implementation (uses your future-based FfiClient)
// =====================================================================

BaseStreamWriter::BaseStreamWriter(
    LocalParticipant &local_participant, const std::string &topic,
    const std::map<std::string, std::string> &attributes,
    const std::string &stream_id, std::optional<std::size_t> total_size,
    const std::string &mime_type,
    const std::vector<std::string> &destination_identities,
    const std::string &sender_identity)
    : local_participant_(local_participant),
      stream_id_(stream_id.empty() ? generateRandomId() : stream_id),
      mime_type_(mime_type), topic_(topic),
      timestamp_ms_(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count()),
      total_size_(total_size), attributes_(attributes),
      destination_identities_(destination_identities),
      sender_identity_(sender_identity) {
  if (sender_identity_.empty()) {
    sender_identity_ = local_participant_.identity();
  }
}

void BaseStreamWriter::ensureHeaderSent() {
  if (header_sent_)
    return;
  proto::DataStream::Header header;
  header.set_stream_id(stream_id_);
  header.set_timestamp(timestamp_ms_);
  header.set_mime_type(mime_type_);
  header.set_topic(topic_);

  if (total_size_.has_value()) {
    header.set_total_length(static_cast<std::uint64_t>(*total_size_));
  }
  for (const auto &kv : attributes_) {
    (*header.mutable_attributes())[kv.first] = kv.second;
  }
  if (kind_ == StreamKind::kText) {
    auto *th = header.mutable_text_header();
    th->set_operation_type(
        proto::DataStream::OperationType::DataStream_OperationType_CREATE);
    if (!reply_to_id_.empty()) {
      th->set_reply_to_stream_id(reply_to_id_);
    }
  } else if (kind_ == StreamKind::kByte) {
    header.mutable_byte_header()->set_name(byte_name_);
  }

  FfiClient::instance()
      .sendStreamHeaderAsync(local_participant_.ffiHandleId(), header,
                             destination_identities_, sender_identity_)
      .get();
  header_sent_ = true;
}

void BaseStreamWriter::sendChunk(const std::vector<std::uint8_t> &content) {
  if (closed_)
    throw std::runtime_error("Cannot send chunk after stream is closed");

  ensureHeaderSent();

  proto::DataStream::Chunk chunk;
  chunk.set_stream_id(stream_id_);
  chunk.set_chunk_index(next_chunk_index_++); // ✅ uses the new member
  chunk.set_content(content.data(), content.size());

  FfiClient::instance()
      .sendStreamChunkAsync(local_participant_.ffiHandleId(), chunk,
                            destination_identities_, sender_identity_)
      .get();
}

void BaseStreamWriter::sendTrailer(
    const std::string &reason,
    const std::map<std::string, std::string> &attributes) {
  ensureHeaderSent();

  proto::DataStream::Trailer trailer;
  trailer.set_stream_id(stream_id_);
  trailer.set_reason(reason);

  for (const auto &kv : attributes) {
    (*trailer.mutable_attributes())[kv.first] = kv.second;
  }

  FfiClient::instance()
      .sendStreamTrailerAsync(local_participant_.ffiHandleId(), trailer,
                              sender_identity_)
      .get();
}

void BaseStreamWriter::close(
    const std::string &reason,
    const std::map<std::string, std::string> &attributes) {
  if (closed_)
    throw std::runtime_error("Stream already closed");
  closed_ = true;
  sendTrailer(reason, attributes);
}

TextStreamWriter::TextStreamWriter(
    LocalParticipant &local_participant, const std::string &topic,
    const std::map<std::string, std::string> &attributes,
    const std::string &stream_id, std::optional<std::size_t> total_size,
    const std::string &reply_to_id,
    const std::vector<std::string> &destination_identities,
    const std::string &sender_identity)
    : BaseStreamWriter(
          local_participant, topic, attributes, stream_id, total_size,
          /*mime_type=*/"text/plain", destination_identities, sender_identity) {
  kind_ = StreamKind::kText;
  reply_to_id_ = reply_to_id;
  // ✅ Canonical user-facing metadata comes from BaseStreamWriter fields.
  fillBaseInfo(info_, stream_id_, mime_type_, topic_, timestamp_ms_,
               total_size_, attributes_);
}

void TextStreamWriter::write(const std::string &text) {
  std::lock_guard<std::mutex> lock(write_mutex_);
  if (closed_)
    throw std::runtime_error("Cannot write to closed TextStreamWriter");

  for (const auto &chunk_str : splitUtf8(text, kStreamChunkSize)) {
    const auto *p = reinterpret_cast<const std::uint8_t *>(chunk_str.data());
    std::vector<std::uint8_t> bytes(p, p + chunk_str.size());
    std::cout << "sending chunk " << std::endl;
    sendChunk(bytes);
  }
}

ByteStreamWriter::ByteStreamWriter(
    LocalParticipant &local_participant, const std::string &name,
    const std::string &topic,
    const std::map<std::string, std::string> &attributes,
    const std::string &stream_id, std::optional<std::size_t> total_size,
    const std::string &mime_type,
    const std::vector<std::string> &destination_identities,
    const std::string &sender_identity)
    : BaseStreamWriter(local_participant, topic, attributes, stream_id,
                       total_size, mime_type, destination_identities,
                       sender_identity) {
  kind_ = StreamKind::kByte;
  byte_name_ = name;
  fillBaseInfo(info_, stream_id_, mime_type_, topic_, timestamp_ms_,
               total_size_, attributes_);
  info_.name = name;
}

void ByteStreamWriter::write(const std::vector<std::uint8_t> &data) {
  std::lock_guard<std::mutex> lock(write_mutex_);
  if (closed_)
    throw std::runtime_error("Cannot write to closed ByteStreamWriter");

  std::size_t offset = 0;
  while (offset < data.size()) {
    const std::size_t n =
        std::min<std::size_t>(kStreamChunkSize, data.size() - offset);
    std::vector<std::uint8_t> chunk(data.begin() + offset,
                                    data.begin() + offset + n);
    sendChunk(chunk);
    offset += n;
  }
}

} // namespace livekit
