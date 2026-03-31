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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>

#include <livekit/lk_log.h>

namespace livekit_examples::data_stamping {

enum class FrameType {
  Video,
  Imu,
};

/**
 * Aligns recent video and IMU frames by user timestamp.
 *
 * This demo stamps video with `VideoFrameMetadata::user_timestamp_us` and
 * IMU data with the same microseconds-since-epoch convention. The constructor
 * accepts an alignment window in nanoseconds for convenience, but comparisons
 * are performed in microseconds to match the stamped values.
 *
 * The default window is +/- 5 ms. At 100 Hz, IMU samples arrive roughly every
 * 10 ms, so a 5 ms half-window is a reasonable default for pairing each ~30 Hz
 * video frame with its nearest IMU sample without being overly permissive.
 */
class DataAlignmentManager {
public:
  // Video comes in at ~30Hz, imu comes in at ~100Hz, so 30 frames of history is
  // enough to cover a few seconds.
  static constexpr std::size_t kDefaultHistorySize = 30;
  // We allow up to 5ms of difference between video and imu timestamps for them
  // to be considered aligned.
  static constexpr std::uint64_t kDefaultAlignmentWindowNs = 5'000'000;

  explicit DataAlignmentManager(
      std::uint64_t alignment_window_ns = kDefaultAlignmentWindowNs,
      std::size_t history_size = kDefaultHistorySize)
      : aligned_frame_count_(0), alignment_window_us_(std::max<std::uint64_t>(
                                     1, (alignment_window_ns + 999) / 1000)),
        history_size_(std::max<std::size_t>(1, history_size)) {}

  void addVideoFrame(std::uint64_t user_timestamp) {
    addFrame(video_timestamps_, imu_timestamps_, user_timestamp,
             FrameType::Video);
  }

  void addImuFrame(std::uint64_t user_timestamp) {
    addFrame(imu_timestamps_, video_timestamps_, user_timestamp,
             FrameType::Imu);
  }

  std::uint64_t numberAlignedDataFrames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return aligned_frame_count_;
  }

private:
  struct TimestampEntry {
    std::uint64_t user_timestamp;
    bool matched;
  };

  static std::uint64_t absoluteDifference(std::uint64_t lhs,
                                          std::uint64_t rhs) {
    return lhs >= rhs ? (lhs - rhs) : (rhs - lhs);
  }

  void addFrame(std::deque<TimestampEntry> &own_timestamps,
                std::deque<TimestampEntry> &other_timestamps,
                std::uint64_t user_timestamp, FrameType frame_type) {
    std::lock_guard<std::mutex> lock(mutex_);

    own_timestamps.push_back(TimestampEntry{user_timestamp, false});
    trim(own_timestamps);

    const auto match_index = findBestMatch(other_timestamps, user_timestamp);
    if (!match_index.has_value()) {
      return;
    }

    own_timestamps.back().matched = true;
    other_timestamps[*match_index].matched = true;
    ++aligned_frame_count_;

    LK_LOG_INFO(
        "Aligned frames: {}={} {}={} "
        "delta_us={}",
        frame_type == FrameType::Video ? "Video" : "IMU", user_timestamp,
        frame_type == FrameType::Video ? "IMU" : "Video",
        other_timestamps[*match_index].user_timestamp,
        absoluteDifference(user_timestamp,
                           other_timestamps[*match_index].user_timestamp));
  }

  void trim(std::deque<TimestampEntry> &timestamps) const {
    while (timestamps.size() > history_size_) {
      timestamps.pop_front();
    }
  }

  std::optional<std::size_t>
  findBestMatch(const std::deque<TimestampEntry> &timestamps,
                std::uint64_t user_timestamp) const {
    std::optional<std::size_t> best_index;
    std::uint64_t best_delta = std::numeric_limits<std::uint64_t>::max();

    for (std::size_t i = 0; i < timestamps.size(); ++i) {
      const auto &candidate = timestamps[i];
      if (candidate.matched) {
        continue;
      }

      const std::uint64_t delta =
          absoluteDifference(candidate.user_timestamp, user_timestamp);
      if (delta > alignment_window_us_ || delta >= best_delta) {
        continue;
      }

      best_index = i;
      best_delta = delta;
    }

    return best_index;
  }

  mutable std::mutex mutex_;
  std::deque<TimestampEntry> video_timestamps_;
  std::deque<TimestampEntry> imu_timestamps_;
  std::uint64_t aligned_frame_count_;
  std::uint64_t alignment_window_us_;
  std::size_t history_size_;
};

} // namespace livekit_examples::data_stamping
