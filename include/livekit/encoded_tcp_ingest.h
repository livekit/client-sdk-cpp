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

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "livekit/ffi_handle.h"
#include "livekit/room_event_types.h"
#include "livekit/track.h"

namespace livekit {

namespace proto {
class FfiEvent;
}

class Room;

struct EncodedTcpIngestOptions {
  std::string host = "127.0.0.1";
  std::uint16_t port = 5005;
  VideoCodec codec = VideoCodec::H265;
  std::uint32_t width = 640;
  std::uint32_t height = 480;
  std::optional<std::string> track_name;
  std::optional<TrackSource> track_source = TrackSource::SOURCE_CAMERA;
  std::optional<std::uint64_t> max_bitrate_bps;
  std::optional<double> max_framerate_fps;
  std::optional<std::uint32_t> reconnect_backoff_ms;
  bool unpublish_on_stop = true;
};

struct EncodedTcpIngestStats {
  std::uint64_t frames_accepted = 0;
  std::uint64_t frames_dropped = 0;
  std::uint64_t keyframes = 0;
  std::uint64_t tcp_reconnects = 0;
};

class EncodedIngestObserver {
public:
  virtual ~EncodedIngestObserver() = default;

  virtual void onConnected(const std::string &peer) { (void)peer; }
  virtual void onDisconnected(const std::string &reason) { (void)reason; }
  virtual void onKeyframeRequested() {}
  virtual void onTargetBitrate(std::uint32_t bitrate_bps,
                               double framerate_fps) {
    (void)bitrate_bps;
    (void)framerate_fps;
  }
};

class EncodedTcpIngest {
public:
  EncodedTcpIngest() = default;
  ~EncodedTcpIngest();

  EncodedTcpIngest(const EncodedTcpIngest &) = delete;
  EncodedTcpIngest &operator=(const EncodedTcpIngest &) = delete;
  EncodedTcpIngest(EncodedTcpIngest &&other) noexcept;
  EncodedTcpIngest &operator=(EncodedTcpIngest &&other) noexcept;

  static EncodedTcpIngest start(Room &room,
                                const EncodedTcpIngestOptions &options);

  void stop();
  EncodedTcpIngestStats stats() const;

  void setObserver(std::shared_ptr<EncodedIngestObserver> observer);

  const std::string &trackSid() const noexcept { return track_sid_; }
  const std::string &trackName() const noexcept { return track_name_; }
  bool running() const noexcept { return handle_.valid(); }

private:
  explicit EncodedTcpIngest(FfiHandle handle, std::string track_sid,
                            std::string track_name);

  void registerListener();
  void unregisterListener() noexcept;
  void handleEvent(const proto::FfiEvent &event) const;

  FfiHandle handle_;
  std::string track_sid_;
  std::string track_name_;
  int listener_id_ = 0;
  mutable std::mutex observer_lock_;
  std::shared_ptr<EncodedIngestObserver> observer_;
};

} // namespace livekit
