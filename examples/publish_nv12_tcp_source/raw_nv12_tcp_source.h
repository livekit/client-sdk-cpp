/*
 * Copyright 2025 LiveKit, Inc.
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

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace publish_nv12 {

struct RawNv12Frame {
  std::vector<std::uint8_t> data;
  std::int64_t timestamp_us{0};
};

using RawNv12FrameCallback = std::function<void(RawNv12Frame)>;

/**
 * Reads raw NV12 frames from a TCP server (fixed-size frames).
 * Runs a background thread; call stop() to disconnect.
 */
class RawNv12TcpSource {
public:
  RawNv12TcpSource(const std::string &host,
                   std::uint16_t port,
                   int width,
                   int height,
                   int fps,
                   RawNv12FrameCallback callback);
  ~RawNv12TcpSource();

  void start();
  void stop();
  bool running() const { return running_.load(); }

private:
  void loop();

  std::string host_;
  std::uint16_t port_;
  int width_;
  int height_;
  int fps_;
  std::size_t frame_size_{0};
  RawNv12FrameCallback callback_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

} // namespace publish_nv12
