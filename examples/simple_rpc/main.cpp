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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "livekit/livekit.h"
#include "livekit_ffi.h" // same as simple_room; internal but used here

using namespace livekit;
using namespace std::chrono_literals;

namespace {

// ------------------------------------------------------------
// Global control (same pattern as simple_room)
// ------------------------------------------------------------

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

void printUsage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  " << prog << " <ws-url> <token> [role]\n"
            << "or:\n"
            << "  " << prog
            << " --url=<ws-url> --token=<token> [--role=<role>]\n"
            << "  " << prog
            << " --url <ws-url> --token <token> [--role <role>]\n\n"
            << "Env fallbacks:\n"
            << "  LIVEKIT_URL, LIVEKIT_TOKEN\n"
            << "Role (participant behavior):\n"
            << "  SIMPLE_RPC_ROLE or --role=<caller|greeter|math-genius>\n"
            << "  default: caller\n";
}

inline double nowMs() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Poll the room until a remote participant with the given identity appears,
// or until 'timeout' elapses. Returns true if found, false on timeout.
bool waitForParticipant(Room &room, const std::string &identity,
                        std::chrono::milliseconds timeout) {
  auto start = std::chrono::steady_clock::now();

  while (std::chrono::steady_clock::now() - start < timeout) {
    if (room.remote_participant(identity) != nullptr) {
      return true;
    }
    std::this_thread::sleep_for(100ms);
  }
  return false;
}

// For the caller: wait for a specific peer, and if they don't show up,
// explain why and how to start them in another terminal.
bool ensurePeerPresent(Room &room, const std::string &identity,
                       const std::string &friendly_role, const std::string &url,
                       std::chrono::seconds timeout) {
  std::cout << "[Caller] Waiting up to " << timeout.count() << "s for "
            << friendly_role << " (identity=\"" << identity
            << "\") to join...\n";

  bool present = waitForParticipant(
      room, identity,
      std::chrono::duration_cast<std::chrono::milliseconds>(timeout));

  if (present) {
    std::cout << "[Caller] " << friendly_role << " is present.\n";
    return true;
  }

  // Timed out
  auto info = room.room_info();
  const std::string room_name = info.name;

  std::cout << "[Caller] Timed out after " << timeout.count()
            << "s waiting for " << friendly_role << " (identity=\"" << identity
            << "\").\n";
  std::cout << "[Caller] No participant with identity \"" << identity
            << "\" appears to be connected to room \"" << room_name
            << "\".\n\n";

  std::cout << "To start a " << friendly_role
            << " in another terminal, run:\n\n"
            << "  lk token create -r test -i " << identity
            << " --join --valid-for 99999h --dev --room=" << room_name << "\n"
            << "  ./build/examples/SimpleRpc " << url
            << " $token --role=" << friendly_role << "\n\n";

  return false;
}

