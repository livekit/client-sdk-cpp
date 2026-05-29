<!--BEGIN_BANNER_IMAGE-->

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="/.github/banner_dark.png">
  <source media="(prefers-color-scheme: light)" srcset="/.github/banner_light.png">
  <img style="width:100%;" alt="The LiveKit icon, the name of the repository and some sample code in the background." src="https://raw.githubusercontent.com/livekit/client-sdk-cpp/main/.github/banner_light.png">
</picture>

<!--END_BANNER_IMAGE-->

# C++ SDK for LiveKit

<!--BEGIN_DESCRIPTION-->
Use this SDK to add realtime video, audio and data features to your C++ app. By connecting to <a href="https://livekit.io/">LiveKit</a> Cloud or a self-hosted server, you can quickly build applications such as multi-modal AI, live streaming, or video calls with just a few lines of code.
<!--END_DESCRIPTION-->

## Docs

- [API reference](https://docs.livekit.io/reference/client-sdk-cpp/) (Doxygen)
- LiveKit docs: [docs.livekit.io](https://docs.livekit.io)

## Install

The fastest way to consume the SDK is via the
[**cpp-example-collection**](https://github.com/livekit-examples/cpp-example-collection),
which downloads a prebuilt release at CMake configure time — no source build
required.

If you do want to build from source, the short version is:

```bash
git clone --recurse-submodules https://github.com/livekit/client-sdk-cpp.git
cd client-sdk-cpp
./build.sh release        # or .\build.cmd release on Windows
```

You'll need CMake ≥ 3.20, a stable Rust toolchain, and platform-specific build
deps (protobuf, abseil, openssl on Linux). See [docs/building.md](docs/building.md)
for the full prerequisites table, Docker recipe, CMake presets, and
troubleshooting.

## Hello world

A minimal program that initializes the SDK, connects to a room, and prints
events as remote tracks are subscribed:

```cpp
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

#include <livekit/livekit.h>

class Delegate : public livekit::RoomDelegate {
public:
  void onParticipantConnected(livekit::Room&,
                              const livekit::ParticipantConnectedEvent& ev) override {
    std::cout << "participant joined: " << ev.participant->identity() << "\n";
  }

  void onTrackSubscribed(livekit::Room&,
                         const livekit::TrackSubscribedEvent& ev) override {
    std::cout << "subscribed: kind=" << static_cast<int>(ev.track->kind())
              << " sid=" << ev.publication->sid() << "\n";
    // For real apps: wrap `ev.track` in AudioStream / VideoStream to receive frames.
  }
};

std::atomic<bool> running{true};

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "usage: " << argv[0] << " <ws-url> <token>\n";
    return 1;
  }
  std::signal(SIGINT, [](int) { running = false; });

  livekit::initialize();

  Delegate delegate;
  auto room = std::make_unique<livekit::Room>();
  room->setDelegate(&delegate);

  livekit::RoomOptions options;
  options.auto_subscribe = true;
  if (!room->connect(argv[1], argv[2], options)) {
    std::cerr << "connect failed\n";
    return 1;
  }

  while (running) std::this_thread::sleep_for(std::chrono::milliseconds(100));

  livekit::shutdown();
  return 0;
}
```

For runnable variants (publishing audio, video rendering with SDL, RPC, data
streams, E2EE), see the
[cpp-example-collection](https://github.com/livekit-examples/cpp-example-collection).

## Features

- [x] Connect to LiveKit rooms (Cloud or self-hosted)
- [x] Receive remote audio/video tracks
- [x] Publish local audio/video tracks
- [x] Data tracks (low-level) and data streams (high-level)
- [x] RPC between participants
- [x] End-to-end encryption (E2EE)
- [x] Hardware-accelerated codecs (via the underlying Rust SDK)
- [x] Chromium-style tracing for performance debugging

Supported platforms: **Linux** (x64, arm64), **macOS** (12.3+, Apple Silicon
& Intel), **Windows** (x64).

## Logging & tracing

The SDK uses a thin public logging API in `<livekit/logging.h>` with both
compile-time and runtime filtering. Plug your own sink in (file, JSON,
syslog, ROS2 `RCLCPP_*`) via `setLogCallback`. See
[docs/logging.md](docs/logging.md).

Chromium-format performance traces can be captured with `startTracing` /
`stopTracing` and viewed in `chrome://tracing` or
[ui.perfetto.dev](https://ui.perfetto.dev). See [docs/tracing.md](docs/tracing.md).

## Testing

Integration and stress test suites live under `src/tests/`. Build them with
`./build.sh debug-tests`, point `LIVEKIT_URL` + two participant tokens at a
local `livekit-server --dev`, and run via `ctest` or directly. See
[docs/testing.md](docs/testing.md).

## Developer tools

`clang-tidy`, `clang-format`, `valgrind`, and Doxygen are all wired up via
scripts under [`scripts/`](scripts/). Set up the pre-commit auto-formatter
with:

```bash
./scripts/install-pre-commit.sh
```

See [docs/tools.md](docs/tools.md) for the rest.

## Deprecation

| Component                                                         | Removed on |
| ----------------------------------------------------------------- | ---------- |
| `livekit_bridge` (bridge/ folder) — migrate to the base SDK       | 2026-06-01 |
| `setOn*FrameCallback` with `TrackSource` — use track name instead | 2026-06-01 |
| Public-header symbols that don't follow `camelBack()`             | 2026-06-01 |

## Contributing

PRs welcome. Issues: <https://github.com/livekit/client-sdk-cpp/issues>.

<!--BEGIN_REPO_NAV-->
<br/><table>
<thead><tr><th colspan="2">LiveKit Ecosystem</th></tr></thead>
<tbody>
<tr><td>Agents SDKs</td><td><a href="https://github.com/livekit/agents">Python</a> · <a href="https://github.com/livekit/agents-js">Node.js</a></td></tr>
<tr><td>LiveKit SDKs</td><td><a href="https://github.com/livekit/client-sdk-js">Browser</a> · <a href="https://github.com/livekit/client-sdk-swift">Swift</a> · <a href="https://github.com/livekit/client-sdk-android">Android</a> · <a href="https://github.com/livekit/client-sdk-flutter">Flutter</a> · <a href="https://github.com/livekit/client-sdk-react-native">React Native</a> · <a href="https://github.com/livekit/rust-sdks">Rust</a> · <a href="https://github.com/livekit/node-sdks">Node.js</a> · <a href="https://github.com/livekit/python-sdks">Python</a> · <a href="https://github.com/livekit/client-sdk-unity">Unity</a> · <a href="https://github.com/livekit/client-sdk-unity-web">Unity (WebGL)</a> · <a href="https://github.com/livekit/client-sdk-esp32">ESP32</a> · <b>C++</b></td></tr>
<tr><td>Starter Apps</td><td><a href="https://github.com/livekit-examples/agent-starter-python">Python Agent</a> · <a href="https://github.com/livekit-examples/agent-starter-node">TypeScript Agent</a> · <a href="https://github.com/livekit-examples/agent-starter-react">React App</a> · <a href="https://github.com/livekit-examples/agent-starter-swift">SwiftUI App</a> · <a href="https://github.com/livekit-examples/agent-starter-android">Android App</a> · <a href="https://github.com/livekit-examples/agent-starter-flutter">Flutter App</a> · <a href="https://github.com/livekit-examples/agent-starter-react-native">React Native App</a> · <a href="https://github.com/livekit-examples/agent-starter-embed">Web Embed</a></td></tr>
<tr><td>UI Components</td><td><a href="https://github.com/livekit/components-js">React</a> · <a href="https://github.com/livekit/components-android">Android Compose</a> · <a href="https://github.com/livekit/components-swift">SwiftUI</a> · <a href="https://github.com/livekit/components-flutter">Flutter</a></td></tr>
<tr><td>Server APIs</td><td><a href="https://github.com/livekit/node-sdks">Node.js</a> · <a href="https://github.com/livekit/server-sdk-go">Golang</a> · <a href="https://github.com/livekit/server-sdk-ruby">Ruby</a> · <a href="https://github.com/livekit/server-sdk-kotlin">Java/Kotlin</a> · <a href="https://github.com/livekit/python-sdks">Python</a> · <a href="https://github.com/livekit/rust-sdks">Rust</a> · <a href="https://github.com/agence104/livekit-server-sdk-php">PHP (community)</a> · <a href="https://github.com/pabloFuente/livekit-server-sdk-dotnet">.NET (community)</a></td></tr>
<tr><td>Resources</td><td><a href="https://docs.livekit.io">Docs</a> · <a href="https://docs.livekit.io/mcp">Docs MCP Server</a> · <a href="https://github.com/livekit/livekit-cli">CLI</a> · <a href="https://cloud.livekit.io">LiveKit Cloud</a></td></tr>
<tr><td>LiveKit Server OSS</td><td><a href="https://github.com/livekit/livekit">LiveKit server</a> · <a href="https://github.com/livekit/egress">Egress</a> · <a href="https://github.com/livekit/ingress">Ingress</a> · <a href="https://github.com/livekit/sip">SIP</a></td></tr>
<tr><td>Community</td><td><a href="https://community.livekit.io">Developer Community</a> · <a href="https://livekit.io/join-slack">Slack</a> · <a href="https://x.com/livekit">X</a> · <a href="https://www.youtube.com/@livekit_io">YouTube</a></td></tr>
</tbody>
</table>
<!--END_REPO_NAV-->
