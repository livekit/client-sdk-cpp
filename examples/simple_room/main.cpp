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
#include <functional>
#include <iostream>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "livekit/livekit.h"
#include "sdl_media_manager.h"
#include "wav_audio_source.h"

// TODO(shijing), remove this livekit_ffi.h as it should be internal only.
#include "livekit_ffi.h"

// Consider expose this video_utils.h to public ?
#include "video_utils.h"

using namespace livekit;

namespace {

std::atomic<bool> g_running{true};

void printUsage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  " << prog << " <ws-url> <token>\n"
            << "or:\n"
            << "  " << prog << " --url=<ws-url> --token=<token>\n"
            << "  " << prog << " --url <ws-url> --token <token>\n\n"
            << "Env fallbacks:\n"
            << "  LIVEKIT_URL, LIVEKIT_TOKEN\n";
}

void handleSignal(int) { g_running.store(false); }

bool parseArgs(int argc, char *argv[], std::string &url, std::string &token) {
  // 1) --help
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      return false;
    }
  }

  // 2) flags: --url= / --token= or split form
  auto get_flag_value = [&](const std::string &name, int &i) -> std::string {
    std::string arg = argv[i];
    const std::string eq = name + "=";
    if (arg.rfind(name, 0) == 0) { // starts with name
      if (arg.size() > name.size() && arg[name.size()] == '=') {
        return arg.substr(eq.size());
      } else if (i + 1 < argc) {
        return std::string(argv[++i]);
      }
    }
    return {};
  };

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.rfind("--url", 0) == 0) {
      auto v = get_flag_value("--url", i);
      if (!v.empty())
        url = v;
    } else if (a.rfind("--token", 0) == 0) {
      auto v = get_flag_value("--token", i);
      if (!v.empty())
        token = v;
    }
  }

  // 3) positional if still empty
  if (url.empty() || token.empty()) {
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      if (a.rfind("--", 0) == 0)
        continue; // skip flags we already parsed
      pos.push_back(std::move(a));
    }
    if (pos.size() >= 2) {
      if (url.empty())
        url = pos[0];
      if (token.empty())
        token = pos[1];
    }
  }

  // 4) env fallbacks
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

  return !(url.empty() || token.empty());
}

class MainThreadDispatcher {
public:
  static void dispatch(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(fn));
  }

  static void update() {
    std::queue<std::function<void()>> local;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::swap(local, queue_);
    }

    // Run everything on main thread
    while (!local.empty()) {
      local.front()();
      local.pop();
    }
  }

private:
  static inline std::mutex mutex_;
  static inline std::queue<std::function<void()>> queue_;
};

class SimpleRoomDelegate : public livekit::RoomDelegate {
public:
  explicit SimpleRoomDelegate(SDLMediaManager &media) : media_(media) {}

  void onParticipantConnected(
      livekit::Room & /*room*/,
      const livekit::ParticipantConnectedEvent &ev) override {
    std::cout << "[Room] participant connected: identity="
              << ev.participant->identity()
              << " name=" << ev.participant->name() << "\n";
  }

  void onTrackSubscribed(livekit::Room & /*room*/,
                         const livekit::TrackSubscribedEvent &ev) override {
    const char *participant_identity =
        ev.participant ? ev.participant->identity().c_str() : "<unknown>";
    const std::string track_sid =
        ev.publication ? ev.publication->sid() : "<unknown>";
    const std::string track_name =
        ev.publication ? ev.publication->name() : "<unknown>";
    std::cout << "[Room] track subscribed: participant_identity="
              << participant_identity << " track_sid=" << track_sid
              << " name=" << track_name;
    if (ev.track) {
      std::cout << " kind=" << static_cast<int>(ev.track->kind());
    }
    if (ev.publication) {
      std::cout << " source=" << static_cast<int>(ev.publication->source());
    }
    std::cout << std::endl;

    // If this is a VIDEO track, create a VideoStream and attach to renderer
    if (ev.track && ev.track->kind() == TrackKind::KIND_VIDEO) {
      VideoStream::Options opts;
      opts.format = livekit::VideoBufferType::RGBA;
      auto video_stream = VideoStream::fromTrack(ev.track, opts);
      if (!video_stream) {
        std::cerr << "Failed to create VideoStream for track " << track_sid
                  << "\n";
        return;
      }

      MainThreadDispatcher::dispatch([this, video_stream] {
        if (!media_.initRenderer(video_stream)) {
          std::cerr << "SDLMediaManager::startRenderer failed for track\n";
        }
      });
    } else if (ev.track && ev.track->kind() == TrackKind::KIND_AUDIO) {
      AudioStream::Options opts;
      auto audio_stream = AudioStream::fromTrack(ev.track, opts);
      MainThreadDispatcher::dispatch([this, audio_stream] {
        if (!media_.startSpeaker(audio_stream)) {
          std::cerr << "SDLMediaManager::startRenderer failed for track\n";
        }
      });
    }
  }

private:
  SDLMediaManager &media_;
};

} // namespace