// Parse args similar to simple_room, plus optional --role / role positional
bool parseArgs(int argc, char *argv[], std::string &url, std::string &token,
               std::string &role) {
  // --help
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      return false;
    }
  }

  // helper for flags
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

  // flags: --url / --token / --role (with = or split)
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
    } else if (a.rfind("--role", 0) == 0) {
      auto v = get_flag_value("--role", i);
      if (!v.empty())
        role = v;
    }
  }

  std::vector<std::string> pos;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--", 0) == 0)
      continue;
    pos.push_back(std::move(a));
  }
  if (!pos.empty()) {
    if (url.empty() && pos.size() >= 1) {
      url = pos[0];
    }
    if (token.empty() && pos.size() >= 2) {
      token = pos[1];
    }
    if (role.empty() && pos.size() >= 3) {
      role = pos[2];
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
  if (role.empty()) {
    const char *e = std::getenv("SIMPLE_RPC_ROLE");
    if (e)
      role = e;
  }
  if (role.empty()) {
    role = "caller";
  }

  return !(url.empty() || token.empty());
}

// ------------------------------------------------------------
// Tiny helpers for the simple JSON used in the sample
// (to avoid bringing in a json library)
// ------------------------------------------------------------

// create {"key":number}
std::string makeNumberJson(const std::string &key, double value) {
  std::ostringstream oss;
  oss << "{\"" << key << "\":" << value << "}";
  return oss.str();
}

// create {"key":"value"}
std::string makeStringJson(const std::string &key, const std::string &value) {
  std::ostringstream oss;
  oss << "{\"" << key << "\":\"" << value << "\"}";
  return oss.str();
}

// very naive parse of {"key":number}
double parseNumberFromJson(const std::string &json) {
  auto colon = json.find(':');
  if (colon == std::string::npos)
    throw std::runtime_error("invalid json: " + json);
  auto start = colon + 1;
  auto end = json.find_first_of(",}", start);
  std::string num_str = json.substr(start, end - start);
  return std::stod(num_str);
}

// very naive parse of {"key":"value"}
std::string parseStringFromJson(const std::string &json) {
  auto colon = json.find(':');
  if (colon == std::string::npos)
    throw std::runtime_error("invalid json: " + json);
  auto first_quote = json.find('"', colon + 1);
  if (first_quote == std::string::npos)
    throw std::runtime_error("invalid json: " + json);
  auto second_quote = json.find('"', first_quote + 1);
  if (second_quote == std::string::npos)
    throw std::runtime_error("invalid json: " + json);
  return json.substr(first_quote + 1, second_quote - first_quote - 1);
}

// ------------------------------------------------------------
// RPC handler registration (for greeter & math-genius)
// ------------------------------------------------------------

void registerReceiverMethods(Room &greeters_room, Room &math_genius_room) {
  LocalParticipant *greeter_lp = greeters_room.local_participant();
  LocalParticipant *math_genius_lp = math_genius_room.local_participant();

  // arrival
  greeter_lp->registerRpcMethod(
      "arrival",
      [](const RpcInvocationData &data) -> std::optional<std::string> {
        std::cout << "[Greeter] Oh " << data.caller_identity
                  << " arrived and said \"" << data.payload << "\"\n";
        std::this_thread::sleep_for(2s);
        return std::optional<std::string>{"Welcome and have a wonderful day!"};
      });

  // square-root
  math_genius_lp->registerRpcMethod(
      "square-root",
      [](const RpcInvocationData &data) -> std::optional<std::string> {
        double number = parseNumberFromJson(data.payload);
        std::cout << "[Math Genius] I guess " << data.caller_identity
                  << " wants the square root of " << number
                  << ". I've only got " << data.response_timeout_sec
                  << " seconds to respond but I think I can pull it off.\n";
        std::cout << "[Math Genius] *doing math*…\n";
        std::this_thread::sleep_for(2s);
        double result = std::sqrt(number);
        std::cout << "[Math Genius] Aha! It's " << result << "\n";
        return makeNumberJson("result", result);
      });

  // divide
  math_genius_lp->registerRpcMethod(
      "divide",
      [](const RpcInvocationData &data) -> std::optional<std::string> {
        // expect {"dividend":X,"divisor":Y} – we'll parse very lazily
        auto div_pos = data.payload.find("dividend");
        auto dvr_pos = data.payload.find("divisor");
        if (div_pos == std::string::npos || dvr_pos == std::string::npos) {
          throw std::runtime_error("invalid divide payload");
        }

        double dividend = parseNumberFromJson(
            data.payload.substr(div_pos, dvr_pos - div_pos - 1)); // rough slice
        double divisor = parseNumberFromJson(data.payload.substr(dvr_pos));

        std::cout << "[Math Genius] " << data.caller_identity
                  << " wants to divide " << dividend << " by " << divisor
                  << ".\n";

        if (divisor == 0.0) {
          // will be translated to APPLICATION_ERROR by your RpcError logic
          throw std::runtime_error("division by zero");
        }

        double result = dividend / divisor;
        return makeNumberJson("result", result);
      });

  // long-calculation
  math_genius_lp->registerRpcMethod(
      "long-calculation",
      [](const RpcInvocationData &data) -> std::optional<std::string> {
        std::cout << "[Math Genius] Starting a very long calculation for "
                  << data.caller_identity << "\n";
        std::cout << "[Math Genius] This will take 30 seconds even though "
                     "you're only giving me "
                  << data.response_timeout_sec << " seconds\n";

        std::this_thread::sleep_for(30s);
        return makeStringJson("result", "Calculation complete!");
      });

  // Note: we do NOT register "quantum-hypergeometric-series" here,
  // so the caller sees UNSUPPORTED_METHOD, just like in Python.
}

// ------------------------------------------------------------
// Caller-side helpers (like perform_* in rpc.py)
// ------------------------------------------------------------

void performGreeting(Room &room) {
  std::cout << "[Caller] Letting the greeter know that I've arrived\n";
  double t0 = nowMs();
  try {
    std::string response = room.local_participant()->performRpc(
        "greeter", "arrival", "Hello", std::nullopt);
    double t1 = nowMs();
    std::cout << "[Caller] RTT: " << (t1 - t0) << " ms\n";
    std::cout << "[Caller] That's nice, the greeter said: \"" << response
              << "\"\n";
  } catch (const std::exception &error) {
    double t1 = nowMs();
    std::cout << "[Caller] (FAILED) RTT: " << (t1 - t0) << " ms\n";
    std::cout << "[Caller] RPC call failed: " << error.what() << "\n";
    throw;
  }
}

void performSquareRoot(Room &room) {
  std::cout << "[Caller] What's the square root of 16?\n";
  double t0 = nowMs();
  try {
    std::string payload = makeNumberJson("number", 16.0);
    std::string response = room.local_participant()->performRpc(
        "math-genius", "square-root", payload, std::nullopt);
    double t1 = nowMs();
    std::cout << "[Caller] RTT: " << (t1 - t0) << " ms\n";
    double result = parseNumberFromJson(response);
    std::cout << "[Caller] Nice, the answer was " << result << "\n";
  } catch (const std::exception &error) {
    double t1 = nowMs();
    std::cout << "[Caller] (FAILED) RTT: " << (t1 - t0) << " ms\n";
    std::cout << "[Caller] RPC call failed: " << error.what() << "\n";
    throw;
  }
}

void performQuantumHyperGeometricSeries(Room &room) {
  std::cout << "\n=== Unsupported Method Example ===\n";
  std::cout
      << "[Caller] Asking math-genius for 'quantum-hypergeometric-series'. "
         "This should FAIL because the handler is NOT registered.\n";
  double t0 = nowMs();
  try {
    std::string payload = makeNumberJson("number", 42.0);
    std::string response = room.local_participant()->performRpc(
        "math-genius", "quantum-hypergeometric-series", payload, std::nullopt);
    double t1 = nowMs();
    std::cout << "[Caller] (Unexpected success) RTT=" << (t1 - t0) << " ms\n";
    std::cout << "[Caller] Result: " << response << "\n";
  } catch (const RpcError &error) {
    double t1 = nowMs();
    std::cout << "[Caller] RpcError RTT=" << (t1 - t0) << " ms\n";
    auto code = static_cast<RpcError::ErrorCode>(error.code());
    if (code == RpcError::ErrorCode::UNSUPPORTED_METHOD) {
      std::cout << "[Caller] ✓ Expected: math-genius does NOT implement this "
                   "method.\n";
      std::cout << "[Caller] Server returned UNSUPPORTED_METHOD.\n";
    } else {
      std::cout << "[Caller] ✗ Unexpected error type: " << error.message()
                << "\n";
    }
  }
}

void performDivide(Room &room) {
  std::cout << "\n=== Divide Example ===\n";
  std::cout << "[Caller] Asking math-genius to divide 10 by 0. "
               "This is EXPECTED to FAIL with an APPLICATION_ERROR.\n";
  double t0 = nowMs();
  try {
    std::string payload = "{\"dividend\":10,\"divisor\":0}";
    std::string response = room.local_participant()->performRpc(
        "math-genius", "divide", payload, std::nullopt);
    double t1 = nowMs();
    std::cout << "[Caller] (Unexpected success) RTT=" << (t1 - t0) << " ms\n";
    std::cout << "[Caller] Result = " << response << "\n";
  } catch (const RpcError &error) {
    double t1 = nowMs();
    std::cout << "[Caller] RpcError RTT=" << (t1 - t0) << " ms\n";
    auto code = static_cast<RpcError::ErrorCode>(error.code());
    if (code == RpcError::ErrorCode::APPLICATION_ERROR) {
      std::cout << "[Caller] ✓ Expected: divide-by-zero triggers "
                   "APPLICATION_ERROR.\n";
      std::cout << "[Caller] Math-genius threw an exception: "
                << error.message() << "\n";
    } else {
      std::cout << "[Caller] ✗ Unexpected RpcError type: " << error.message()
                << "\n";
    }
  }
}

void performLongCalculation(Room &room) {
  std::cout << "\n=== Long Calculation Example ===\n";
  std::cout
      << "[Caller] Asking math-genius for a calculation that takes 30s.\n";
  std::cout
      << "[Caller] Giving only 10s to respond. EXPECTED RESULT: TIMEOUT.\n";
  double t0 = nowMs();
  try {
    std::string response = room.local_participant()->performRpc(
        "math-genius", "long-calculation", "{}", 10.0);
    double t1 = nowMs();
    std::cout << "[Caller] (Unexpected success) RTT=" << (t1 - t0) << " ms\n";
    std::cout << "[Caller] Result: " << response << "\n";
  } catch (const RpcError &error) {
    double t1 = nowMs();
    std::cout << "[Caller] RpcError RTT=" << (t1 - t0) << " ms\n";
    auto code = static_cast<RpcError::ErrorCode>(error.code());
    if (code == RpcError::ErrorCode::RESPONSE_TIMEOUT) {
      std::cout
          << "[Caller] ✓ Expected: handler sleeps 30s but timeout is 10s.\n";
      std::cout << "[Caller] Server correctly returned RESPONSE_TIMEOUT.\n";
    } else if (code == RpcError::ErrorCode::RECIPIENT_DISCONNECTED) {
      std::cout << "[Caller] ✓ Expected if math-genius disconnects during the "
                   "test.\n";
    } else {
      std::cout << "[Caller] ✗ Unexpected RPC error: " << error.message()
                << "\n";
    }
  }
}

} // namespace

