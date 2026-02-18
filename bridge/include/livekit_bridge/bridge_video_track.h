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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace livekit {
class VideoSource;
class LocalVideoTrack;
class LocalTrackPublication;
class LocalParticipant;
} // namespace livekit

namespace livekit_bridge {

namespace test {
class BridgeVideoTrackTest;
} // namespace test

/**
 * RAII wrapper around a published local video track.
 *
 * Created via LiveKitBridge::createVideoTrack(). When the last shared_ptr
 * reference is released (or release() is called explicitly), the track is
 * unpublished and all underlying SDK resources are freed.
 *
 * All public methods are thread-safe: it is safe to call pushFrame() from
 * one thread while another calls mute()/unmute()/release(), or to call
 * pushFrame() concurrently from multiple threads.
 *
 * Usage:
 *   auto cam = bridge.createVideoTrack("cam", 1280, 720);
 *   cam->pushFrame(rgba_data, timestamp_us);
 *   cam->mute();
 *   cam.reset();  // unpublishes
 */
class BridgeVideoTrack {
public:
  ~BridgeVideoTrack();

  // Non-copyable
  BridgeVideoTrack(const BridgeVideoTrack &) = delete;
  BridgeVideoTrack &operator=(const BridgeVideoTrack &) = delete;

  /**
   * Push an RGBA video frame to the track.
   *
   * @param rgba          Raw RGBA pixel data. Must contain exactly
   *                      (width * height * 4) bytes.
   * @param timestamp_us  Presentation timestamp in microseconds.
   *                      Pass 0 to let the SDK assign one.
   */
  void pushFrame(const std::vector<std::uint8_t> &rgba,
                 std::int64_t timestamp_us = 0);

  /**
   * Push an RGBA video frame from a raw pointer.
   *
   * @param rgba          Pointer to RGBA pixel data.
   * @param rgba_size     Size of the data buffer in bytes.
   * @param timestamp_us  Presentation timestamp in microseconds.
   */
  void pushFrame(const std::uint8_t *rgba, std::size_t rgba_size,
                 std::int64_t timestamp_us = 0);

  /// Mute the video track (stops sending video to the room).
  void mute();

  /// Unmute the video track (resumes sending video to the room).
  void unmute();

  /// Track name as provided at creation.
  const std::string &name() const noexcept { return name_; }

  /// Video width in pixels.
  int width() const noexcept { return width_; }

  /// Video height in pixels.
  int height() const noexcept { return height_; }

  /// Whether this track has been released / unpublished.
  bool isReleased() const noexcept;

private:
  /// Explicitly unpublish and release all resources.
  /// Called automatically by the destructor.
  void release();

  friend class LiveKitBridge;
  friend class test::BridgeVideoTrackTest;

  BridgeVideoTrack(std::string name, int width, int height,
                   std::shared_ptr<livekit::VideoSource> source,
                   std::shared_ptr<livekit::LocalVideoTrack> track,
                   std::shared_ptr<livekit::LocalTrackPublication> publication,
                   livekit::LocalParticipant *participant);

  mutable std::mutex mutex_;
  std::string name_;
  int width_;
  int height_;
  bool released_ = false;

  std::shared_ptr<livekit::VideoSource> source_;
  std::shared_ptr<livekit::LocalVideoTrack> track_;
  std::shared_ptr<livekit::LocalTrackPublication> publication_;
  livekit::LocalParticipant *participant_ = nullptr; // not owned
};

} // namespace livekit_bridge
