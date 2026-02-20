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

/*
 * Human example -- receives audio and video frames from a robot in a
 * LiveKit room and prints a summary each time a frame arrives.
 *
 * This participant does not publish any tracks of its own; it only
 * subscribes to the robot's camera and microphone streams via
 * setOnAudioFrameCallback / setOnVideoFrameCallback.
 *
 * Usage:
 *   human <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... human
 *
 * The token must grant identity "human". Generate one with:
 *   lk token create --api-key <key> --api-secret <secret> \
 *       --join --room my-room --identity human \
 *       --valid-for 24h
 *
 * Run alongside the "robot" example (which publishes with identity "robot").
 */

#include "livekit/audio_frame.h"
#include "livekit/track.h"
#include "livekit/video_frame.h"
#include "livekit_bridge/livekit_bridge.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <thread>

static std::atomic<bool> g_running{true};
static void handleSignal(int) { g_running.store(false); }

// Simple counters for periodic status reporting.
static std::atomic<uint64_t> g_audio_frames{0};
static std::atomic<uint64_t> g_video_frames{0};

int main(int argc, char *argv[]) {
  // ----- Parse args / env -----
  std::string url, token;
  if (argc >= 3) {
    url = argv[1];
    token = argv[2];
  } else {
    const char *e = std::getenv("LIVEKIT_URL");
    if (e)
      url = e;
    e = std::getenv("LIVEKIT_TOKEN");
    if (e)
      token = e;
  }
  if (url.empty() || token.empty()) {
    std::cerr << "Usage: human <ws-url> <token>\n"
              << "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... human\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);

  // ----- Connect -----
  livekit_bridge::LiveKitBridge bridge;
  std::cout << "[human] Connecting to " << url << " ...\n";
  if (!bridge.connect(url, token)) {
    std::cerr << "[human] Failed to connect.\n";
    return 1;
  }
  std::cout << "[human] Connected. Waiting for robot...\n";

  // ----- Register callbacks for the "robot" participant -----
  // These are registered BEFORE the robot joins, so the bridge will
  // automatically wire them up when the robot's tracks are subscribed.

  bridge.setOnAudioFrameCallback(
      "robot", livekit::TrackSource::SOURCE_MICROPHONE,
      [](const livekit::AudioFrame &frame) {
        uint64_t count = g_audio_frames.fetch_add(1) + 1;

        // Print every 100th frame (~1 per second at 10ms frames)
        // to avoid flooding the console.
        if (count % 100 == 1) {
          std::cout << "[human] Audio frame #" << count << ": "
                    << frame.samples_per_channel() << " samples/ch, "
                    << frame.sample_rate() << " Hz, " << frame.num_channels()
                    << " ch, duration=" << std::fixed << std::setprecision(3)
                    << frame.duration() << "s\n";
        }
      });

  bridge.setOnVideoFrameCallback(
      "robot", livekit::TrackSource::SOURCE_CAMERA,
      [](const livekit::VideoFrame &frame, std::int64_t timestamp_us) {
        uint64_t count = g_video_frames.fetch_add(1) + 1;

        // Print every 30th frame (~1 per second at 30 fps).
        if (count % 30 == 1) {
          std::cout << "[human] Video frame #" << count << ": " << frame.width()
                    << "x" << frame.height() << ", " << frame.dataSize()
                    << " bytes, ts=" << timestamp_us << " us\n";
        }
      });

  // ----- Idle loop -----
  // The human has no tracks to publish. Just keep the process alive
  // while the reader threads (created by the bridge on subscription)
  // deliver frames to our callbacks above.
  std::cout << "[human] Listening for robot frames... press Ctrl-C to stop.\n";

  auto last_report = std::chrono::steady_clock::now();

  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Periodic summary every 5 seconds
    auto now = std::chrono::steady_clock::now();
    if (now - last_report >= std::chrono::seconds(5)) {
      last_report = now;
      std::cout << "[human] Status: " << g_audio_frames.load()
                << " audio frames, " << g_video_frames.load()
                << " video frames received so far.\n";
    }
  }

  // ----- Cleanup -----
  std::cout << "[human] Shutting down...\n";
  std::cout << "[human] Total received: " << g_audio_frames.load()
            << " audio frames, " << g_video_frames.load() << " video frames.\n";
  bridge.disconnect();
  std::cout << "[human] Done.\n";
  return 0;
}
