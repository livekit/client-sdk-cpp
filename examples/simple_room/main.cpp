#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>

#include "livekit/livekit.h"

// TODO, remove this livekit_ffi.h as it should be internal only.
#include "livekit_ffi.h"

// If you have concrete RemoteParticipant / Track / Publication types exposed from Room,
// you can also include their headers and use them directly in main.
// For now we demonstrate logging via RoomDelegate events.

using namespace livekit;

namespace {

std::atomic<bool> g_running{true};

void print_usage(const char* prog) {
    std::cerr << "Usage:\n"
              << "  " << prog << " <ws-url> <token>\n"
              << "or:\n"
              << "  " << prog << " --url=<ws-url> --token=<token>\n"
              << "  " << prog << " --url <ws-url> --token <token>\n\n"
              << "Env fallbacks:\n"
              << "  LIVEKIT_URL, LIVEKIT_TOKEN\n";
}

void handle_sigint(int) {
    g_running = false;
}

bool parse_args(int argc, char* argv[], std::string& url, std::string& token) {
    // 1) --help
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            return false;
        }
    }

    // 2) flags: --url= / --token= or split form
    auto get_flag_value = [&](const std::string& name, int& i) -> std::string {
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
            if (!v.empty()) url = v;
        } else if (a.rfind("--token", 0) == 0) {
            auto v = get_flag_value("--token", i);
            if (!v.empty()) token = v;
        }
    }

    // 3) positional if still empty
    if (url.empty() || token.empty()) {
        std::vector<std::string> pos;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a.rfind("--", 0) == 0) continue; // skip flags we already parsed
            pos.push_back(std::move(a));
        }
        if (pos.size() >= 2) {
            if (url.empty())   url   = pos[0];
            if (token.empty()) token = pos[1];
        }
    }

    // 4) env fallbacks
    if (url.empty()) {
        const char* e = std::getenv("LIVEKIT_URL");
        if (e) url = e;
    }
    if (token.empty()) {
        const char* e = std::getenv("LIVEKIT_TOKEN");
        if (e) token = e;
    }

    return !(url.empty() || token.empty());
}

// ---------------------------------------------------------------------
// SimpleRoomDelegate: analogous to the Python @room.on(...) handlers
// ---------------------------------------------------------------------

class SimpleRoomDelegate : public livekit::RoomDelegate {
public:
    void onParticipantConnected(
        livekit::Room& /*room*/,
        const livekit::ParticipantConnectedEvent& ev) override
    {
        // Python:
        // logger.info("participant connected: %s %s", participant.sid, participant.identity)
        std::cout << "[Room] participant connected: identity="
                  << ev.identity << " name=" << ev.name << "\n";
    }

    void onTrackSubscribed(
        livekit::Room& /*room*/,
        const livekit::TrackSubscribedEvent& ev) override
    {
        // Python:
        // logger.info("track subscribed: %s", publication.sid)
        std::cout << "[Room] track subscribed: participant_identity="
                  << ev.participant_identity
                  << " track_sid=" << ev.track_sid
                  << " name=" << ev.track_name << "\n";

        // Python version also does:
        //   if track.kind == KIND_VIDEO:
        //       video_stream = VideoStream(track)
        //       asyncio.ensure_future(receive_frames(video_stream))
        //
        // In C++, you'd typically spawn a thread or use your own executor here
        // once you have a concrete Track / VideoStream API.
        //
        // TODO: when you expose Track kind/source here, you can check whether
        //       this is a video track and start a VideoStream-like consumer.
    }
};

} // namespace

int main(int argc, char* argv[]) {
    std::string url, token;
    if (!parse_args(argc, argv, url, token)) {
        print_usage(argv[0]);
        return 1;
    }

    // exit if token and url are not set (similar to Python example)
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
    << "  Active recording: " << (info.active_recording ? "yes" : "no") << "\n"
    << "  Empty timeout (s): " << info.empty_timeout << "\n"
    << "  Departure timeout (s): " << info.departure_timeout << "\n"
    << "  Lossy DC low threshold: " << info.lossy_dc_buffered_amount_low_threshold << "\n"
    << "  Reliable DC low threshold: " << info.reliable_dc_buffered_amount_low_threshold << "\n"
    << "  Creation time (ms): " << info.creation_time << "\n";


    // TODO, implement local and remoteParticipants in the room
    /*
    const auto& participants = room.remoteParticipants(); // e.g. map<string, shared_ptr<RemoteParticipant>>
    for (const auto& [identity, participant] : participants) {
        std::cout << "identity: " << identity << "\n";
        std::cout << "participant sid: " << participant->sid() << "\n";
        std::cout << "participant identity: " << participant->identity() << "\n";
        std::cout << "participant name: " << participant->name() << "\n";
        std::cout << "participant kind: " << static_cast<int>(participant->kind()) << "\n";

        const auto& pubs = participant->trackPublications(); // e.g. map<string, shared_ptr<RemoteTrackPublication>>
        std::cout << "participant track publications: " << pubs.size() << "\n";
        for (const auto& [tid, publication] : pubs) {
            std::cout << "\ttrack id: " << tid << "\n";
            std::cout << "\t\ttrack publication sid: " << publication->sid() << "\n";
            std::cout << "\t\ttrack kind: " << static_cast<int>(publication->kind()) << "\n";
            std::cout << "\t\ttrack name: " << publication->name() << "\n";
            std::cout << "\t\ttrack source: " << static_cast<int>(publication->source()) << "\n";
        }

        std::cout << "participant metadata: " << participant->metadata() << "\n";
    }*/

    // Keep the app alive until Ctrl-C so we continue receiving events,
    // similar to asyncio.run(main()) keeping the loop running.
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    FfiClient::instance().shutdown();
    std::cout << "Exiting.\n";
    return 0;
}