int main(int argc, char *argv[]) {
  std::string url, token;
  if (!parseArgs(argc, argv, url, token)) {
    printUsage(argv[0]);
    return 1;
  }

  // Exit if token and url are not set
  if (url.empty() || token.empty()) {
    std::cerr << "LIVEKIT_URL and LIVEKIT_TOKEN (or CLI args) are required\n";
    return 1;
  }

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init(SDL_INIT_VIDEO) failed: " << SDL_GetError() << "\n";
    // You can choose to exit, or run in "headless" mode without renderer.
    // return 1;
  }

  // Setup media;
  SDLMediaManager media;

  std::cout << "Connecting to: " << url << std::endl;

  // Handle Ctrl-C to exit the idle loop
  std::signal(SIGINT, handleSignal);

  livekit::Room room{};
  SimpleRoomDelegate delegate(media);
  room.setDelegate(&delegate);

  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;
  bool res = room.Connect(url, token, options);
  std::cout << "Connect result is " << std::boolalpha << res << std::endl;
  if (!res) {
    std::cerr << "Failed to connect to room\n";
    FfiClient::instance().shutdown();
    return 1;
  }

  auto info = room.room_info();
  std::cout << "Connected to room:\n"
            << "  SID: " << (info.sid ? *info.sid : "(none)") << "\n"
            << "  Name: " << info.name << "\n"
            << "  Metadata: " << info.metadata << "\n"
            << "  Max participants: " << info.max_participants << "\n"
            << "  Num participants: " << info.num_participants << "\n"
            << "  Num publishers: " << info.num_publishers << "\n"
            << "  Active recording: " << (info.active_recording ? "yes" : "no")
            << "\n"
            << "  Empty timeout (s): " << info.empty_timeout << "\n"
            << "  Departure timeout (s): " << info.departure_timeout << "\n"
            << "  Lossy DC low threshold: "
            << info.lossy_dc_buffered_amount_low_threshold << "\n"
            << "  Reliable DC low threshold: "
            << info.reliable_dc_buffered_amount_low_threshold << "\n"
            << "  Creation time (ms): " << info.creation_time << "\n";

  // Setup Audio Source / Track
  auto audioSource = std::make_shared<AudioSource>(44100, 1, 10);
  auto audioTrack =
      LocalAudioTrack::createLocalAudioTrack("micTrack", audioSource);

  TrackPublishOptions audioOpts;
  audioOpts.source = TrackSource::SOURCE_MICROPHONE;
  audioOpts.dtx = false;
  audioOpts.simulcast = false;
  std::shared_ptr<LocalTrackPublication> audioPub;
  try {
    // publishTrack takes std::shared_ptr<Track>, LocalAudioTrack derives from
    // Track
    audioPub = room.localParticipant()->publishTrack(audioTrack, audioOpts);

    std::cout << "Published track:\n"
              << "  SID: " << audioPub->sid() << "\n"
              << "  Name: " << audioPub->name() << "\n"
              << "  Kind: " << static_cast<int>(audioPub->kind()) << "\n"
              << "  Source: " << static_cast<int>(audioPub->source()) << "\n"
              << "  Simulcasted: " << std::boolalpha << audioPub->simulcasted()
              << "\n"
              << "  Muted: " << std::boolalpha << audioPub->muted() << "\n";
  } catch (const std::exception &e) {
    std::cerr << "Failed to publish track: " << e.what() << std::endl;
  }

  media.startMic(audioSource);

  // Setup Video Source / Track
  auto videoSource = std::make_shared<VideoSource>(1280, 720);
  std::shared_ptr<LocalVideoTrack> videoTrack =
      LocalVideoTrack::createLocalVideoTrack("cam", videoSource);
  TrackPublishOptions videoOpts;
  videoOpts.source = TrackSource::SOURCE_CAMERA;
  videoOpts.dtx = false;
  videoOpts.simulcast = true;
  std::shared_ptr<LocalTrackPublication> videoPub;
  try {
    // publishTrack takes std::shared_ptr<Track>, LocalAudioTrack derives from
    // Track
    videoPub = room.localParticipant()->publishTrack(videoTrack, videoOpts);

    std::cout << "Published track:\n"
              << "  SID: " << videoPub->sid() << "\n"
              << "  Name: " << videoPub->name() << "\n"
              << "  Kind: " << static_cast<int>(videoPub->kind()) << "\n"
              << "  Source: " << static_cast<int>(videoPub->source()) << "\n"
              << "  Simulcasted: " << std::boolalpha << videoPub->simulcasted()
              << "\n"
              << "  Muted: " << std::boolalpha << videoPub->muted() << "\n";
  } catch (const std::exception &e) {
    std::cerr << "Failed to publish track: " << e.what() << std::endl;
  }
  media.startCamera(videoSource);

  // Keep the app alive until Ctrl-C so we continue receiving events,
  // similar to asyncio.run(main()) keeping the loop running.
  while (g_running.load()) {
    MainThreadDispatcher::update();
    media.render();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Shutdown the audio thread.
  media.stopMic();

  // Clean up the audio track publishment
  room.localParticipant()->unpublishTrack(audioPub->sid());

  media.stopCamera();

  // Clean up the video track publishment
  room.localParticipant()->unpublishTrack(videoPub->sid());

  FfiClient::instance().shutdown();
  std::cout << "Exiting.\n";
  return 0;
}
