#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>

#include "livekit/livekit.h"

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
} // namespace

int main(int argc, char* argv[]) {
    std::string url, token;
    if (!parse_args(argc, argv, url, token)) {
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "Connecting to: " << url << std::endl;

    // Handle Ctrl-C to exit the idle loop
    std::signal(SIGINT, handle_sigint);

    Room room{};
    room.Connect(url.c_str(), token.c_str());

    // TODO: replace with proper event loop / callbacks.
    // For now, keep the app alive until Ctrl-C.
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Exiting.\n";
    return 0;
}
