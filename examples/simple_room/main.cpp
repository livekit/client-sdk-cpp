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

  auto audioSource = std::make_shared<AudioSource>(44100, 1, 10);
  auto audioTrack =
      LocalAudioTrack::createLocalAudioTrack("micTrack", audioSource);

  TrackPublishOptions opts;
  opts.source = TrackSource::SOURCE_MICROPHONE;
  opts.dtx = false;
  opts.simulcast = false;

  try {
    // publishTrack takes std::shared_ptr<Track>, LocalAudioTrack derives from
    // Track
    auto pub = room.local_participant()->publishTrack(audioTrack, opts);

    std::cout << "Published track:\n"
              << "  SID: " << pub->sid() << "\n"
              << "  Name: " << pub->name() << "\n"
              << "  Kind: " << static_cast<int>(pub->kind()) << "\n"
              << "  Source: " << static_cast<int>(pub->source()) << "\n"
              << "  Simulcasted: " << std::boolalpha << pub->simulcasted()
              << "\n"
              << "  Muted: " << std::boolalpha << pub->muted() << "\n";
  } catch (const std::exception &e) {
    std::cerr << "Failed to publish track: " << e.what() << std::endl;
  }

  // TODO, if we have pre-buffering feature, we might consider starting the
  // thread right after creating the source.
  std::thread audioThread(runNoiseCaptureLoop, audioSource);
  // Keep the app alive until Ctrl-C so we continue receiving events,
  // similar to asyncio.run(main()) keeping the loop running.
  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Shutdown the audio thread.
  if (audioThread.joinable()) {
    audioThread.join();
  }

  FfiClient::instance().shutdown();
  std::cout << "Exiting.\n";
  return 0;
}
