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

#include "data_stream_proto_converter.h"

namespace livekit {

proto::StreamTextOptions toProto(const StreamTextOptions& in) {
  proto::StreamTextOptions out;
  out.set_topic(in.topic);
  for (const auto& kv : in.attributes) {
    (*out.mutable_attributes())[kv.first] = kv.second;
  }
  for (const auto& identity : in.destination_identities) {
    out.add_destination_identities(identity);
  }
  if (in.stream_id) {
    out.set_id(*in.stream_id);
  }
  if (in.reply_to_stream_id) {
    out.set_reply_to_stream_id(*in.reply_to_stream_id);
  }
  for (const auto& id : in.attached_stream_ids) {
    out.add_attached_stream_ids(id);
  }
  out.set_compress(in.compress);
  if (in.sender_identity) {
    out.set_sender_identity(*in.sender_identity);
  }
  return out;
}

proto::StreamByteOptions toProto(const StreamByteOptions& in) {
  proto::StreamByteOptions out;
  out.set_topic(in.topic);
  for (const auto& kv : in.attributes) {
    (*out.mutable_attributes())[kv.first] = kv.second;
  }
  for (const auto& identity : in.destination_identities) {
    out.add_destination_identities(identity);
  }
  if (in.stream_id) {
    out.set_id(*in.stream_id);
  }
  if (in.name) {
    out.set_name(*in.name);
  }
  if (in.mime_type) {
    out.set_mime_type(*in.mime_type);
  }
  if (in.total_length) {
    out.set_total_length(*in.total_length);
  }
  out.set_compress(in.compress);
  if (in.sender_identity) {
    out.set_sender_identity(*in.sender_identity);
  }
  return out;
}

TextStreamInfo fromProto(const proto::TextStreamInfo& in) {
  TextStreamInfo out;
  out.stream_id = in.stream_id();
  out.mime_type = in.mime_type();
  out.topic = in.topic();
  out.timestamp = in.timestamp();
  if (in.has_total_length()) {
    out.size = static_cast<std::size_t>(in.total_length());
  }
  for (const auto& kv : in.attributes()) {
    out.attributes.emplace(kv.first, kv.second);
  }
  for (const auto& id : in.attached_stream_ids()) {
    out.attachments.push_back(id);
  }
  if (in.has_reply_to_stream_id()) {
    out.reply_to_stream_id = in.reply_to_stream_id();
  }
  return out;
}

ByteStreamInfo fromProto(const proto::ByteStreamInfo& in) {
  ByteStreamInfo out;
  out.stream_id = in.stream_id();
  out.mime_type = in.mime_type();
  out.topic = in.topic();
  out.timestamp = in.timestamp();
  if (in.has_total_length()) {
    out.size = static_cast<std::size_t>(in.total_length());
  }
  for (const auto& kv : in.attributes()) {
    out.attributes.emplace(kv.first, kv.second);
  }
  out.name = in.name();
  return out;
}

} // namespace livekit
