#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "livekit/livekit.h"
// TODO, remove the ffi_client from the public usage.
#include "ffi_client.h"

using namespace livekit;

namespace {

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

// Helper: get env var or empty string
std::string getenvOrEmpty(const char *name) {
  const char *v = std::getenv(name);
  return v ? std::string(v) : std::string{};
}

std::int64_t nowEpochMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
      .count();
}

std::string randomHexId(std::size_t nbytes = 16) {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::ostringstream oss;
  for (std::size_t i = 0; i < nbytes; ++i) {
    std::uint8_t b = static_cast<std::uint8_t>(rng() & 0xFF);
    const char *hex = "0123456789abcdef";
    oss << hex[(b >> 4) & 0xF] << hex[b & 0xF];
  }
  return oss.str();
}

// Greeting: send text + image
void greetParticipant(Room *room, const std::string &identity) {
  std::cout << "[DataStream] Greeting participant: " << identity << "\n";

  LocalParticipant *lp = room->localParticipant();
  if (!lp) {
    std::cerr << "[DataStream] No local participant, cannot greet.\n";
    return;
  }

  try {
    const std::int64_t sent_ms = nowEpochMs();
    const std::string sender_id =
        !lp->identity().empty() ? lp->identity() : std::string("cpp_sender");
    const std::vector<std::string> dest{identity};

    // Send text stream ("chat")
    const std::string chat_stream_id = randomHexId();
    const std::string reply_to_id = "";
    std::map<std::string, std::string> chat_attrs;
    chat_attrs["sent_ms"] = std::to_string(sent_ms);
    chat_attrs["kind"] = "chat";
    chat_attrs["test_flag"] = "1";
    chat_attrs["seq"] = "1";

    // Put timestamp in payload too (so you can compute latency even if
    // attributes aren’t plumbed through your reader info yet).
    const std::string body = "Hi! Just a friendly message";
    const std::string payload = "sent_ms=" + std::to_string(sent_ms) + "\n" +
                                "stream_id=" + chat_stream_id + "\n" + body;
    TextStreamWriter text_writer(*lp, "chat", chat_attrs, chat_stream_id,
                                 payload.size(), reply_to_id, dest, sender_id);

    const std::string message = "Hi! Just a friendly message";
    text_writer.write(message); // will be chunked internally if needed
    text_writer.close();        // optional reason/attributes omitted

    // Send image as byte stream
    const std::string file_path = "data/green.avif";
    std::ifstream in(file_path, std::ios::binary);
    if (!in) {
      std::cerr << "[DataStream] Failed to open file: " << file_path << "\n";
      return;
    }

    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());

    const std::string file_stream_id = randomHexId();
    std::map<std::string, std::string> file_attrs;
    file_attrs["sent_ms"] = std::to_string(sent_ms);
    file_attrs["kind"] = "file";
    file_attrs["test_flag"] = "1";
    file_attrs["orig_path"] = file_path;
    const std::string name =
        std::filesystem::path(file_path).filename().string();
    const std::string mime = "image/avif";
    ByteStreamWriter byte_writer(*lp, name, "files", file_attrs, file_stream_id,
                                 data.size(), mime, dest, sender_id);
    byte_writer.write(data);
    byte_writer.close();

    std::cout << "[DataStream] Greeting sent to " << identity
              << " (sent_ms=" << sent_ms << ")\n";
  } catch (const std::exception &e) {
    std::cerr << "[DataStream] Error greeting participant " << identity << ": "
              << e.what() << "\n";
  }
}

// Handlers for incoming streams
void handleChatMessage(std::shared_ptr<livekit::TextStreamReader> reader,
                       const std::string &participant_identity) {
  try {
    const auto info = reader->info(); // copy (safe even if reader goes away)
    const std::int64_t recv_ms = nowEpochMs();
    const std::int64_t sent_ms = info.timestamp;
    const auto latency = (sent_ms > 0) ? (recv_ms - sent_ms) : -1;
    std::string full_text = reader->readAll();
    std::cout << "[DataStream] Received chat from " << participant_identity
              << " topic=" << info.topic << " stream_id=" << info.stream_id
              << " latency_ms=" << latency << " text='" << full_text << "'\n";
  } catch (const std::exception &e) {
    std::cerr << "[DataStream] Error reading chat stream from "
              << participant_identity << ": " << e.what() << "\n";
  }
}

