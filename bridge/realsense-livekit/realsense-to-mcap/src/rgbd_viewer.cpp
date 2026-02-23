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
 * rgbd_viewer — LiveKit participant that subscribes to realsense_rgbd's
 * video track (RGB) and data track (depth), and writes both to an MCAP file.
 *
 * Usage:
 *   rgbd_viewer [output.mcap] <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... rgbd_viewer [output.mcap]
 *
 * Token must grant identity "rgbd_viewer". Start realsense_rgbd in the same
 * room first to publish camera/color and camera/depth.
 */

#define MCAP_IMPLEMENTATION
#include <mcap/writer.hpp>

#include "livekit/track.h"
#include "livekit/video_frame.h"
#include "livekit_bridge/livekit_bridge.h"

#include "BuildFileDescriptorSet.h"
#include "foxglove/RawImage.pb.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static volatile std::sig_atomic_t g_running = 1;
static void signalHandler(int) { g_running = 0; }

static std::string nowStr() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&t, &tm);
  std::ostringstream os;
  os << std::put_time(&tm, "%H:%M:%S") << '.' << std::setfill('0')
     << std::setw(3) << ms.count();
  return os.str();
}

static const char kSenderIdentity[] = "realsense_rgbd";
static const char kColorTrackName[] = "camera/color"; // video track
static const char kDepthTrackName[] = "camera/depth"; // data track
static const char kHelloTrackName[] = "hello";        // test data track

/// Thread-safe MCAP writer wrapper for camera/color and camera/depth.
struct McapRecorder {
  mcap::McapWriter writer;
  mcap::ChannelId colorChannelId = 0;
  mcap::ChannelId depthChannelId = 0;
  std::mutex writeMutex;
  uint32_t colorSeq = 0;
  uint32_t depthSeq = 0;
  bool open = false;

  bool openFile(const std::string &path) {
    mcap::McapWriterOptions opts("");
    opts.compression = mcap::Compression::Zstd;
    auto res = writer.open(path, opts);
    if (!res.ok()) {
      std::cerr << "[rgbd_viewer] Failed to open MCAP: " << res.message << "\n";
      return false;
    }

    mcap::Schema schema("foxglove.RawImage", "protobuf",
                        BuildFileDescriptorSet(foxglove::RawImage::descriptor())
                            .SerializeAsString());
    writer.addSchema(schema);

    mcap::Channel colorChannel("camera/color", "protobuf", schema.id);
    writer.addChannel(colorChannel);
    colorChannelId = colorChannel.id;

    mcap::Channel depthChannel("camera/depth", "protobuf", schema.id);
    writer.addChannel(depthChannel);
    depthChannelId = depthChannel.id;

    open = true;
    return true;
  }

  void writeColorFrame(const std::uint8_t *rgba, std::size_t size, int width,
                       int height, std::int64_t timestamp_us) {
    std::lock_guard<std::mutex> lock(writeMutex);
    if (!open)
      return;

    const uint64_t logTime =
        timestamp_us > 0
            ? static_cast<uint64_t>(timestamp_us) * 1000ULL
            : std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();

    foxglove::RawImage msg;
    auto *ts = msg.mutable_timestamp();
    ts->set_seconds(static_cast<int64_t>(logTime / 1000000000ULL));
    ts->set_nanos(static_cast<int32_t>(logTime % 1000000000ULL));
    msg.set_frame_id("camera_color");
    msg.set_width(width);
    msg.set_height(height);
    msg.set_encoding("rgba8");
    msg.set_step(width * 4);
    msg.set_data(rgba, size);

    std::string serialized = msg.SerializeAsString();
    mcap::Message m;
    m.channelId = colorChannelId;
    m.sequence = colorSeq++;
    m.logTime = logTime;
    m.publishTime = logTime;
    m.data = reinterpret_cast<const std::byte *>(serialized.data());
    m.dataSize = serialized.size();
    auto res = writer.write(m);
    if (!res.ok()) {
      std::cerr << "[rgbd_viewer] Write color error: " << res.message << "\n";
    }
  }

