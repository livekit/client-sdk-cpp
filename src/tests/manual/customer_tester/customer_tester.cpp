#include <livekit/livekit.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
volatile std::sig_atomic_t stopped = 0;
void onSignal(int) { stopped = 1; }

struct Options {
  std::string url, token, codec = "h264", encoder = "auto", format = "i420", require_encoder, csv_path,
                          leave_mode = "server";
  int cycles = 100, seconds = 10, settle = 2, warmup = 5;
  int streams = 3, width = 1280, height = 720, fps = 30, bitrate = 4000000;
  bool server_controlled = false, unpublish_before_server_delete = false;
};

void usage() {
  std::cerr << "Usage: livekit_rss_probe --url wss://HOST --token JWT [options]\n"
               "  --server-controlled  Read fresh room credentials/control acknowledgements from stdin\n"
               "  --leave-mode server|client   Controlled-room departure mode (server)\n"
               "  --cycles N       Room cycles; 0 = until Ctrl+C (default 100)\n"
               "  --unpublish-before-server-delete  Unpublish tracks before external room deletion\n"
               "  --seconds N      Seconds of noise per room (10)\n"
               "  --settle N       Seconds after releasing each room before RSS sample (2)\n"
               "  --warmup N       Completed cycles before growth baseline (5; 0 = after init)\n"
               "  --streams N      Concurrent video tracks per room; 1..16 (default 3)\n"
               "  --width N --height N --fps N --bitrate N   Per stream (1280 720 30 4000000 bps)\n"
               "  --codec h264|av1|vp8|vp9|h265             (h264)\n"
               "  --encoder auto|software|hardware|nvenc|vaapi (auto; preference only)\n"
               "  --require-encoder TEXT  Fail unless encoder stats contain TEXT (case-sensitive)\n"
               "  --format i420|nv12                        (i420)\n"
               "  --csv FILE       Write CSV to FILE (default stdout)\n"
               "CSV defaults to stdout; progress, encoder details and summary go to stderr.\n";
}

template <typename T>
T choice(const std::string& value, std::initializer_list<std::pair<const char*, T>> values) {
  for (const auto& [name, result] : values)
    if (value == name) return result;
  throw std::runtime_error("Unsupported choice: " + value);
}
// VideoCodec is forward-declared in the public headers. These values match
// livekit-ffi video_frame.proto. There is no per-publish encoder field; nvenc
// and vaapi are selected with LIVEKIT_PREFERRED_HW_ENCODER.
livekit::VideoCodec codec(const Options& o) {
  const int value = choice<int>(o.codec, {{"vp8", 0}, {"h264", 1}, {"av1", 2}, {"vp9", 3}, {"h265", 4}});
  return static_cast<livekit::VideoCodec>(value);
}
void preferEncoder(const Options& o) {
  choice<int>(o.encoder, {{"auto", 0}, {"software", 1}, {"hardware", 2}, {"nvenc", 3}, {"vaapi", 4}});
  if (o.encoder == "nvenc" || o.encoder == "vaapi") {
#if defined(_WIN32)
    _putenv_s("LIVEKIT_PREFERRED_HW_ENCODER", o.encoder.c_str());
#else
    setenv("LIVEKIT_PREFERRED_HW_ENCODER", o.encoder.c_str(), 1);
#endif
  }
}
livekit::VideoBufferType format(const Options& o) {
  using V = livekit::VideoBufferType;
  return choice<V>(o.format, {{"i420", V::I420}, {"nv12", V::NV12}});
}

