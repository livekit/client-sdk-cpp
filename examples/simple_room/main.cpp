#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "livekit/livekit.h"
#include "wav_audio_source.h"

// TODO(shijing), remove this livekit_ffi.h as it should be internal only.
#include "livekit_ffi.h"

// Consider expose this video_utils.h to public ?
#include "video_utils.h"

using namespace livekit;

namespace {

std::atomic<bool> g_running{true};

void print_usage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  " << prog << " <ws-url> <token>\n"
            << "or:\n"
            << "  " << prog << " --url=<ws-url> --token=<token>\n"
            << "  " << prog << " --url <ws-url> --token <token>\n\n"
            << "Env fallbacks:\n"
            << "  LIVEKIT_URL, LIVEKIT_TOKEN\n";
}

void handle_sigint(int) { g_running.store(false); }

bool parse_args(int argc, char *argv[], std::string &url, std::string &token) {
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

class SimpleRoomDelegate : public livekit::RoomDelegate {
public:
  void onParticipantConnected(
      livekit::Room & /*room*/,
      const livekit::ParticipantConnectedEvent &ev) override {
    std::cout << "[Room] participant connected: identity=" << ev.identity
              << " name=" << ev.name << "\n";
  }

  void onTrackSubscribed(livekit::Room & /*room*/,
                         const livekit::TrackSubscribedEvent &ev) override {
    std::cout << "[Room] track subscribed: participant_identity="
              << ev.participant_identity << " track_sid=" << ev.track_sid
              << " name=" << ev.track_name << "\n";
    // TODO(shijing): when you expose Track kind/source here, you can check
    // whether this is a video track and start a VideoStream-like consumer. Use
    // the python code as reference.
  }
};

// Test utils to run a capture loop to publish noisy audio frames to the room
void runNoiseCaptureLoop(const std::shared_ptr<AudioSource> &source) {
  const int sample_rate = source->sample_rate();
  const int num_channels = source->num_channels();
  const int frame_ms = 10;
  const int samples_per_channel = sample_rate * frame_ms / 1000;

  WavAudioSource WavAudioSource("data/welcome.wav", 48000, 1, false);
  using Clock = std::chrono::steady_clock;
  auto next_deadline = Clock::now();
  while (g_running.load(std::memory_order_relaxed)) {
    AudioFrame frame =
        AudioFrame::create(sample_rate, num_channels, samples_per_channel);
    WavAudioSource.fillFrame(frame);
    try {
      source->captureFrame(frame);
    } catch (const std::exception &e) {
      // If something goes wrong, log and break out
      std::cerr << "Error in captureFrame: " << e.what() << std::endl;
      break;
    }

    // Pace the loop to roughly real-time
    next_deadline += std::chrono::milliseconds(frame_ms);
    std::this_thread::sleep_until(next_deadline);
  }

  // Optionally clear queued audio on exit
  try {
    source->clearQueue();
  } catch (...) {
    // ignore errors on shutdown
    std::cout << "Error in clearQueue" << std::endl;
  }
}

void runFakeVideoCaptureLoop(const std::shared_ptr<VideoSource> &source) {
  auto frame = LKVideoFrame::create(1280, 720, VideoBufferType::ARGB);
  double framerate = 1.0 / 30;
  while (g_running.load(std::memory_order_relaxed)) {
    static auto start = std::chrono::high_resolution_clock::now();
    float t = std::chrono::duration<float>(
                  std::chrono::high_resolution_clock::now() - start)
                  .count();
    // Cycle every 4 seconds: 0=red, 1=green, 2=blue, 3 black
    int stage = static_cast<int>(t) % 4;
    std::vector<int> rgb(4);
    switch (stage) {
    case 0: // red
      rgb[0] = 255;
      rgb[1] = 0;
      rgb[2] = 0;
      break;
    case 1: // green
      rgb[0] = 0;
      rgb[1] = 255;
      rgb[2] = 0;
      break;
    case 2: // blue
      rgb[0] = 0;
      rgb[1] = 0;
      rgb[2] = 255;
      break;
    case 4: // black
      rgb[0] = 0;
      rgb[1] = 0;
      rgb[2] = 0;
    }
    for (size_t i = 0; i < frame.dataSize(); i += 4) {
      frame.data()[i] = 255;
      frame.data()[i + 1] = rgb[0];
      frame.data()[i + 2] = rgb[1];
      frame.data()[i + 3] = rgb[2];
    }
    LKVideoFrame i420 = convertViaFfi(frame, VideoBufferType::I420, false);
    try {
      source->captureFrame(frame, 0, VideoRotation::VIDEO_ROTATION_0);
    } catch (const std::exception &e) {
      // If something goes wrong, log and break out
      std::cerr << "Error in captureFrame: " << e.what() << std::endl;
      break;
    }

    std::this_thread::sleep_for(std::chrono::duration<double>(framerate));
  }
}

} // namespace

int main(int argc, char *argv[]) {
  std::string url, token;
  if (!parse_args(argc, argv, url, token)) {
    print_usage(argv[0]);
    return 1;
  }

  // Exit if token and url are not set
  if (url.empty() || token.empty()) {
    std::cerr << "LIVEKIT_URL and LIVEKIT_TOKEN (or CLI args) are required\n";
    return 1;
  }

  std::cout << "Connecting to: " << url << std::endl;

  // Handle Ctrl-C to exit the idle loop
  std::signal(SIGINT, handle_sigint);

  livekit::Room room{};
  SimpleRoomDelegate delegate;
  room.setDelegate(&delegate);

  bool res = room.Connect(url, token);
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
    audioPub = room.local_participant()->publishTrack(audioTrack, audioOpts);

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

  // TODO, if we have pre-buffering feature, we might consider starting the
  // thread right after creating the source.
  std::thread audioThread(runNoiseCaptureLoop, audioSource);

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
    videoPub = room.local_participant()->publishTrack(videoTrack, videoOpts);

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
  std::thread videoThread(runFakeVideoCaptureLoop, videoSource);

  // Keep the app alive until Ctrl-C so we continue receiving events,
  // similar to asyncio.run(main()) keeping the loop running.
  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Shutdown the audio thread.
  if (audioThread.joinable()) {
    audioThread.join();
  }
  // Clean up the audio track publishment
  room.local_participant()->unpublishTrack(audioPub->sid());

  if (videoThread.joinable()) {
    videoThread.join();
  }
  // Clean up the video track publishment
  room.local_participant()->unpublishTrack(videoPub->sid());

  FfiClient::instance().shutdown();
  std::cout << "Exiting.\n";
  return 0;
}
