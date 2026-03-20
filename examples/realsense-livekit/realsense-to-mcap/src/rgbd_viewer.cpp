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

#include <zlib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
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

/// MCAP writer with a background write thread.
///
/// All public write methods enqueue entries and return immediately so reader
/// callbacks are never blocked by Zstd compression or disk I/O.  A dedicated
/// writer thread drains the queue and performs the actual MCAP writes.
struct McapRecorder {
  mcap::McapWriter writer;
  mcap::ChannelId colorChannelId = 0;
  mcap::ChannelId depthChannelId = 0;
  mcap::ChannelId helloChannelId = 0;
  uint32_t colorSeq = 0;
  uint32_t depthSeq = 0;
  uint32_t helloSeq = 0;
  bool open = false;

  struct WriteEntry {
    enum Type { kColor, kDepth, kHello } type;
    std::vector<std::uint8_t> data;
    std::string text;
    int width = 0;
    int height = 0;
    std::optional<std::uint64_t> user_timestamp_us;
    uint64_t wall_time_ns = 0;
  };

  std::deque<WriteEntry> queue_;
  std::mutex queue_mtx_;
  std::condition_variable queue_cv_;
  std::thread writer_thread_;
  std::atomic<bool> stop_{false};
  std::atomic<uint32_t> color_enqueue_seq_{0};

  std::atomic<uint64_t> depth_received{0};
  std::atomic<uint64_t> hello_received{0};
  std::atomic<int64_t> last_depth_latency_us{-1};
  std::atomic<int64_t> last_hello_latency_us{-1};

  static constexpr size_t kMaxQueueSize = 60;

  static uint64_t wallNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  bool openFile(const std::string &path) {
    mcap::McapWriterOptions opts("");
    opts.compression = mcap::Compression::Zstd;
    auto res = writer.open(path, opts);
    if (!res.ok()) {
      std::cerr << "[rgbd_viewer] Failed to open MCAP: " << res.message << "\n";
      return false;
    }

    mcap::Schema rawImageSchema(
        "foxglove.RawImage", "protobuf",
        BuildFileDescriptorSet(foxglove::RawImage::descriptor())
            .SerializeAsString());
    writer.addSchema(rawImageSchema);

    mcap::Schema textSchema("hello", "jsonschema",
                            R"({"type":"object","properties":{"text":{"type":"string"}}})");
    writer.addSchema(textSchema);

    mcap::Channel colorChannel("camera/color", "protobuf", rawImageSchema.id);
    writer.addChannel(colorChannel);
    colorChannelId = colorChannel.id;

    mcap::Channel depthChannel("camera/depth", "protobuf", rawImageSchema.id);
    writer.addChannel(depthChannel);
    depthChannelId = depthChannel.id;

    mcap::Channel helloChannel("hello", "json", textSchema.id);
    writer.addChannel(helloChannel);
    helloChannelId = helloChannel.id;

    open = true;
    writer_thread_ = std::thread([this] { writerLoop(); });
    return true;
  }

  // Throttle color to ~10fps to reduce memory/write pressure (video arrives
  // at 30fps but each RGBA frame is ~1.2 MB before Zstd).
  void writeColorFrame(const std::uint8_t *rgba, std::size_t size, int width,
                       int height, std::int64_t /*timestamp_us*/) {
    uint32_t seq = color_enqueue_seq_.fetch_add(1, std::memory_order_relaxed);
    if (seq % 3 != 0) return;

    WriteEntry e;
    e.type = WriteEntry::kColor;
    e.data.assign(rgba, rgba + size);
    e.width = width;
    e.height = height;
    e.wall_time_ns = wallNs();
    enqueue(std::move(e));
  }

  void writeDepthPayload(const std::uint8_t *data, std::size_t size,
                         std::optional<std::uint64_t> user_timestamp_us) {
    WriteEntry e;
    e.type = WriteEntry::kDepth;
    e.data.assign(data, data + size);
    e.user_timestamp_us = user_timestamp_us;
    e.wall_time_ns = wallNs();
    enqueue(std::move(e));
  }

