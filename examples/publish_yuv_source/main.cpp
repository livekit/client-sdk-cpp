/*
 * Copyright 2025 LiveKit, Inc.
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

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "livekit/livekit.h"
#include "yuv_source.h"

using namespace livekit;

namespace {

std::atomic<bool> g_running{true};

void printUsage(const char *prog) {
  std::cerr
      << "Usage: " << prog
      << " --url <ws-url> --token <token> --raw-nv12 <host:port> [options]\n\n"
      << "  --url <url>              LiveKit WebSocket URL\n"
      << "  --token <token>          JWT token\n"
      << "  --enable_e2ee            Enable E2EE\n"
      << "  --e2ee_key <key>         E2EE shared key\n\n"
      << "  --raw-nv12 <host:port>   TCP server for raw NV12 (default "
         "127.0.0.1:5004)\n"
      << "  --raw-width <w>          Frame width (default: 1280)\n"
      << "  --raw-height <h>         Frame height (default: 720)\n"
      << "  --raw-fps <fps>          Frame rate (default: 30)\n\n"
      << "Env: LIVEKIT_URL, LIVEKIT_TOKEN, LIVEKIT_E2EE_KEY\n";
}

void handleSignal(int) { g_running.store(false); }

struct RawNv12Args {
  std::string host = "127.0.0.1";
  std::uint16_t port = 5004;
  int width = 1280;
  int height = 720;
  int fps = 30;
};

bool parseArgs(int argc, char *argv[], std::string &url, std::string &token,
               bool &enable_e2ee, std::string &e2ee_key,
               RawNv12Args &raw_nv12) {
  enable_e2ee = false;
  raw_nv12 = RawNv12Args{};
  auto get_flag_value = [&](const std::string &name, int &i) -> std::string {
    std::string arg = argv[i];
    const std::string eq = name + "=";
    if (arg.rfind(name, 0) == 0) {
      if (arg.size() > name.size() && arg[name.size()] == '=')
        return arg.substr(eq.size());
      if (i + 1 < argc)
        return std::string(argv[++i]);
    }
    return {};
  };

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help")
      return false;
    if (a == "--enable_e2ee") {
      enable_e2ee = true;
      continue;
    }
    if (a.rfind("--raw-nv12", 0) == 0) {
      std::string v = get_flag_value("--raw-nv12", i);
      if (v.empty())
        v = "127.0.0.1:5004";
      size_t colon = v.find(':');
      if (colon != std::string::npos) {
        raw_nv12.host = v.substr(0, colon);
        try {
          raw_nv12.port =
              static_cast<std::uint16_t>(std::stoul(v.substr(colon + 1)));
        } catch (...) {
          raw_nv12.port = 5004;
        }
      } else {
        raw_nv12.host = v;
      }
      continue;
    }
    if (a.rfind("--raw-width", 0) == 0) {
      std::string v = get_flag_value("--raw-width", i);
      if (!v.empty())
        try {
          raw_nv12.width = std::stoi(v);
        } catch (...) {
        }
      continue;
    }
    if (a.rfind("--raw-height", 0) == 0) {
      std::string v = get_flag_value("--raw-height", i);
      if (!v.empty())
        try {
          raw_nv12.height = std::stoi(v);
        } catch (...) {
        }
      continue;
    }
    if (a.rfind("--raw-fps", 0) == 0) {
      std::string v = get_flag_value("--raw-fps", i);
      if (!v.empty())
        try {
          raw_nv12.fps = std::stoi(v);
        } catch (...) {
        }
      continue;
    }
    if (a.rfind("--url", 0) == 0) {
      std::string v = get_flag_value("--url", i);
      if (!v.empty())
        url = v;
      continue;
    }
    if (a.rfind("--token", 0) == 0) {
      std::string v = get_flag_value("--token", i);
      if (!v.empty())
        token = v;
      continue;
    }
    if (a.rfind("--e2ee_key", 0) == 0) {
      std::string v = get_flag_value("--e2ee_key", i);
      if (!v.empty())
        e2ee_key = v;
    }
  }

  if (url.empty()) {
    const char *e = std::getenv("LIVEKIT_URL");
    if (e)
      url = e;
  }
  if (token.empty()) {
    const char *e = std::getenv("LIVEKIT_TOKEN");
    if (e)
      token = e;
  }
  if (e2ee_key.empty()) {
    const char *e = std::getenv("LIVEKIT_E2EE_KEY");
    if (e)
      e2ee_key = e;
  }
  return !(url.empty() || token.empty());
}

class LoggingDelegate : public livekit::RoomDelegate {
public:
  void onParticipantConnected(
      livekit::Room &, const livekit::ParticipantConnectedEvent &ev) override {
    std::cout << "[Room] participant connected: " << ev.participant->identity()
              << "\n";
  }
  void onTrackSubscribed(livekit::Room &,
                         const livekit::TrackSubscribedEvent &ev) override {
    std::cout << "[Room] track subscribed: "
              << (ev.publication ? ev.publication->name() : "?") << "\n";
  }
};

static std::vector<std::uint8_t> toBytes(const std::string &s) {
  return std::vector<std::uint8_t>(s.begin(), s.end());
}

} // namespace

int main(int argc, char *argv[]) {
  std::string url, token, e2ee_key;
  bool enable_e2ee = false;
  RawNv12Args raw_nv12;
  if (!parseArgs(argc, argv, url, token, enable_e2ee, e2ee_key, raw_nv12)) {
    printUsage(argv[0]);
    return 1;
  }
  if (url.empty() || token.empty()) {
    std::cerr
        << "LIVEKIT_URL and LIVEKIT_TOKEN (or --url/--token) are required\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);
  livekit::initialize(livekit::LogSink::kConsole);

  auto room = std::make_unique<livekit::Room>();
  LoggingDelegate delegate;
  room->setDelegate(&delegate);

  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;
  if (enable_e2ee) {
    livekit::E2EEOptions enc;
    enc.encryption_type = livekit::EncryptionType::GCM;
    if (!e2ee_key.empty())
      enc.key_provider_options.shared_key = toBytes(e2ee_key);
    options.encryption = enc;
  }

  if (!room->Connect(url, token, options)) {
    std::cerr << "Failed to connect\n";
    livekit::shutdown();
    return 1;
  }
  std::cout << "Connected to room: " << room->room_info().name << "\n";

  const int width = raw_nv12.width;
  const int height = raw_nv12.height;
  const std::size_t expected_size = static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(height) * 3 / 2;

  auto videoSource = std::make_shared<VideoSource>(width, height);
  auto videoTrack =
      LocalVideoTrack::createLocalVideoTrack("yuv_source", videoSource);
  TrackPublishOptions videoOpts;
  videoOpts.source = TrackSource::SOURCE_CAMERA;
  videoOpts.dtx = false;
  videoOpts.simulcast = true;
  videoOpts.video_codec = static_cast<VideoCodec>(1); // H264

  std::shared_ptr<LocalTrackPublication> videoPub;
  try {
    videoPub = room->localParticipant()->publishTrack(videoTrack, videoOpts);
    std::cout << "Published video track: SID=" << videoPub->sid()
              << " name=" << videoPub->name() << "\n";
  } catch (const std::exception &e) {
    std::cerr << "Failed to publish track: " << e.what() << "\n";
    livekit::shutdown();
    return 1;
  }

  auto yuvSource = std::make_unique<publish_yuv::YuvSource>(
      raw_nv12.host, raw_nv12.port, width, height, raw_nv12.fps,
      [videoSource, expected_size, width, height](publish_yuv::YuvFrame frame) {
        if (frame.data.size() != expected_size) {
          std::cerr << "Raw NV12 frame size mismatch\n";
          return;
        }
        try {
          VideoFrame vf(width, height, VideoBufferType::NV12,
                        std::move(frame.data));
          videoSource->captureFrame(vf, frame.timestamp_us,
                                    VideoRotation::VIDEO_ROTATION_0);
        } catch (const std::exception &e) {
          std::cerr << "captureFrame: " << e.what() << "\n";
        }
      });
  yuvSource->start();

  while (g_running.load())
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

  yuvSource->stop();
  room->setDelegate(nullptr);
  if (videoPub)
    room->localParticipant()->unpublishTrack(videoPub->sid());
  room.reset();
  livekit::shutdown();
  std::cout << "Exiting.\n";
  return 0;
}