  void writeDepthPayload(const std::uint8_t *data, std::size_t size,
                         std::optional<std::uint64_t> user_timestamp_us) {
    std::lock_guard<std::mutex> lock(writeMutex);
    if (!open)
      return;

    uint64_t logTime;
    if (user_timestamp_us && *user_timestamp_us > 0) {
      logTime = *user_timestamp_us * 1000ULL;
    } else {
      logTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
    }

    mcap::Message m;
    m.channelId = depthChannelId;
    m.sequence = depthSeq++;
    m.logTime = logTime;
    m.publishTime = logTime;
    m.data = reinterpret_cast<const std::byte *>(data);
    m.dataSize = size;
    auto res = writer.write(m);
    if (!res.ok()) {
      std::cerr << "[rgbd_viewer] Write depth error: " << res.message << "\n";
    }
  }

  void close() {
    std::lock_guard<std::mutex> lock(writeMutex);
    if (open) {
      writer.close();
      open = false;
    }
  }
};

int main(int argc, char *argv[]) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);

  std::string outputPath = "rgbd_viewer_output.mcap";
  std::string url;
  std::string token;

  const char *env_url = std::getenv("LIVEKIT_URL");
  const char *env_token = std::getenv("LIVEKIT_TOKEN");

  if (argc >= 4) {
    outputPath = argv[1];
    url = argv[2];
    token = argv[3];
  } else if (argc >= 3) {
    url = argv[1];
    token = argv[2];
  } else if (env_url && env_token) {
    url = env_url;
    token = env_token;
    if (argc >= 2) {
      outputPath = argv[1];
    }
  } else {
    std::cerr << "Usage: rgbd_viewer [output.mcap] <ws-url> <token>\n"
                 "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... rgbd_viewer "
                 "[output.mcap]\n";
    return 1;
  }

  std::shared_ptr<McapRecorder> recorder = std::make_shared<McapRecorder>();
  if (!recorder->openFile(outputPath)) {
    return 1;
  }
  std::cout << "[rgbd_viewer] Recording to " << outputPath << "\n";

  std::atomic<uint64_t> depth_frames_received{0};

  livekit_bridge::LiveKitBridge bridge;

  // Video callback: realsense_rgbd's camera (RGB stream as video track)
  bridge.setOnVideoFrameCallback(
      kSenderIdentity, livekit::TrackSource::SOURCE_CAMERA,
      [recorder](const livekit::VideoFrame &frame, std::int64_t timestamp_us) {
        const std::uint8_t *data = frame.data();
        const std::size_t size = frame.dataSize();
        if (data && size > 0) {
          recorder->writeColorFrame(data, size, frame.width(), frame.height(),
                                    timestamp_us);
        }
      });

  // Data callback: realsense_rgbd's camera/depth (RawImage proto bytes)
  bridge.setOnDataFrameCallback(
      kSenderIdentity, kDepthTrackName,
      [recorder,
       &depth_frames_received](const std::vector<std::uint8_t> &payload,
                               std::optional<std::uint64_t> user_timestamp) {
        if (payload.empty())
          return;
        uint64_t n =
            depth_frames_received.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n == 1) {
          std::cout << "[" << nowStr()
                    << "] [rgbd_viewer] First depth frame received ("
                    << payload.size() << " bytes).\n";
        } 
        // else if (n % 30 == 0)
        //   std::cout << "[" << nowStr()
        //             << "] [rgbd_viewer] Depth frames received: " << n << "\n";
        recorder->writeDepthPayload(payload.data(), payload.size(),
                                    user_timestamp);
      });

  // Test callback: realsense_rgbd's "hello" track (plain-text ping)
  bridge.setOnDataFrameCallback(
      kSenderIdentity, kHelloTrackName,
      [](const std::vector<std::uint8_t> &payload,
         std::optional<std::uint64_t> /*user_timestamp*/) {
        std::string text(payload.begin(), payload.end());
        std::cout << "[" << nowStr() << "] [rgbd_viewer] Received hello: \""
                  << text << "\" (" << payload.size() << " bytes)\n";
      });

  std::cout << "[rgbd_viewer] Connecting to " << url << " ...\n";
  livekit::RoomOptions options;
  options.auto_subscribe = true;
  if (!bridge.connect(url, token, options)) {
    std::cerr << "[rgbd_viewer] Failed to connect.\n";
    recorder->close();
    return 1;
  }

  std::cout << "[rgbd_viewer] Connected. Waiting for " << kSenderIdentity
            << " (camera/color + camera/depth). Press Ctrl+C to stop.\n";

  while (g_running && bridge.isConnected()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "[rgbd_viewer] Stopping... (depth frames received: "
            << depth_frames_received.load(std::memory_order_relaxed) << ")\n";
  bridge.disconnect();
  recorder->close();
  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