  void writeHello(const std::string &text,
                  std::optional<std::uint64_t> user_timestamp_us) {
    WriteEntry e;
    e.type = WriteEntry::kHello;
    e.text = text;
    e.user_timestamp_us = user_timestamp_us;
    e.wall_time_ns = wallNs();
    enqueue(std::move(e));
  }

  void close() {
    stop_.store(true, std::memory_order_release);
    queue_cv_.notify_one();
    if (writer_thread_.joinable())
      writer_thread_.join();
    if (open) {
      writer.close();
      open = false;
    }
  }

private:
  void enqueue(WriteEntry &&e) {
    std::lock_guard<std::mutex> lock(queue_mtx_);
    if (queue_.size() >= kMaxQueueSize) {
      for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (it->type == WriteEntry::kColor) {
          queue_.erase(it);
          break;
        }
      }
    }
    queue_.push_back(std::move(e));
  }

  void writerLoop() {
    while (true) {
      {
        std::unique_lock<std::mutex> lock(queue_mtx_);
        queue_cv_.wait_for(lock, std::chrono::seconds(1), [this] {
          return stop_.load(std::memory_order_acquire);
        });
      }

      std::deque<WriteEntry> batch;
      {
        std::lock_guard<std::mutex> lock(queue_mtx_);
        batch.swap(queue_);
      }

      uint32_t nc = 0, nd = 0, nh = 0;
      for (auto &e : batch) {
        switch (e.type) {
        case WriteEntry::kColor:
          flushColor(e);
          ++nc;
          break;
        case WriteEntry::kDepth:
          flushDepth(e);
          ++nd;
          break;
        case WriteEntry::kHello:
          flushHello(e);
          ++nh;
          break;
        }
      }

      if (nc + nd + nh > 0) {
        auto cr = color_enqueue_seq_.load(std::memory_order_relaxed);
        auto dr = depth_received.load(std::memory_order_relaxed);
        auto hr = hello_received.load(std::memory_order_relaxed);
        auto dl = last_depth_latency_us.load(std::memory_order_relaxed);
        auto hl = last_hello_latency_us.load(std::memory_order_relaxed);
        std::cout << "[" << nowStr() << "] [rgbd_viewer] wrote "
                  << nc << "c " << nd << "d " << nh << "h"
                  << " | totals " << cr << "c " << dr << "d " << hr << "h";
        if (dl >= 0)
          std::cout << " | depth_lat=" << std::fixed << std::setprecision(1)
                    << dl / 1000.0 << "ms";
        if (hl >= 0)
          std::cout << " | hello_lat=" << std::fixed << std::setprecision(1)
                    << hl / 1000.0 << "ms";
        std::cout << "\n";
      }

      if (stop_.load(std::memory_order_acquire) && batch.empty())
        break;
    }
  }

  void flushColor(const WriteEntry &e) {
    foxglove::RawImage msg;
    auto *ts = msg.mutable_timestamp();
    ts->set_seconds(static_cast<int64_t>(e.wall_time_ns / 1000000000ULL));
    ts->set_nanos(static_cast<int32_t>(e.wall_time_ns % 1000000000ULL));
    msg.set_frame_id("camera_color");
    msg.set_width(e.width);
    msg.set_height(e.height);
    msg.set_encoding("rgba8");
    msg.set_step(e.width * 4);
    msg.set_data(e.data.data(), e.data.size());

    std::string serialized = msg.SerializeAsString();
    mcap::Message m;
    m.channelId = colorChannelId;
    m.sequence = colorSeq++;
    m.logTime = e.wall_time_ns;
    m.publishTime = e.wall_time_ns;
    m.data = reinterpret_cast<const std::byte *>(serialized.data());
    m.dataSize = serialized.size();
    auto res = writer.write(m);
    if (!res.ok()) {
      std::cerr << "[rgbd_viewer] Write color error: " << res.message << "\n";
    }
  }

  void flushDepth(const WriteEntry &e) {
    const uint64_t publishTime =
        (e.user_timestamp_us && *e.user_timestamp_us > 0)
            ? *e.user_timestamp_us * 1000ULL
            : e.wall_time_ns;

    // Depth payloads arrive zlib-compressed from the sender.  Decompress
    // so the MCAP file contains standard foxglove.RawImage protobuf bytes.
    const std::uint8_t *write_ptr = e.data.data();
    std::size_t write_size = e.data.size();

    // 640*480*2 (depth) + proto overhead ≈ 620 KB; 1 MB is a safe upper bound.
    static constexpr uLongf kMaxDecompressed = 1024 * 1024;
    std::vector<std::uint8_t> decompressed(kMaxDecompressed);
    uLongf decompressed_size = kMaxDecompressed;
    int zrc = uncompress(decompressed.data(), &decompressed_size,
                         e.data.data(),
                         static_cast<uLong>(e.data.size()));
    if (zrc == Z_OK) {
      write_ptr = decompressed.data();
      write_size = static_cast<std::size_t>(decompressed_size);
    }

    mcap::Message m;
    m.channelId = depthChannelId;
    m.sequence = depthSeq++;
    m.logTime = e.wall_time_ns;
    m.publishTime = publishTime;
    m.data = reinterpret_cast<const std::byte *>(write_ptr);
    m.dataSize = write_size;
    auto res = writer.write(m);
    if (!res.ok()) {
      std::cerr << "[rgbd_viewer] Write depth error: " << res.message << "\n";
    }
  }

  void flushHello(const WriteEntry &e) {
    const uint64_t publishTime =
        (e.user_timestamp_us && *e.user_timestamp_us > 0)
            ? *e.user_timestamp_us * 1000ULL
            : e.wall_time_ns;

    std::string json = "{\"text\":\"" + e.text + "\"}";
    mcap::Message m;
    m.channelId = helloChannelId;
    m.sequence = helloSeq++;
    m.logTime = e.wall_time_ns;
    m.publishTime = publishTime;
    m.data = reinterpret_cast<const std::byte *>(json.data());
    m.dataSize = json.size();
    auto res = writer.write(m);
    if (!res.ok()) {
      std::cerr << "[rgbd_viewer] Write hello error: " << res.message << "\n";
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
      [recorder](const std::vector<std::uint8_t> &payload,
                 std::optional<std::uint64_t> user_timestamp) {
        if (payload.empty())
          return;
        recorder->depth_received.fetch_add(1, std::memory_order_relaxed);
        if (user_timestamp) {
          uint64_t recv_us =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
          recorder->last_depth_latency_us.store(
              static_cast<int64_t>(recv_us - *user_timestamp),
              std::memory_order_relaxed);
        }
        recorder->writeDepthPayload(payload.data(), payload.size(),
                                    user_timestamp);
      });

  // Test callback: realsense_rgbd's "hello" track (plain-text ping)
  bridge.setOnDataFrameCallback(
      kSenderIdentity, kHelloTrackName,
      [recorder](const std::vector<std::uint8_t> &payload,
                 std::optional<std::uint64_t> user_timestamp) {
        recorder->hello_received.fetch_add(1, std::memory_order_relaxed);
        if (user_timestamp) {
          uint64_t recv_us =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
          recorder->last_hello_latency_us.store(
              static_cast<int64_t>(recv_us - *user_timestamp),
              std::memory_order_relaxed);
        }
        std::string text(payload.begin(), payload.end());
        recorder->writeHello(text, user_timestamp);
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

  std::cout << "[rgbd_viewer] Stopping... (depth: "
            << recorder->depth_received.load(std::memory_order_relaxed)
            << ", hello: "
            << recorder->hello_received.load(std::memory_order_relaxed)
            << ")\n";
  bridge.disconnect();
  recorder->close();
  google::protobuf::ShutdownProtobufLibrary();
  return 0;
}
