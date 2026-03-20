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

#define MCAP_IMPLEMENTATION
#include <mcap/writer.hpp>

#include <librealsense2/rs.hpp>

#include "BuildFileDescriptorSet.h"
#include "foxglove/RawImage.pb.h"

#include <chrono>
#include <csignal>
#include <iostream>

static volatile std::sig_atomic_t g_running = 1;

static void signalHandler(int signum) {
  (void)signum;
  g_running = 0;
}

static uint64_t nowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
           std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int main() {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);

  rs2::pipeline pipe;
  rs2::config cfg;
  cfg.enable_stream(RS2_STREAM_COLOR, 640, 480, RS2_FORMAT_RGB8, 30);
  cfg.enable_stream(RS2_STREAM_DEPTH, 640, 480, RS2_FORMAT_Z16, 30);
  pipe.start(cfg);

  mcap::McapWriter writer;
  auto options = mcap::McapWriterOptions("");
  options.compression = mcap::Compression::Zstd;

  {
    const auto res = writer.open("realsense_rgbd.mcap", options);
    if (!res.ok()) {
      std::cerr << "Failed to open MCAP file: " << res.message << std::endl;
      return 1;
    }
  }

  // Register schema as a serialized FileDescriptorSet (required by MCAP protobuf profile)
  mcap::Schema schema(
    "foxglove.RawImage", "protobuf",
    BuildFileDescriptorSet(foxglove::RawImage::descriptor()).SerializeAsString());
  writer.addSchema(schema);

  mcap::Channel colorChannel("camera/color", "protobuf", schema.id);
  writer.addChannel(colorChannel);

  mcap::Channel depthChannel("camera/depth", "protobuf", schema.id);
  writer.addChannel(depthChannel);

  std::cout << "Recording to realsense_rgbd.mcap ... Press Ctrl+C to stop.\n";

  uint32_t seq = 0;
  while (g_running) {
    rs2::frameset frames;
    if (!pipe.poll_for_frames(&frames)) {
      continue;
    }

    auto color = frames.get_color_frame();
    auto depth = frames.get_depth_frame();
    if (!color || !depth) {
      continue;
    }

    uint64_t timestamp = nowNs();
    int64_t secs = static_cast<int64_t>(timestamp / 1000000000ULL);
    int32_t nsecs = static_cast<int32_t>(timestamp % 1000000000ULL);

    // Color image
    {
      foxglove::RawImage msg;
      auto* ts = msg.mutable_timestamp();
      ts->set_seconds(secs);
      ts->set_nanos(nsecs);
      msg.set_frame_id("camera_color");
      msg.set_width(color.get_width());
      msg.set_height(color.get_height());
      msg.set_encoding("rgb8");
      msg.set_step(color.get_width() * 3);
      msg.set_data(color.get_data(), color.get_data_size());

      std::string serialized = msg.SerializeAsString();

      mcap::Message mcapMsg;
      mcapMsg.channelId = colorChannel.id;
      mcapMsg.sequence = seq;
      mcapMsg.logTime = timestamp;
      mcapMsg.publishTime = timestamp;
      mcapMsg.data = reinterpret_cast<const std::byte*>(serialized.data());
      mcapMsg.dataSize = serialized.size();
      const auto res = writer.write(mcapMsg);
      if (!res.ok()) {
        std::cerr << "Failed to write color message: " << res.message << std::endl;
      }
    }

    // Depth image
    {
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

      mcap::Message mcapMsg;
      mcapMsg.channelId = depthChannel.id;
      mcapMsg.sequence = seq;
      mcapMsg.logTime = timestamp;
      mcapMsg.publishTime = timestamp;
      mcapMsg.data = reinterpret_cast<const std::byte*>(serialized.data());
      mcapMsg.dataSize = serialized.size();
      const auto res = writer.write(mcapMsg);
      if (!res.ok()) {
        std::cerr << "Failed to write depth message: " << res.message << std::endl;
      }
    }

    ++seq;
  }

  std::cout << "\nStopping... wrote " << seq << " frame pairs.\n";
  writer.close();
  pipe.stop();
  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
