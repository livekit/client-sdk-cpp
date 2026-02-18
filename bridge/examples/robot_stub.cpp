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
 * Robot example -- publishes audio and video frames to a LiveKit room.
 *
 * The robot acts as a sensor platform: it streams a camera feed (simulated
 * as a solid-color frame) and microphone audio (simulated as a sine tone)
 * into the room. A "human" participant can subscribe and receive these
 * frames via their own bridge instance.
 *
 * Usage:
 *   robot <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... robot
 *
 * The token must grant identity "robot". Generate one with:
 *   lk token create --api-key <key> --api-secret <secret> \
 *       --join --room my-room --identity robot \
 *       --valid-for 24h
 */

#include "livekit/audio_frame.h"
#include "livekit/track.h"
#include "livekit/video_frame.h"
#include "livekit_bridge/livekit_bridge.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};
static void handleSignal(int) { g_running.store(false); }

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
    std::cerr << "Usage: robot <ws-url> <token>\n"
              << "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... robot\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);

  // ----- Connect -----
  livekit_bridge::LiveKitBridge bridge;
  std::cout << "[robot] Connecting to " << url << " ...\n";
  if (!bridge.connect(url, token)) {
    std::cerr << "[robot] Failed to connect.\n";
    return 1;
  }
  std::cout << "[robot] Connected.\n";

  // ----- Create outgoing tracks -----
  constexpr int kSampleRate = 48000;
  constexpr int kChannels = 1;
  constexpr int kWidth = 640;
  constexpr int kHeight = 480;

  auto mic = bridge.createAudioTrack("robot-mic", kSampleRate, kChannels,
                                     livekit::TrackSource::SOURCE_MICROPHONE);
  auto cam = bridge.createVideoTrack("robot-cam", kWidth, kHeight,
                                     livekit::TrackSource::SOURCE_CAMERA);
  std::cout << "[robot] Publishing audio (" << kSampleRate << " Hz, "
            << kChannels << " ch) and video (" << kWidth << "x" << kHeight
            << ").\n";

  // ----- Prepare frame data -----

  // Audio: 10ms frames of a 440 Hz sine tone so the human can verify
  // it is receiving real (non-silent) data.
  constexpr int kSamplesPerFrame = kSampleRate / 100; // 480 samples = 10ms
  constexpr double kToneHz = 440.0;
  constexpr double kAmplitude = 3000.0; // ~10% of int16 max
  std::vector<std::int16_t> audio_buf(kSamplesPerFrame * kChannels);
  int audio_sample_index = 0;

  // Video: solid green RGBA frame (simulating a "robot camera" view).
  std::vector<std::uint8_t> video_buf(kWidth * kHeight * 4);
  for (int i = 0; i < kWidth * kHeight; ++i) {
    video_buf[i * 4 + 0] = 0;   // R
    video_buf[i * 4 + 1] = 180; // G
    video_buf[i * 4 + 2] = 0;   // B
    video_buf[i * 4 + 3] = 255; // A
  }

  // ----- Stream loop -----
  std::cout << "[robot] Streaming... press Ctrl-C to stop.\n";

  std::int64_t video_ts = 0;
  int loop_count = 0;

  while (g_running.load()) {
    // Generate 10ms of sine tone
    for (int i = 0; i < kSamplesPerFrame; ++i) {
      double t = static_cast<double>(audio_sample_index++) / kSampleRate;
      audio_buf[i] = static_cast<std::int16_t>(
          kAmplitude * std::sin(2.0 * M_PI * kToneHz * t));
    }

    try {
      mic->pushFrame(audio_buf, kSamplesPerFrame);
    } catch (const std::exception &e) {
      std::cerr << "[robot] Audio push error: " << e.what() << "\n";
    }

    // Push video at ~30 fps (every 3rd loop iteration, since loop is 10ms)
    if (++loop_count % 3 == 0) {
      try {
        cam->pushFrame(video_buf, video_ts);
        video_ts += 33333; // ~30 fps in microseconds
      } catch (const std::exception &e) {
        std::cerr << "[robot] Video push error: " << e.what() << "\n";
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // ----- Cleanup -----
  std::cout << "[robot] Shutting down...\n";
  mic.reset();
  cam.reset();
  bridge.disconnect();
  std::cout << "[robot] Done.\n";
  return 0;
}