Options parse(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    if (key == "--server-controlled") {
      o.server_controlled = true;
      continue;
    }
    if (key == "--unpublish-before-server-delete") {
      o.unpublish_before_server_delete = true;
      continue;
    }
    if (i + 1 == argc) throw std::runtime_error("Missing value for " + key);
    const std::string value = argv[++i];
    if (key == "--url")
      o.url = value;
    else if (key == "--token")
      o.token = value;
    else if (key == "--codec")
      o.codec = value;
    else if (key == "--encoder")
      o.encoder = value;
    else if (key == "--require-encoder")
      o.require_encoder = value;
    else if (key == "--format")
      o.format = value;
    else if (key == "--csv")
      o.csv_path = value;
    else if (key == "--leave-mode")
      o.leave_mode = value;
    else {
      int* dest = nullptr;
      if (key == "--cycles")
        dest = &o.cycles;
      else if (key == "--seconds")
        dest = &o.seconds;
      else if (key == "--settle")
        dest = &o.settle;
      else if (key == "--warmup")
        dest = &o.warmup;
      else if (key == "--streams")
        dest = &o.streams;
      else if (key == "--width")
        dest = &o.width;
      else if (key == "--height")
        dest = &o.height;
      else if (key == "--fps")
        dest = &o.fps;
      else if (key == "--bitrate")
        dest = &o.bitrate;
      else
        throw std::runtime_error("Unknown option: " + key);
      auto r = std::from_chars(value.data(), value.data() + value.size(), *dest);
      if (r.ec != std::errc{} || r.ptr != value.data() + value.size() || *dest < 0)
        throw std::runtime_error("Expected nonnegative integer for " + key);
    }
  }
  if (o.url.empty() || (!o.server_controlled && o.token.empty()))
    throw std::runtime_error("--url and either --token or --server-controlled are required");
  if (o.server_controlled && !o.token.empty())
    throw std::runtime_error("--server-controlled receives per-room tokens on stdin; omit --token");
  if (o.leave_mode != "server" && o.leave_mode != "client")
    throw std::runtime_error("--leave-mode must be server or client");
  if (o.streams < 1 || o.streams > 16) throw std::runtime_error("--streams must be 1..16");
  if (o.width < 2 || o.width > 8192 || o.height < 2 || o.height > 8192 || o.width % 2 || o.height % 2 || o.fps < 1 ||
      o.fps > 240 || o.bitrate < 1 || o.seconds < 1)
    throw std::runtime_error("Need even dimensions 2..8192, fps 1..240, positive bitrate and seconds");
  if (o.cycles && o.warmup >= o.cycles)
    throw std::runtime_error("--warmup must be less than --cycles (unless cycles=0)");
  (void)codec(o);
  preferEncoder(o);
  (void)format(o);
  return o;
}

// Bounded line protocol; raw reads avoid stdio buffering hiding data from poll().
std::string controlLine() {
  std::string line;
  const auto deadline = Clock::now() + 60s;
  while (!stopped && Clock::now() < deadline && line.size() < 16384) {
    pollfd input{STDIN_FILENO, POLLIN, 0};
    const int ready = poll(&input, 1, 100);
    if (ready < 0 && errno == EINTR) continue;
    if (ready < 0) throw std::runtime_error("Controller pipe poll failed");
    if (!ready) continue;
    char c;
    const auto n = read(STDIN_FILENO, &c, 1);
    if (n < 0 && errno == EINTR) continue;
    if (n != 1) throw std::runtime_error("Controller pipe closed");
    if (c == '\n') return line;
    line += c;
  }
  throw std::runtime_error("Controller wait interrupted, timed out, or line too long");
}

// Read current RSS, not getrusage().ru_maxrss (a high-water mark that cannot fall).
struct Memory {
  std::int64_t rss_kib = -1;
  int threads = 0;
};
Memory memory() {
  Memory m;
  std::string line;
  std::ifstream smaps("/proc/self/smaps_rollup");
  while (std::getline(smaps, line))
    if (line.rfind("Rss:", 0) == 0) m.rss_kib = std::stoll(line.substr(4));
  if (m.rss_kib < 0) throw std::runtime_error("Cannot read Rss in /proc/self/smaps_rollup");
  std::ifstream status("/proc/self/status");
  while (std::getline(status, line))
    if (line.rfind("Threads:", 0) == 0) m.threads = std::stoi(line.substr(8));
  return m;
}

struct Counts {
  std::uint64_t captured = 0, encoded = 0, sent = 0, bytes = 0;
};
struct Reporter {
  explicit Reporter(std::ostream& stream) : output(stream) {}
  std::ostream& output;
  Clock::time_point start = Clock::now();
  std::int64_t initial = -1;
  Memory row(std::uint64_t cycle, const char* phase, Counts c = {}) {
    const auto m = memory();
    if (initial < 0) initial = m.rss_kib;
    output << cycle << ',' << phase << ',' << std::fixed << std::setprecision(3)
           << std::chrono::duration<double>(Clock::now() - start).count() << ',' << m.rss_kib / 1024.0 << ','
           << (m.rss_kib - initial) / 1024.0 << ',' << m.threads << ',' << c.captured << ',' << c.encoded << ','
           << c.sent << ',' << c.bytes << '\n';
    output.flush();
    return m;
  }
};

