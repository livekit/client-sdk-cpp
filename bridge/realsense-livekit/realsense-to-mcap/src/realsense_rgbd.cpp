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
 * publishes RGB as a video track and depth as a DataTrack (foxglove.RawImage proto).
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
#include "foxglove/RawImage.pb.h"

#include <librealsense2/rs.hpp>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
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
  const int kDepthFps = 2;  // data track depth rate (Hz); keep low to avoid saturating SCTP

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
  std::shared_ptr<livekit_bridge::BridgeDataTrack> hello_track;

  try {
    video_track = bridge.createVideoTrack("camera/color", kWidth, kHeight,
                                          livekit::TrackSource::SOURCE_CAMERA);
    depth_track = bridge.createDataTrack("camera/depth");
    hello_track = bridge.createDataTrack("hello");
  } catch (const std::exception& e) {
    std::cerr << "[realsense_rgbd] Failed to create tracks: " << e.what()
              << "\n";
    bridge.disconnect();
    pipe.stop();
    return 1;
  }

  std::cout << "[realsense_rgbd] Publishing camera/color (video), "
               "camera/depth (DataTrack), and hello (DataTrack). "
               "Press Ctrl+C to stop.\n";

  std::vector<std::uint8_t> rgbaBuf(static_cast<std::size_t>(kWidth * kHeight * 4));

  uint32_t hello_seq = 0;
  uint32_t depth_pushed = 0;
  auto last_hello = std::chrono::steady_clock::now();
  auto last_depth = std::chrono::steady_clock::now();
  const auto depth_interval =
      std::chrono::microseconds(1000000 / kDepthFps);

  while (g_running) {
    // Periodic "hello viewer" test message every 10 seconds
    auto now_steady = std::chrono::steady_clock::now();
    if (now_steady - last_hello >= std::chrono::seconds(10)) {
      last_hello = now_steady;
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
      continue;
    }

    auto color = frames.get_color_frame();
    auto depth = frames.get_depth_frame();
    if (!color || !depth) {
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
    if (now_steady - last_depth >= depth_interval) {
      last_depth = now_steady;

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
      bool ok = depth_track->pushFrame(
          reinterpret_cast<const std::uint8_t*>(serialized.data()),
          serialized.size(), static_cast<std::uint64_t>(timestamp_us));
      ++depth_pushed;
      if (!ok) {
        std::cout << "[" << nowStr()
                  << "] [realsense_rgbd] Failed to push depth frame #"
                  << depth_pushed << "\n";
        break;
      }
      if (depth_pushed == 1 || depth_pushed % 10 == 0) {
        std::cout << "[" << nowStr()
                  << "] [realsense_rgbd] Pushed depth frame #" << depth_pushed
                  << " (" << serialized.size() << " bytes)\n";
      }
    }
  }

  std::cout << "[realsense_rgbd] Stopping...\n";
  bridge.disconnect();
  pipe.stop();
  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
