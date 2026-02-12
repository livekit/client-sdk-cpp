// Copyright 2026 LiveKit, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
 * realsense_rgbd — LiveKit participant that captures RealSense RGB+D frames,
 * publishes RGB as a video track, depth as a DataTrack (foxglove.RawImage),
 * and IMU-derived orientation as a DataTrack (foxglove.PoseInFrame).
 *
 * If the RealSense device has an IMU (e.g. D435i), gyroscope and
 * accelerometer data are fused via a complementary filter to produce a
 * camera/pose topic. Devices without an IMU skip pose publishing.
 *
 * Usage:
 *   realsense_rgbd <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... realsense_rgbd
 *
 * Token must grant identity "realsense_rgbd". Run rgbd_viewer in the same room
 * to receive and record to MCAP.
 */

#include "livekit_bridge/bridge_data_track.h"
#include "livekit_bridge/livekit_bridge.h"
#include "livekit/track.h"

#include "BuildFileDescriptorSet.h"
#include "foxglove/PoseInFrame.pb.h"
#include "foxglove/RawImage.pb.h"

#include <librealsense2/rs.hpp>

#include <zlib.h>

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static volatile std::sig_atomic_t g_running = 1;
static void signalHandler(int) { g_running = 0; }

static uint64_t nowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

static std::string nowStr() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&t, &tm);
  std::ostringstream os;
  os << std::put_time(&tm, "%H:%M:%S") << '.' << std::setfill('0')
     << std::setw(3) << ms.count();
  return os.str();
}

struct OrientationQuat {
  double x, y, z, w;
};

/// Convert intrinsic XYZ Euler angles (radians) to a unit quaternion.
static OrientationQuat eulerToQuaternion(double x, double y, double z) {
  double cx = std::cos(x / 2), sx = std::sin(x / 2);
  double cy = std::cos(y / 2), sy = std::sin(y / 2);
  double cz = std::cos(z / 2), sz = std::sin(z / 2);
  return {
      sx * cy * cz + cx * sy * sz,
      cx * sy * cz - sx * cy * sz,
      cx * cy * sz + sx * sy * cz,
      cx * cy * cz - sx * sy * sz,
  };
}

/// Fuse gyroscope + accelerometer into an orientation estimate using a
/// complementary filter. Adapted from the librealsense motion example.
class RotationEstimator {
public:
  void process_gyro(rs2_vector gyro_data, double ts) {
    if (first_gyro_) {
      first_gyro_ = false;
      last_ts_gyro_ = ts;
      return;
    }
    float dt = static_cast<float>((ts - last_ts_gyro_) / 1000.0);
    last_ts_gyro_ = ts;

    std::lock_guard<std::mutex> lock(mtx_);
    theta_x_ += -gyro_data.z * dt;
    theta_y_ += -gyro_data.y * dt;
    theta_z_ += gyro_data.x * dt;
  }

  void process_accel(rs2_vector accel_data) {
    float accel_angle_x = std::atan2(accel_data.x,
        std::sqrt(accel_data.y * accel_data.y +
                  accel_data.z * accel_data.z));
    float accel_angle_z = std::atan2(accel_data.y, accel_data.z);

    std::lock_guard<std::mutex> lock(mtx_);
    if (first_accel_) {
      first_accel_ = false;
      theta_x_ = accel_angle_x;
      theta_y_ = static_cast<float>(M_PI);
      theta_z_ = accel_angle_z;
      return;
    }
    theta_x_ = theta_x_ * kAlpha + accel_angle_x * (1.0f - kAlpha);
    theta_z_ = theta_z_ * kAlpha + accel_angle_z * (1.0f - kAlpha);
  }

  OrientationQuat get_orientation() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return eulerToQuaternion(
        static_cast<double>(theta_x_),
        static_cast<double>(theta_y_),
        static_cast<double>(theta_z_));
  }

private:
  static constexpr float kAlpha = 0.98f;
  mutable std::mutex mtx_;
  float theta_x_ = 0, theta_y_ = 0, theta_z_ = 0;
  bool first_gyro_ = true;
  bool first_accel_ = true;
  double last_ts_gyro_ = 0;
};

/// Convert RGB8 to RGBA (alpha = 0xFF). Assumes dst has size width*height*4.
static void rgb8ToRgba(const std::uint8_t* rgb, std::uint8_t* rgba,
                       int width, int height) {
  const int rgbStep = width * 3;
  const int rgbaStep = width * 4;
  for (int y = 0; y < height; ++y) {
    const std::uint8_t* src = rgb + y * rgbStep;
    std::uint8_t* dst = rgba + y * rgbaStep;
    for (int x = 0; x < width; ++x) {
      *dst++ = *src++;
      *dst++ = *src++;
      *dst++ = *src++;
      *dst++ = 0xFF;
    }
  }
}

