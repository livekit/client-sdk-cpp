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

#include "data_stream.pb.h"
#include "livekit/data_stream.h"
#include "livekit/visibility.h"

namespace livekit {

/// @brief Convert a text stream operation type to the FFI enum.
/// @param in C++ operation type.
/// @return The FFI enum value.
LIVEKIT_INTERNAL_API proto::TextStreamInfo::OperationType toProto(TextStreamOperationType in);

/// @brief Convert an FFI text stream operation type to the public enum.
/// @param in FFI enum value.
/// @return The C++ operation type.
LIVEKIT_INTERNAL_API TextStreamOperationType fromProto(proto::TextStreamInfo::OperationType in);

/// @brief Convert one-shot text send options to the FFI proto.
/// @param in C++ options.
/// @return The FFI options; `compress` is always set explicitly.
LIVEKIT_INTERNAL_API proto::StreamTextOptions toProto(const StreamTextOptions& in);

/// @brief Convert one-shot byte send options to the FFI proto.
/// @param in C++ options.
/// @return The FFI options; `compress` is always set explicitly.
LIVEKIT_INTERNAL_API proto::StreamByteOptions toProto(const StreamByteOptions& in);

/// @brief Convert FFI text stream metadata to the public info struct.
/// @param in FFI info.
/// @return Public info.
LIVEKIT_INTERNAL_API TextStreamInfo fromProto(const proto::TextStreamInfo& in);

/// @brief Convert FFI byte stream metadata to the public info struct.
/// @param in FFI info.
/// @return Public info.
LIVEKIT_INTERNAL_API ByteStreamInfo fromProto(const proto::ByteStreamInfo& in);

} // namespace livekit