// ------------------------------------------------------------
// main – similar style to simple_room/main.cpp
// ------------------------------------------------------------

int main(int argc, char *argv[]) {
  std::string url, token, role;
  if (!parseArgs(argc, argv, url, token, role)) {
    printUsage(argv[0]);
    return 1;
  }

  if (url.empty() || token.empty()) {
    std::cerr << "LIVEKIT_URL and LIVEKIT_TOKEN (or CLI args) are required\n";
    return 1;
  }

  std::cout << "Connecting to: " << url << "\n";
  std::cout << "Role: " << role << "\n";

  // Ctrl-C
  std::signal(SIGINT, handleSignal);

  Room room{};
  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  bool res = room.Connect(url, token, options);
  std::cout << "Connect result is " << std::boolalpha << res << "\n";
  if (!res) {
    std::cerr << "Failed to connect to room\n";
    FfiClient::instance().shutdown();
    return 1;
  }

  auto info = room.room_info();
  std::cout << "Connected to room:\n"
            << "  Name: " << info.name << "\n"
            << "  Metadata: " << info.metadata << "\n"
            << "  Num participants: " << info.num_participants << "\n";

  try {
    if (role == "caller") {
      // Check that both peers are present (or explain how to start them).
      bool has_greeter = ensurePeerPresent(room, "greeter", "greeter", url, 8s);
      bool has_math_genius =
          ensurePeerPresent(room, "math-genius", "math-genius", url, 8s);
      if (!has_greeter || !has_math_genius) {
        std::cout << "\n[Caller] One or more RPC peers are missing. "
                  << "Some examples may be skipped.\n";
      }
      if (has_greeter) {
        std::cout << "\n\nRunning greeting example...\n";
        performGreeting(room);
      } else {
        std::cout << "[Caller] Skipping greeting example because greeter is "
                     "not present.\n";
      }
      if (has_math_genius) {
        std::cout << "\n\nRunning error handling example...\n";
        performDivide(room);

        std::cout << "\n\nRunning math example...\n";
        performSquareRoot(room);
        std::this_thread::sleep_for(2s);
        performQuantumHyperGeometricSeries(room);

        std::cout << "\n\nRunning long calculation with timeout...\n";
        performLongCalculation(room);
      } else {
        std::cout << "[Caller] Skipping math examples because math-genius is "
                     "not present.\n";
      }

      std::cout << "\n\nCaller done. Exiting.\n";
    } else if (role == "greeter" || role == "math-genius") {
      // For these roles we expect multiple processes:
      //   - One process with role=caller
      //   - One with role=greeter
      //   - One with role=math-genius
      //
      // Each process gets its own token (with that identity) via LIVEKIT_TOKEN.
      // Here we only register handlers for the appropriate role, and then
      // stay alive until Ctrl-C so we can receive RPCs.

      if (role == "greeter") {
        // Use the same room object for both arguments; only "arrival" is used.
        registerReceiverMethods(room, room);
      } else { // math-genius
        // We only need math handlers; greeter handlers won't be used.
        registerReceiverMethods(room, room);
      }

      std::cout << "RPC handlers registered for role=" << role
                << ". Waiting for RPC calls (Ctrl-C to exit)...\n";

      while (g_running.load()) {
        std::this_thread::sleep_for(50ms);
      }
      std::cout << "Exiting receiver role.\n";
    } else {
      std::cerr << "Unknown role: " << role << "\n";
    }
  } catch (const std::exception &e) {
    std::cerr << "Unexpected error in main: " << e.what() << "\n";
  }

  FfiClient::instance().shutdown();
  return 0;
}
