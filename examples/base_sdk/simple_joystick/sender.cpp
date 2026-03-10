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

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <conio.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "json_utils.h"
#include "livekit/livekit.h"
#include "utils.h"

using namespace livekit;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

// --- Raw terminal input helpers ---

#ifndef _WIN32
struct termios g_orig_termios;
bool g_raw_mode_enabled = false;

void disableRawMode() {
  if (g_raw_mode_enabled) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
    g_raw_mode_enabled = false;
  }
}

void enableRawMode() {
  tcgetattr(STDIN_FILENO, &g_orig_termios);
  g_raw_mode_enabled = true;
  std::atexit(disableRawMode);

  struct termios raw = g_orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON); // disable echo and canonical mode
  raw.c_cc[VMIN] = 0;              // non-blocking read
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Returns -1 if no key is available, otherwise the character code.
int readKeyNonBlocking() {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  struct timeval tv = {0, 0}; // immediate return
  if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
    unsigned char ch;
    if (read(STDIN_FILENO, &ch, 1) == 1)
      return ch;
  }
  return -1;
}
#else
void enableRawMode() { /* Windows _getch() is already unbuffered */ }
void disableRawMode() {}

int readKeyNonBlocking() {
  if (_kbhit())
    return _getch();
  return -1;
}
#endif

void printUsage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  " << prog << " <ws-url> <token>\n"
            << "or:\n"
            << "  " << prog << " --url=<ws-url> --token=<token>\n\n"
            << "Env fallbacks:\n"
            << "  LIVEKIT_URL, LIVEKIT_TOKEN\n\n"
            << "This is the sender. It connects to the room and\n"
            << "continuously checks for a receiver peer every 2 seconds.\n"
            << "Once connected, use keyboard to send joystick commands:\n"
            << "  w / s  = +x / -x\n"
            << "  d / a  = +y / -y\n"
            << "  z / c  = +z / -z\n"
            << "  q      = quit\n"
            << "Automatically reconnects if receiver leaves.\n";
}

void printControls() {
  std::cout << "\n"
            << "  Controls:\n"
            << "    w / s  = +x / -x\n"
            << "    d / a  = +y / -y\n"
            << "    z / c  = +z / -z\n"
            << "    q      = quit\n\n";
}

} // namespace

int main(int argc, char *argv[]) {
  std::string url, token;
  if (!simple_joystick::parseArgs(argc, argv, url, token)) {
    printUsage(argv[0]);
    return 1;
  }

  std::cout << "[Sender] Connecting to: " << url << "\n";
  std::signal(SIGINT, handleSignal);

  livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);
  auto room = std::make_unique<Room>();
  RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;

  bool res = room->Connect(url, token, options);
  std::cout << "[Sender] Connect result: " << std::boolalpha << res << "\n";
  if (!res) {
    std::cerr << "[Sender] Failed to connect to room\n";
    livekit::shutdown();
    return 1;
  }

  auto info = room->room_info();
  std::cout << "[Sender] Connected to room: " << info.name << "\n";

  // Enable raw terminal mode for immediate keypress detection
  enableRawMode();

  std::cout << "[Sender] Waiting for 'robot' to join (checking every 2s)...\n";
  printControls();

  LocalParticipant *lp = room->localParticipant();
  double x = 0.0, y = 0.0, z = 0.0;
  bool receiver_connected = false;
  auto last_receiver_check = std::chrono::steady_clock::now();

  while (g_running.load()) {
    // Periodically check receiver presence every 2 seconds
    auto now = std::chrono::steady_clock::now();
    if (now - last_receiver_check >= 2s) {
      last_receiver_check = now;
      bool receiver_present = (room->remoteParticipant("robot") != nullptr);

      if (receiver_present && !receiver_connected) {
        std::cout << "[Sender] Receiver connected! Use keys to send commands.\n";
        receiver_connected = true;
      } else if (!receiver_present && receiver_connected) {
        std::cout << "[Sender] Receiver disconnected. Waiting for reconnect...\n";
        receiver_connected = false;
      }
    }

    // Poll for keypress (non-blocking)
    int key = readKeyNonBlocking();
    if (key == -1) {
      std::this_thread::sleep_for(20ms); // avoid busy-wait
      continue;
    }

    // Handle quit
    if (key == 'q' || key == 'Q') {
      std::cout << "\n[Sender] Quit requested.\n";
      break;
    }

    // Map key to axis change
    bool changed = false;
    switch (key) {
    case 'w':
    case 'W':
      x += 1.0;
      changed = true;
      break;
    case 's':
    case 'S':
      x -= 1.0;
      changed = true;
      break;
    case 'd':
    case 'D':
      y += 1.0;
      changed = true;
      break;
    case 'a':
    case 'A':
      y -= 1.0;
      changed = true;
      break;
    case 'z':
    case 'Z':
      z += 1.0;
      changed = true;
      break;
    case 'c':
    case 'C':
      z -= 1.0;
      changed = true;
      break;
    default:
      break;
    }

    if (!changed)
      continue;

    if (!receiver_connected) {
      std::cout << "[Sender] (no receiver connected) x=" << x << " y=" << y
                << " z=" << z << "\n";
      continue;
    }

    // Send joystick command via RPC
    simple_joystick::JoystickCommand cmd{x, y, z};
    std::string payload = simple_joystick::joystick_to_json(cmd);

    std::cout << "[Sender] Sending: x=" << x << " y=" << y << " z=" << z << "\n";

    try {
      std::string response =
          lp->performRpc("robot", "joystick_command", payload, 5.0);
      std::cout << "[Sender] Receiver acknowledged: " << response << "\n";
    } catch (const RpcError &e) {
      std::cerr << "[Sender] RPC error: " << e.message() << "\n";
      if (static_cast<RpcError::ErrorCode>(e.code()) ==
          RpcError::ErrorCode::RECIPIENT_DISCONNECTED) {
        std::cout << "[Sender] Receiver disconnected. Waiting for reconnect...\n";
        receiver_connected = false;
      }
    } catch (const std::exception &e) {
      std::cerr << "[Sender] Error sending command: " << e.what() << "\n";
    }
  }

  disableRawMode();

  std::cout << "[Sender] Done. Shutting down.\n";
  room->setDelegate(nullptr);
  room.reset();
  livekit::shutdown();
  return 0;
}
