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
#include <vector>

namespace livekit {
class LocalDataTrack;
} // namespace livekit

namespace livekit_bridge {

namespace test {
class BridgeDataTrackTest;
} // namespace test

/**
 * Handle to a published local data track.
 *
 * Created via LiveKitBridge::createDataTrack(). The bridge retains a
 * reference to every track it creates and will automatically release all
 * tracks when disconnect() is called. To unpublish a track mid-session,
 * call release() explicitly.
 *
 * Unlike BridgeAudioTrack / BridgeVideoTrack, data tracks have no
 * Source, Publication, mute(), or unmute(). They carry arbitrary binary
 * frames via pushFrame().
 *
 * All public methods are thread-safe.
 *
 * Usage:
 *   auto dt = bridge.createDataTrack("sensor-data");
 *   dt->pushFrame({0x01, 0x02, 0x03});
 *   dt->release();  // unpublishes mid-session
 */
class BridgeDataTrack {
public:
  ~BridgeDataTrack();

  BridgeDataTrack(const BridgeDataTrack &) = delete;
  BridgeDataTrack &operator=(const BridgeDataTrack &) = delete;

  /**
   * Push a binary frame to all subscribers of this data track.
   *
   * @param payload        Raw bytes to send.
   * @param user_timestamp Optional application-defined timestamp.
   * @return true if the frame was pushed, false if the track has been
   *         released or the push failed (e.g. back-pressure).
   */
  bool pushFrame(const std::vector<std::uint8_t> &payload,
                 std::optional<std::uint64_t> user_timestamp = std::nullopt);

  /**
   * Push a binary frame from a raw pointer.
   *
   * @param data           Pointer to raw bytes.
   * @param size           Number of bytes.
   * @param user_timestamp Optional application-defined timestamp.
   * @return true on success, false if released or push failed.
   */
  bool pushFrame(const std::uint8_t *data, std::size_t size,
                 std::optional<std::uint64_t> user_timestamp = std::nullopt);

  /// Track name as provided at creation.
  const std::string &name() const noexcept { return name_; }

  /// Whether the track is still published in the room.
  bool isPublished() const;

  /// Whether this track has been released / unpublished.
  bool isReleased() const noexcept;

  /**
   * Explicitly unpublish the track and release underlying SDK resources.
   *
   * After this call, pushFrame() returns false. Called automatically by the
   * destructor and by LiveKitBridge::disconnect(). Safe to call multiple
   * times (idempotent).
   */
  void release();

private:
  friend class LiveKitBridge;
  friend class test::BridgeDataTrackTest;

  BridgeDataTrack(std::string name,
                  std::shared_ptr<livekit::LocalDataTrack> track);

  /** Protects released_ and track_ for thread-safe access. */
  mutable std::mutex mutex_;

  /** Publisher-assigned track name (immutable after construction). */
  std::string name_;

  /** True after release() or disconnect(); prevents further pushFrame(). */
  bool released_ = false;

  /** Underlying SDK data track handle. Nulled on release(). */
  std::shared_ptr<livekit::LocalDataTrack> track_;
};

} // namespace livekit_bridge