// SplitMix64: constant state, new spatial AND temporal noise; no frame history.
void noise(livekit::VideoFrame& frame, std::uint64_t& state) {
  auto* p = frame.data();
  for (std::size_t i = 0; i < frame.dataSize(); i += 8) {
    auto z = (state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    z ^= z >> 31;
    std::memcpy(p + i, &z, std::min<std::size_t>(8, frame.dataSize() - i));
  }
}

// Delegate only stores atomics. It outlives Room and performs no SDK calls.
struct DisconnectObserver : livekit::RoomDelegate {
  std::atomic<int> reason{-1};
  std::atomic<bool> eos{false};
  void onDisconnected(livekit::Room&, const livekit::DisconnectedEvent& event) override {
    reason.store(static_cast<int>(event.reason));
  }
  void onRoomEos(livekit::Room&, const livekit::RoomEosEvent&) override { eos.store(true); }
};

struct VideoStreamState {
  std::string name;
  std::shared_ptr<livekit::VideoSource> source;
  std::shared_ptr<livekit::LocalVideoTrack> track;
};

// SDK construction, capture and destruction are owned by the main thread.
struct Session {
  DisconnectObserver observer; // Declared before Room: destroyed after it.
  std::unique_ptr<livekit::Room> room = std::make_unique<livekit::Room>();
  std::shared_ptr<livekit::LocalParticipant> participant;
  std::vector<VideoStreamState> streams;
  bool unpublishTracks() noexcept {
    bool ok = true;
    for (auto& stream : streams) {
      const auto& track = stream.track;
      if (!track) continue;
      try {
        auto publication = track->publication();
        if (publication && participant && room && room->connectionState() == livekit::ConnectionState::Connected)
          participant->unpublishTrack(publication->sid());
      } catch (const std::exception& e) {
        std::cerr << "Unpublish failed: " << e.what() << '\n';
        ok = false;
      }
      track->setPublication(nullptr); // Also break ownership on error, as in the sketch.
    }
    return ok;
  }
  bool close(bool explicitly_disconnect = false) noexcept {
    bool ok = unpublishTracks();
    participant.reset();
    streams.clear();
    if (explicitly_disconnect && room) {
      try {
        if (!room->disconnect(livekit::DisconnectReason::ClientInitiated)) {
          std::cerr << "Explicit client disconnect did not succeed\n";
          ok = false;
        }
      } catch (const std::exception& e) {
        std::cerr << "Client disconnect failed: " << e.what() << '\n';
        ok = false;
      }
    }
    // ~Room disconnects if still necessary.
    room.reset();
    return ok;
  }
  ~Session() { close(); }
};

Counts runCycle(const Options& o, std::uint64_t cycle, Reporter& report, const std::string& expected_name,
                const std::string& expected_sid) {
  Session s;
  if (o.server_controlled) s.room->setDelegate(&s.observer);
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = false;
  room_options.dynacast = false; // Keep publishing even without a viewer.
  room_options.join_retries = 0;
  room_options.connect_timeout = 10s;
  std::cerr << "Cycle " << cycle << ": joining\n";
  if (!s.room->connect(o.url, o.token, room_options)) throw std::runtime_error("Room connect failed");
  const auto info = s.room->roomInfo();
  if (o.server_controlled && (info.name != expected_name || info.sid.value_or("") != expected_sid))
    throw std::runtime_error("Joined room name/SID did not match the newly created server room");
  std::cerr << "Cycle " << cycle << ": room=" << info.name << " sid=" << info.sid.value_or("") << '\n';
  s.participant = s.room->localParticipant().lock();
  if (!s.participant) throw std::runtime_error("No local participant");
  livekit::TrackPublishOptions pub;
  pub.source = livekit::TrackSource::SOURCE_CAMERA;
  pub.video_codec = codec(o);
  pub.simulcast = false;
  pub.red = false;
  pub.dtx = false;
  pub.preconnect_buffer = false;
  pub.video_encoding = livekit::VideoEncodingOptions{static_cast<std::uint64_t>(o.bitrate), double(o.fps)};
  pub.degradation_preference = livekit::DegradationPreference::MaintainResolution;
  s.streams.reserve(o.streams);
  for (int i = 0; i < o.streams; ++i) {
    auto& stream = s.streams.emplace_back(); // Owned before any throwing SDK calls.
    stream.name = "noise-" + std::to_string(i + 1);
    stream.source = std::make_shared<livekit::VideoSource>(o.width, o.height);
    stream.track = livekit::LocalVideoTrack::createLocalVideoTrack(stream.name, stream.source);
    pub.stream = stream.name;
    s.participant->publishTrack(stream.track, pub);
  }
  Counts counts; // CSV counters are totals across all tracks.
  std::vector<Counts> per_stream(o.streams);
  {
    std::vector<livekit::VideoFrame> frames;
    std::vector<std::uint64_t> rng;
    frames.reserve(o.streams);
    rng.reserve(o.streams);
    for (int i = 0; i < o.streams; ++i) {
      frames.push_back(livekit::VideoFrame::create(o.width, o.height, format(o)));
      rng.push_back(cycle ^ (UINT64_C(0xd1b54a32d192ed03) * (i + 1)));
    }
    auto next = Clock::now();
    const auto end = next + std::chrono::seconds(o.seconds);
    auto sample = next + 1s;
    const auto period = std::chrono::nanoseconds(1000000000 / o.fps);
    while (!stopped && Clock::now() < end) {
      // Feed every track once per tick: fps is PER TRACK, not shared between tracks.
      for (int i = 0; i < o.streams && !stopped; ++i) {
        noise(frames[i], rng[i]);
        const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch());
        s.streams[i].source->captureFrame(frames[i], timestamp.count());
        ++per_stream[i].captured;
        ++counts.captured;
      }
      if (Clock::now() >= sample) {
        report.row(cycle, "streaming", counts);
        sample = Clock::now() + 1s;
      }
      next += period;
      // If encoding/capture cannot keep up, do not queue a catch-up burst.
      if (next < Clock::now()) next = Clock::now();
      std::this_thread::sleep_until(std::min(next, end));
    }
  } // Frame storage is released before the idle sample.
  std::this_thread::sleep_for(250ms); // Let the last captured frame reach encoder stats.
  for (int i = 0; i < o.streams; ++i) {
    const auto& stream = s.streams[i];
    auto& track_counts = per_stream[i];
    auto future = stream.track->getStats();
    if (future.wait_for(5s) != std::future_status::ready) throw std::runtime_error("Video stats timed out");
    const auto stats = future.get();
    for (const auto& entry : stats) {
      const auto* out = std::get_if<livekit::RtcOutboundRtpStats>(&entry.stats);
      if (!out || out->stream.kind != "video") continue;
      track_counts.encoded += out->outbound.frames_encoded;
      track_counts.sent += out->outbound.frames_sent;
      track_counts.bytes += out->sent.bytes_sent;
      std::string mime = "unknown";
      for (const auto& other : stats)
        if (auto* c = std::get_if<livekit::RtcCodecStats>(&other.stats); c && c->rtc.id == out->stream.codec_id)
          mime = c->codec.mime_type;
      std::cerr << "Cycle " << cycle << ": track=" << stream.name << " codec=" << mime
                << " encoder=" << std::quoted(out->outbound.encoder_implementation)
                << " power_efficient=" << out->outbound.power_efficient_encoder
                << " encoded_size=" << out->outbound.frame_width << 'x' << out->outbound.frame_height
                << " captured=" << track_counts.captured << " encoded=" << track_counts.encoded
                << " sent=" << track_counts.sent << " bytes=" << track_counts.bytes << '\n';
      if (out->outbound.frames_encoded && (out->outbound.frame_width != static_cast<unsigned>(o.width) ||
                                           out->outbound.frame_height != static_cast<unsigned>(o.height)))
        throw std::runtime_error(stream.name + ": encoded resolution does not match requested size");
      if (!o.require_encoder.empty() &&
          out->outbound.encoder_implementation.find(o.require_encoder) == std::string::npos)
        throw std::runtime_error("Actual encoder does not match --require-encoder " + o.require_encoder);
    }
    if (!stopped && (!track_counts.encoded || !track_counts.sent || !track_counts.bytes))
      throw std::runtime_error(stream.name + ": no encoded/sent video confirmed");
    counts.encoded += track_counts.encoded;
    counts.sent += track_counts.sent;
    counts.bytes += track_counts.bytes;
  } // Each track's stats buffers/future are released before teardown / idle RSS.
  if (o.server_controlled && !stopped && o.leave_mode == "client") {
    report.row(cycle, "ready_for_client_leave", counts);
    // Controller only checks server state. This process initiates the SDK leave.
    if (controlLine() != "LEAVE") throw std::runtime_error("Client leave was not acknowledged");
    if (s.observer.reason.load() != -1 || s.room->connectionState() != livekit::ConnectionState::Connected)
      throw std::runtime_error("Room disconnected before the client could initiate leaving");
    if (!s.close(true)) throw std::runtime_error("Client unpublish/disconnect failed");
    if (s.observer.reason.load() != static_cast<int>(livekit::DisconnectReason::ClientInitiated))
      throw std::runtime_error("Expected ClientInitiated disconnect reason");
    std::cerr << "Cycle " << cycle << ": disconnect=ClientInitiated; all tracks and room released\n";
    report.row(cycle, "client_left", counts);
    return counts;
  }
  if (o.server_controlled && !stopped) {
    // The controller deletes the still-occupied room in response to this CSV row.
    if (o.unpublish_before_server_delete) {
      if (!s.unpublishTracks()) throw std::runtime_error("Pre-delete unpublish failed");
      report.row(cycle, "tracks_unpublished", counts);
    }
    report.row(cycle, "ready_for_delete", counts);
    if (controlLine() != "DELETED") throw std::runtime_error("Server deletion was not acknowledged");
    const auto deadline = Clock::now() + 10s;
    while (!stopped && Clock::now() < deadline && (s.observer.reason.load() < 0 || !s.observer.eos.load()))
      std::this_thread::sleep_for(10ms);
    if (s.observer.reason.load() != static_cast<int>(livekit::DisconnectReason::RoomDeleted) || !s.observer.eos.load())
      throw std::runtime_error("Expected RoomDeleted and room EOS after server deletion");
    std::cerr << "Cycle " << cycle << ": server disconnect=RoomDeleted; room EOS received\n";
    report.row(cycle, "server_deleted", counts);
  } else
    report.row(cycle, "before_leave", counts);
  if (!s.close()) throw std::runtime_error("Cycle teardown failed; excluding from growth summary");
  return counts;
}