int main(int argc, char* argv[]) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);

  std::string url;
  std::string token;
  const char* env_url = std::getenv("LIVEKIT_URL");
  const char* env_token = std::getenv("LIVEKIT_TOKEN");
  if (argc >= 3) {
    url = argv[1];
    token = argv[2];
  } else if (env_url && env_token) {
    url = env_url;
    token = env_token;
  } else {
    std::cerr << "Usage: realsense_rgbd <ws-url> <token>\n"
                 "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... realsense_rgbd\n";
    return 1;
  }

  const int kWidth = 640;
  const int kHeight = 480;
  const int kDepthFps = 10;  // data track depth rate (Hz); limited by SCTP throughput
  const int kPoseFps = 30;

  RotationEstimator rotation_est;

  // Check for IMU support (following rs-motion.cpp pattern).
  bool imu_supported = false;
  {
    rs2::context ctx;
    auto devices = ctx.query_devices();
    for (auto dev : devices) {
      bool found_gyro = false, found_accel = false;
      for (auto sensor : dev.query_sensors()) {
        for (auto profile : sensor.get_stream_profiles()) {
          if (profile.stream_type() == RS2_STREAM_GYRO)
            found_gyro = true;
          if (profile.stream_type() == RS2_STREAM_ACCEL)
            found_accel = true;
        }
      }
      if (found_gyro && found_accel) {
        imu_supported = true;
        break;
      }
    }
  }

  // Color+depth pipeline.
  rs2::pipeline pipe;
  rs2::config cfg;
  cfg.enable_stream(RS2_STREAM_COLOR, kWidth, kHeight, RS2_FORMAT_RGB8, 30);
  cfg.enable_stream(RS2_STREAM_DEPTH, kWidth, kHeight, RS2_FORMAT_Z16, 30);
  try {
    pipe.start(cfg);
  } catch (const rs2::error& e) {
    std::cerr << "RealSense error: " << e.what() << "\n";
    return 1;
  }

  // Separate IMU-only pipeline with callback, mirroring rs-motion.cpp.
  // A dedicated pipeline for gyro+accel avoids interfering with the
  // color+depth pipeline's frame syncer.
  rs2::pipeline imu_pipe;
  bool has_imu = false;
  if (imu_supported) {
    rs2::config imu_cfg;
    imu_cfg.enable_stream(RS2_STREAM_ACCEL, RS2_FORMAT_MOTION_XYZ32F);
    imu_cfg.enable_stream(RS2_STREAM_GYRO, RS2_FORMAT_MOTION_XYZ32F);
    try {
      imu_pipe.start(imu_cfg, [&rotation_est](rs2::frame frame) {
        auto motion = frame.as<rs2::motion_frame>();
        if (motion &&
            motion.get_profile().stream_type() == RS2_STREAM_GYRO &&
            motion.get_profile().format() == RS2_FORMAT_MOTION_XYZ32F) {
          rotation_est.process_gyro(motion.get_motion_data(),
                                    motion.get_timestamp());
        }
        if (motion &&
            motion.get_profile().stream_type() == RS2_STREAM_ACCEL &&
            motion.get_profile().format() == RS2_FORMAT_MOTION_XYZ32F) {
          rotation_est.process_accel(motion.get_motion_data());
        }
      });
      has_imu = true;
      std::cout << "[realsense_rgbd] IMU pipeline started.\n";
    } catch (const rs2::error& e) {
      std::cerr << "[realsense_rgbd] Could not start IMU pipeline: "
                << e.what() << " — continuing without pose.\n";
    }
  } else {
    std::cout << "[realsense_rgbd] IMU not available, continuing without "
                 "pose.\n";
  }

  livekit_bridge::LiveKitBridge bridge;
  livekit::RoomOptions options;
  options.auto_subscribe = false;

  std::cout << "[realsense_rgbd] Connecting to " << url << " ...\n";
  if (!bridge.connect(url, token, options)) {
    std::cerr << "[realsense_rgbd] Failed to connect.\n";
    pipe.stop();
    return 1;
  }

  std::shared_ptr<livekit_bridge::BridgeVideoTrack> video_track;
  std::shared_ptr<livekit_bridge::BridgeDataTrack> depth_track;
  std::shared_ptr<livekit_bridge::BridgeDataTrack> pose_track;
  std::shared_ptr<livekit_bridge::BridgeDataTrack> hello_track;

  try {
    video_track = bridge.createVideoTrack("camera/color", kWidth, kHeight,
                                          livekit::TrackSource::SOURCE_CAMERA);
    depth_track = bridge.createDataTrack("camera/depth");
    if (has_imu) {
      pose_track = bridge.createDataTrack("camera/pose");
    }
    hello_track = bridge.createDataTrack("hello");
  } catch (const std::exception& e) {
    std::cerr << "[realsense_rgbd] Failed to create tracks: " << e.what()
              << "\n";
    bridge.disconnect();
    pipe.stop();
    return 1;
  }

  std::cout << "[realsense_rgbd] Publishing camera/color (video), "
               "camera/depth (DataTrack)"
            << (has_imu ? ", camera/pose (DataTrack)" : "")
            << ", and hello (DataTrack). Press Ctrl+C to stop.\n";

  std::vector<std::uint8_t> rgbaBuf(static_cast<std::size_t>(kWidth * kHeight * 4));

  uint32_t hello_seq = 0;
  uint32_t depth_pushed = 0;
  uint32_t pose_pushed = 0;
  uint32_t last_depth_report_count = 0;
  auto last_hello = std::chrono::steady_clock::now();
  auto last_depth = std::chrono::steady_clock::now();
  auto last_pose = std::chrono::steady_clock::now();
  auto last_depth_report = std::chrono::steady_clock::now();
  const auto depth_interval =
      std::chrono::microseconds(1000000 / kDepthFps);
  const auto pose_interval =
      std::chrono::microseconds(1000000 / kPoseFps);

  constexpr auto kMinLoopDuration = std::chrono::milliseconds(15);

  while (g_running) {
    auto loop_start = std::chrono::steady_clock::now();

    // Periodic "hello viewer" test message every 10 seconds
    if (loop_start - last_hello >= std::chrono::seconds(1)) {
      last_hello = loop_start;
      ++hello_seq;
      std::string text = "hello viewer #" + std::to_string(hello_seq);
      uint64_t ts_us = static_cast<uint64_t>(nowNs() / 1000);
      bool ok = hello_track->pushFrame(
          reinterpret_cast<const std::uint8_t*>(text.data()), text.size(),
          ts_us);
      std::cout << "[" << nowStr() << "] [realsense_rgbd] Sent hello #"
                << hello_seq << " (" << text.size() << " bytes) -> "
                << (ok ? "ok" : "FAILED") << "\n";
    }

    rs2::frameset frames;
    if (!pipe.poll_for_frames(&frames)) {
      auto elapsed = std::chrono::steady_clock::now() - loop_start;
      if (elapsed < kMinLoopDuration)
        std::this_thread::sleep_for(kMinLoopDuration - elapsed);
      continue;
    }

    auto color = frames.get_color_frame();
    auto depth = frames.get_depth_frame();
    if (!color || !depth) {
      auto elapsed = std::chrono::steady_clock::now() - loop_start;
      if (elapsed < kMinLoopDuration)
        std::this_thread::sleep_for(kMinLoopDuration - elapsed);
      continue;
    }

    const uint64_t timestamp_ns = nowNs();
    const std::int64_t timestamp_us =
        static_cast<std::int64_t>(timestamp_ns / 1000);
    const int64_t secs = static_cast<int64_t>(timestamp_ns / 1000000000ULL);
    const int32_t nsecs = static_cast<int32_t>(timestamp_ns % 1000000000ULL);

    // RGB → RGBA and push to video track
    rgb8ToRgba(static_cast<const std::uint8_t*>(color.get_data()),
               rgbaBuf.data(), kWidth, kHeight);
    if (!video_track->pushFrame(rgbaBuf.data(), rgbaBuf.size(), timestamp_us)) {
      break;
    }

    // Depth as RawImage proto on DataTrack (throttled to kDepthFps)
    if (loop_start - last_depth >= depth_interval) {
      last_depth = loop_start;

      foxglove::RawImage msg;
      auto* ts = msg.mutable_timestamp();
      ts->set_seconds(secs);
      ts->set_nanos(nsecs);
      msg.set_frame_id("camera_depth");
      msg.set_width(depth.get_width());
      msg.set_height(depth.get_height());
      msg.set_encoding("16UC1");
      msg.set_step(depth.get_width() * 2);
      msg.set_data(depth.get_data(), depth.get_data_size());

      std::string serialized = msg.SerializeAsString();

      uLongf comp_bound = compressBound(static_cast<uLong>(serialized.size()));
      std::vector<std::uint8_t> compressed(comp_bound);
      uLongf comp_size = comp_bound;
      int zrc = compress2(
          compressed.data(), &comp_size,
          reinterpret_cast<const Bytef*>(serialized.data()),
          static_cast<uLong>(serialized.size()), Z_BEST_SPEED);

      auto push_start = std::chrono::steady_clock::now();
      bool ok = false;
      if (zrc == Z_OK) {
        ok = depth_track->pushFrame(
            compressed.data(), static_cast<std::size_t>(comp_size),
            static_cast<std::uint64_t>(timestamp_us));
      } else {
        std::cerr << "[realsense_rgbd] zlib compress failed (" << zrc
                  << "), sending uncompressed\n";
        ok = depth_track->pushFrame(
            reinterpret_cast<const std::uint8_t*>(serialized.data()),
            serialized.size(), static_cast<std::uint64_t>(timestamp_us));
      }
      auto push_dur = std::chrono::steady_clock::now() - push_start;
      double push_ms =
          std::chrono::duration_cast<std::chrono::microseconds>(push_dur)
              .count() / 1000.0;

      ++depth_pushed;
      if (!ok) {
        std::cout << "[" << nowStr()
                  << "] [realsense_rgbd] Failed to push depth frame #"
                  << depth_pushed << " (push took " << std::fixed
                  << std::setprecision(1) << push_ms << "ms)\n";
        break;
      }
      if (depth_pushed == 1 || depth_pushed % 10 == 0) {
        double elapsed_sec =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                loop_start - last_depth_report)
                .count() / 1000.0;
        double actual_fps =
            (elapsed_sec > 0)
                ? static_cast<double>(depth_pushed - last_depth_report_count) /
                      elapsed_sec
                : 0;
        std::cout << "[" << nowStr()
                  << "] [realsense_rgbd] Depth #" << depth_pushed
                  << " push=" << std::fixed << std::setprecision(1) << push_ms
                  << "ms " << serialized.size() << "B->"
                  << (zrc == Z_OK ? comp_size : serialized.size()) << "B"
                  << " actual=" << std::setprecision(1) << actual_fps
                  << "fps\n";
        last_depth_report = loop_start;
        last_depth_report_count = depth_pushed;
      }
    }

    // Pose as PoseInFrame proto on DataTrack (throttled to kPoseFps)
    if (has_imu && pose_track && (loop_start - last_pose >= pose_interval)) {
      last_pose = loop_start;

      auto orientation = rotation_est.get_orientation();

      foxglove::PoseInFrame pose_msg;
      auto* pose_ts = pose_msg.mutable_timestamp();
      pose_ts->set_seconds(secs);
      pose_ts->set_nanos(nsecs);
      pose_msg.set_frame_id("camera_imu");

      auto* pose = pose_msg.mutable_pose();
      auto* pos = pose->mutable_position();
      pos->set_x(0);
      pos->set_y(0);
      pos->set_z(0);
      auto* orient = pose->mutable_orientation();
      orient->set_x(orientation.x);
      orient->set_y(orientation.y);
      orient->set_z(orientation.z);
      orient->set_w(orientation.w);

      std::string pose_serialized = pose_msg.SerializeAsString();
      bool pose_ok = pose_track->pushFrame(
          reinterpret_cast<const std::uint8_t*>(pose_serialized.data()),
          pose_serialized.size(), static_cast<std::uint64_t>(timestamp_us));
      ++pose_pushed;
      if (!pose_ok) {
        std::cout << "[" << nowStr()
                  << "] [realsense_rgbd] Failed to push pose frame #"
                  << pose_pushed << "\n";
      }
      if (pose_pushed == 1 || pose_pushed % 100 == 0) {
        std::cout << "[" << nowStr()
                  << "] [realsense_rgbd] Pose #" << pose_pushed
                  << " " << pose_serialized.size() << "B"
                  << " q=(" << std::fixed << std::setprecision(3)
                  << orientation.x << ", " << orientation.y << ", "
                  << orientation.z << ", " << orientation.w << ")\n";
      }
    }

    auto elapsed = std::chrono::steady_clock::now() - loop_start;
    if (elapsed < kMinLoopDuration) {
      std::this_thread::sleep_for(kMinLoopDuration - elapsed);
    }
  }

  std::cout << "[realsense_rgbd] Stopping...\n";
  bridge.disconnect();
  if (has_imu) imu_pipe.stop();
  pipe.stop();
  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