void handleWelcomeImage(std::shared_ptr<livekit::ByteStreamReader> reader,
                        const std::string &participant_identity) {
  try {
    const auto info = reader->info();
    const std::string stream_id =
        info.stream_id.empty() ? "unknown" : info.stream_id;
    const std::string original_name =
        info.name.empty() ? "received_image.bin" : info.name;
    // Latency: prefer header timestamp
    std::int64_t sent_ms = info.timestamp;
    // Optional: override with explicit attribute if you set it
    auto it = info.attributes.find("sent_ms");
    if (it != info.attributes.end()) {
      try {
        sent_ms = std::stoll(it->second);
      } catch (...) {
      }
    }
    const std::int64_t recv_ms = nowEpochMs();
    const std::int64_t latency_ms = (sent_ms > 0) ? (recv_ms - sent_ms) : -1;
    const std::string out_file = "received_" + original_name;
    std::cout << "[DataStream] Receiving image from " << participant_identity
              << " stream_id=" << stream_id << " name='" << original_name << "'"
              << " mime='" << info.mime_type << "'"
              << " size="
              << (info.size ? std::to_string(*info.size) : "unknown")
              << " latency_ms=" << latency_ms << " -> '" << out_file << "'\n";
    std::ofstream out(out_file, std::ios::binary);
    if (!out) {
      std::cerr << "[DataStream] Failed to open output file: " << out_file
                << "\n";
      return;
    }
    std::vector<std::uint8_t> chunk;
    std::uint64_t total_written = 0;
    while (reader->readNext(chunk)) {
      if (!chunk.empty()) {
        out.write(reinterpret_cast<const char *>(chunk.data()),
                  static_cast<std::streamsize>(chunk.size()));
        total_written += chunk.size();
      }
    }
    std::cout << "[DataStream] Saved image from " << participant_identity
              << " stream_id=" << stream_id << " bytes=" << total_written
              << " to '" << out_file << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[DataStream] Error reading image stream from "
              << participant_identity << ": " << e.what() << "\n";
  }
}

} // namespace

int main(int argc, char *argv[]) {
  // Get URL and token from env.
  std::string url = getenvOrEmpty("LIVEKIT_URL");
  std::string token = getenvOrEmpty("LIVEKIT_TOKEN");

  if (argc >= 3) {
    // Allow overriding via CLI: ./SimpleDataStream <ws-url> <token>
    url = argv[1];
    token = argv[2];
  }

  if (url.empty() || token.empty()) {
    std::cerr << "LIVEKIT_URL and LIVEKIT_TOKEN (or CLI args) are required\n";
    return 1;
  }

  std::cout << "[DataStream] Connecting to: " << url << "\n";

  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  auto room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  bool ok = room->Connect(url, token, options);
  std::cout << "[DataStream] Connect result: " << std::boolalpha << ok << "\n";
  if (!ok) {
    std::cerr << "[DataStream] Failed to connect to room\n";
    FfiClient::instance().shutdown();
    return 1;
  }

  auto info = room->room_info();
  std::cout << "[DataStream] Connected to room '" << info.name
            << "', participants: " << info.num_participants << "\n";

  // Register stream handlers
  room->registerTextStreamHandler(
      "chat", [](std::shared_ptr<TextStreamReader> reader,
                 const std::string &participant_identity) {
        std::thread t(handleChatMessage, std::move(reader),
                      participant_identity);
        t.detach();
      });

  room->registerByteStreamHandler(
      "files", [](std::shared_ptr<ByteStreamReader> reader,
                  const std::string &participant_identity) {
        std::thread t(handleWelcomeImage, std::move(reader),
                      participant_identity);
        t.detach();
      });

  // Greet existing participants
  {
    auto remotes = room->remoteParticipants();
    for (const auto &rp : remotes) {
      if (!rp)
        continue;
      std::cout << "Remote: " << rp->identity() << "\n";
      greetParticipant(room.get(), rp->identity());
    }
  }

  // Optionally: greet on join
  //
  // If Room API exposes a participant-connected callback, you could do:
  //
  // room->onParticipantConnected(
  //     [&](RemoteParticipant& participant) {
  //       std::cout << "[DataStream] participant connected: "
  //                 << participant.sid() << " " << participant.identity()
  //                 << "\n";
  //       greetParticipant(room.get(), participant.identity());
  //     });
  //
  // Adjust to your actual event API.
  std::cout << "[DataStream] Ready. Waiting for streams (Ctrl-C to exit)...\n";
  // Keep process alive until signal
  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  std::cout << "[DataStream] Shutting down...\n";
  // It is important to clean up the delegate and room in order.
  room->setDelegate(nullptr);
  room.reset();
  FfiClient::instance().shutdown();
  return 0;
}
