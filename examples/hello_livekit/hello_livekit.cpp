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

/// Combined hello_livekit example: instantiates both a sender and a receiver
/// peer in the same process on separate threads.
///
/// The sender publishes synthetic RGBA video and a data track. The receiver
/// subscribes and logs every 10th video frame plus every data message.
///
/// Usage:
///   HelloLivekit <ws-url> <sender-token> <receiver-token>
///
/// Or via environment variables:
///   LIVEKIT_URL, LIVEKIT_SENDER_TOKEN, LIVEKIT_RECEIVER_TOKEN

#include "livekit/livekit.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <thread>

using namespace livekit;

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr const char *kVideoTrackName = "camera0";
constexpr const char *kDataTrackName = "app-data";

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char *name) {
  const char *v = std::getenv(name);
  return v ? std::string(v) : std::string{};
}

// ---------------------------------------------------------------------------
// Sender peer
// ---------------------------------------------------------------------------
void runSender(const std::string &url, const std::string &token,
               std::atomic<std::string *> &sender_identity_out) {
  auto room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  if (!room->Connect(url, token, options)) {
    LK_LOG_ERROR("[sender] Failed to connect");
    return;
  }

  LocalParticipant *lp = room->localParticipant();
  assert(lp);

  auto *identity = new std::string(lp->identity());
  sender_identity_out.store(identity);

  LK_LOG_INFO("[sender] Connected as identity='{}' room='{}'", lp->identity(),
              room->room_info().name);

  auto video_source = std::make_shared<VideoSource>(kWidth, kHeight);

  std::shared_ptr<LocalVideoTrack> video_track = lp->publishVideoTrack(
      kVideoTrackName, video_source, TrackSource::SOURCE_CAMERA);

  std::shared_ptr<LocalDataTrack> data_track =
      lp->publishDataTrack(kDataTrackName);

  const auto t0 = std::chrono::steady_clock::now();
  std::uint64_t count = 0;

  LK_LOG_INFO(
      "[sender] Publishing synthetic video + data on '{}'; Ctrl-C to exit",
      kDataTrackName);

  while (g_running.load()) {
    VideoFrame vf = VideoFrame::create(kWidth, kHeight, VideoBufferType::RGBA);
    video_source->captureFrame(std::move(vf));

    const auto now = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(now - t0).count();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << ms << " ms, count: " << count;
    const std::string msg = oss.str();
    data_track->tryPush(std::vector<std::uint8_t>(msg.begin(), msg.end()));

    ++count;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  LK_LOG_INFO("[sender] Disconnecting");
  room.reset();
}

// ---------------------------------------------------------------------------
// Receiver peer
// ---------------------------------------------------------------------------
void runReceiver(const std::string &url, const std::string &token,
                 std::atomic<std::string *> &sender_identity_out) {
  // Wait for the sender to connect so we know its identity.
  std::string sender_identity;
  while (g_running.load()) {
    std::string *id = sender_identity_out.load();
    if (id) {
      sender_identity = *id;
      break;
    }
    LK_LOG_INFO("[receiver] Waiting for sender to connect");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (!g_running.load())
    return;

  auto room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  if (!room->Connect(url, token, options)) {
    LK_LOG_ERROR("[receiver] Failed to connect");
    return;
  }

  LocalParticipant *lp = room->localParticipant();
  assert(lp);

  LK_LOG_INFO("[receiver] Connected as identity='{}' room='{}'; expecting "
              "sender identity='{}'",
              lp->identity(), room->room_info().name, sender_identity);

  // set the video frame callback for the sender's camera track
  int video_frame_count = 0;
  room->setOnVideoFrameCallback(
      sender_identity, TrackSource::SOURCE_CAMERA,
      [&video_frame_count](const VideoFrame &frame, std::int64_t timestamp_us) {
        const auto ts_ms =
            std::chrono::duration<double, std::milli>(timestamp_us).count();
        const int n = video_frame_count++;
        if (n % 10 == 0) {
          LK_LOG_INFO("[receiver] Video frame #{} {}x{} ts_ms={}", n,
                      frame.width(), frame.height(), ts_ms);
        }
      });

  // Add a callback for the sender's data track. DataTracks can have mutliple
  // subscribers to fan out  callbacks
  int data_frame_count = 0;
  room->addOnDataFrameCallback(
      sender_identity, kDataTrackName,
      [&data_frame_count](const std::vector<std::uint8_t> &payload,
                          std::optional<std::uint64_t> user_ts) {
        const int n = data_frame_count++;
        if (n % 10 == 0) {
          LK_LOG_INFO("[receiver] Data frame #{}", n);
        }
      });

  LK_LOG_INFO(
      "[receiver] Listening for camera + data track '{}'; Ctrl-C to exit",
      kDataTrackName);

  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  LK_LOG_INFO("[receiver`] Shutting down");
  room.reset();
}

int main(int argc, char *argv[]) {
  std::string url = getenvOrEmpty("LIVEKIT_URL");
  std::string sender_token = getenvOrEmpty("LIVEKIT_SENDER_TOKEN");
  std::string receiver_token = getenvOrEmpty("LIVEKIT_RECEIVER_TOKEN");

  if (argc >= 4) {
    url = argv[1];
    sender_token = argv[2];
    receiver_token = argv[3];
  }

  if (url.empty() || sender_token.empty() || receiver_token.empty()) {
    LK_LOG_ERROR(
        "Usage: HelloLivekit <ws-url> <sender-token> <receiver-token>\n"
        "  or set LIVEKIT_URL, LIVEKIT_SENDER_TOKEN, LIVEKIT_RECEIVER_TOKEN");
    return 1;
  }

  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

  // Shared: sender publishes its identity here so the receiver knows who
  // to subscribe to (no hardcoded identity required).
  std::atomic<std::string *> sender_identity{nullptr};

  std::thread sender_thread(runSender, std::cref(url), std::cref(sender_token),
                            std::ref(sender_identity));
  std::thread receiver_thread(runReceiver, std::cref(url),
                              std::cref(receiver_token),
                              std::ref(sender_identity));

  sender_thread.join();
  receiver_thread.join();

  delete sender_identity.load();

  livekit::shutdown();
  return 0;
}