struct SdkLifetime {
  SdkLifetime() { livekit::initialize(livekit::LogLevel::Warn); }
  ~SdkLifetime() { livekit::shutdown(); }
};

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--help") {
    usage();
    return 0;
  }
  try {
    const auto o = parse(argc, argv);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::cerr << "PID=" << getpid() << " LiveKit=" << LIVEKIT_BUILD_VERSION << " codec=" << o.codec
              << " encoder_preference=" << o.encoder << " format=" << o.format
              << " leave_mode=" << (o.server_controlled ? o.leave_mode : "client") << " streams=" << o.streams
              << " size=" << o.width << 'x' << o.height << " fps_per_stream=" << o.fps
              << " bitrate_per_stream=" << o.bitrate << '\n';
    std::ofstream csv_file;
    if (!o.csv_path.empty()) {
      csv_file.open(o.csv_path);
      if (!csv_file) throw std::runtime_error("Cannot open --csv output");
    }
    auto& csv = o.csv_path.empty() ? std::cout : csv_file;
    Reporter report(csv);
    csv << "cycle,phase,elapsed_s,rss_mib,delta_start_mib,threads,captured,encoded,sent,bytes_sent\n";
    report.row(0, "before_init");
    std::uint64_t completed = 0;
    std::int64_t baseline = -1, last = -1;
    int result = 0;
    {
      SdkLifetime sdk; // Exactly one initialize(), one shutdown() per process.
      const auto initial = report.row(0, "after_init");
      if (o.warmup == 0) baseline = initial.rss_kib;
      for (std::uint64_t cycle = 1; !stopped && (!o.cycles || cycle <= std::uint64_t(o.cycles)); ++cycle) {
        Counts counts;
        bool valid = true;
        try {
          auto current = o;
          std::string name, sid;
          if (o.server_controlled) {
            name = controlLine();
            sid = controlLine();
            current.token = controlLine();
            if (name.empty() || sid.empty() || current.token.empty())
              throw std::runtime_error("Missing fresh room credentials");
          }
          counts = runCycle(current, cycle, report, name, sid);
        } catch (const std::exception& e) {
          std::cerr << "Cycle " << cycle << " failed: " << e.what() << '\n';
          valid = false;
        }
        const auto deadline = Clock::now() + std::chrono::seconds(o.settle);
        while (Clock::now() < deadline) std::this_thread::sleep_for(50ms);
        const auto idle = report.row(cycle, valid && !stopped ? "idle" : "incomplete_idle", counts);
        if (!valid) {
          result = 1;
          break;
        }
        if (stopped) break; // Interrupted cycles never enter the growth calculation.
        completed = cycle;
        last = idle.rss_kib;
        if (cycle == std::uint64_t(o.warmup)) baseline = last;
      }
      report.row(completed, "before_shutdown");
    } // Every room/track/source has already been destroyed.
    report.row(completed, "after_shutdown");
    if (baseline >= 0 && completed > std::uint64_t(o.warmup)) {
      const auto delta = (last - baseline) / 1024.0;
      std::cerr << "Post-warmup idle RSS growth: " << std::fixed << std::setprecision(3) << delta << " MiB over "
                << completed - o.warmup << " cycles = " << delta / double(completed - o.warmup)
                << " MiB/cycle (endpoint average).\n";
    } else
      std::cerr << "Too few completed cycles for a post-warmup RSS comparison.\n";
    return result;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
}
