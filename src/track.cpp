/*
 * Copyright 2023 LiveKit
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

#include "livekit/track.h"

#include "livekit/ffi_client.h"
#include <future>
#include <optional>

namespace livekit {

Track::Track(std::weak_ptr<FfiHandle> handle, std::string sid, std::string name,
             TrackKind kind, StreamState state, bool muted, bool remote)
    : handle_(std::move(handle)), sid_(std::move(sid)), name_(std::move(name)),
      kind_(kind), state_(state), muted_(muted), remote_(remote) {}

void Track::setPublicationFields(std::optional<TrackSource> source,
                                 std::optional<bool> simulcasted,
                                 std::optional<uint32_t> width,
                                 std::optional<uint32_t> height,
                                 std::optional<std::string> mime_type) {
  source_ = source;
  simulcasted_ = simulcasted;
  width_ = width;
  height_ = height;
  mime_type_ = std::move(mime_type);
}

std::future<std::vector<RtcStats>> Track::getStats() const {
  auto id = ffi_handle_id();
  if (!id) {
    // make a ready future with an empty vector
    std::promise<std::vector<RtcStats>> pr;
    pr.set_value({});
    return pr.get_future();
  }

  // just forward the future from FfiClient
  return FfiClient::instance().getTrackStatsAsync(id);
}

} // namespace livekit