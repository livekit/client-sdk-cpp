<!--BEGIN_BANNER_IMAGE-->

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="/.github/banner_dark.png">
  <source media="(prefers-color-scheme: light)" srcset="/.github/banner_light.png">
  <img style="width:100%;" alt="The LiveKit icon, the name of the repository and some sample code in the background." src="https://raw.githubusercontent.com/livekit/client-sdk-cpp/main/.github/banner_light.png">
</picture>

<!--END_BANNER_IMAGE-->

# C++ client SDK for LiveKit

<!--BEGIN_DESCRIPTION-->
Use this SDK to add realtime video, audio and data features to your C++ app. By connecting to <a href="https://livekit.io/">LiveKit</a> Cloud or a self-hosted server, you can quickly build applications such as multimodal AI, live streaming, or video calls with just a few lines of code.
<!--END_DESCRIPTION-->

[![Builds](https://github.com/livekit/client-sdk-cpp/actions/workflows/builds.yml/badge.svg?branch=main)](https://github.com/livekit/client-sdk-cpp/actions/workflows/builds.yml)
[![Tests](https://github.com/livekit/client-sdk-cpp/actions/workflows/tests.yml/badge.svg?branch=main)](https://github.com/livekit/client-sdk-cpp/actions/workflows/tests.yml)

## Docs

- [LiveKit docs](https://docs.livekit.io)
- [SDK reference](https://docs.livekit.io/reference/client-sdk-cpp/)
- [Repository docs](./docs/README.md)

## Using the SDK

[CMake](https://cmake.org/) (≥ 3.20) is used for building the SDK itself and for consuming it as a library. The
[**cpp-example-collection**](https://github.com/livekit-examples/cpp-example-collection) contains a reference [LiveKitSDK.cmake](https://github.com/livekit-examples/cpp-example-collection/blob/main/cmake/LiveKitSDK.cmake)
which downloads the latest stable release at CMake configure time. See [docs/building.md](docs/building.md) for additional documentation on integrating LiveKit into your project.

To build the SDK from source:

```bash
git clone --recurse-submodules https://github.com/livekit/client-sdk-cpp.git
cd client-sdk-cpp
./build.sh release  # or .\build.cmd release on Windows
```

Building requires a stable Rust toolchain and platform-specific build
deps (`protobuf` for `protoc`/protobuf-lite, `abseil`, `openssl` on Linux). See [docs/building.md](docs/building.md)
for full prerequisites table, Docker recipe, CMake presets, and troubleshooting.

### Hello, LiveKit

Here is a minimal example of the `main` function for sending and receiving video and data track frames. The [sender](https://github.com/livekit-examples/cpp-example-collection/blob/main/hello_livekit/sender/main.cpp) plays the role of a robot or camera, publishing video and data track frames every 100 ms; the [receiver](https://github.com/livekit-examples/cpp-example-collection/blob/main/hello_livekit/receiver/main.cpp) stands in for the cloud service or operator UI, logging every frame it sees. In a production system the synthetic video would be a robot's perception output and the data track would carry sensor readings or operator commands, but the connection and publishing pattern is the same. Full source for both processes lives in the [cpp-example-collection](https://github.com/livekit-examples/cpp-example-collection/tree/main/hello_livekit) repo.

The sender creates tracks and publishes data.

**Initialize LiveKit and connect to the room**

```cpp
#include "livekit/livekit.h"

// Get your url and token from env vars, args, etc.
const std::string url = "wss://hello.livekit.cloud";
const std::string token = "sender_token";

// Start the LiveKit SDK before creating rooms or tracks.
livekit::initialize(livekit::LogLevel::Info);

// Set your room options, here we will use defaults.
livekit::RoomOptions options;

// Create the room & connect to the room using a server URL and participant token.
auto room = std::make_unique<livekit::Room>();
if (!room->connect(url, token, options)) {
  std::cerr << "Failed to connect to LiveKit\n";
  return 1;
}
```

**Create the VideoSource, which provides frames to the VideoTrack, then create and publish the VideoTrack**

```cpp
// Get the local participant to create tracks.
auto participant = room->localParticipant().lock();
if (!participant)
{
  std::cerr << "Unable to get the local participant!" << std::endl;
  return 1;
}

// Publish a synthetic camera track named "camera0" backed by a VideoSource.
auto video_source = std::make_shared<livekit::VideoSource>(640, 480);
auto video_track = participant->publishVideoTrack("camera0", video_source, livekit::TrackSource::SOURCE_CAMERA);
if (!video_track) {
  std::cerr << "Failed to publish video track\n";
  return 1;
}
```

**Create and publish the DataTrack**

```cpp
// Publish a data track named "app-data" for app messages.
auto data_track_result = participant->publishDataTrack("app-data");
if (!data_track_result) {
  std::cerr << "Failed to publish data track\n";
  return 1;
}
auto data_track = data_track_result.value();

// Release the participant to reduce scope.
participant.reset();
```

**Publish video and data track frames every 100ms**

```cpp
int count = 0;

while (true)
{
  // Create a 640x480 RGBA video frame.
  auto vf = livekit::VideoFrame::create(640, 480, livekit::VideoBufferType::RGBA);

  // Capture the frame. This publishes the frame on VideoTrack camera0.
  video_source->captureFrame(vf);

  const std::string message = "hello #" + std::to_string(count);
  const auto now_microsec =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

  // Create and push stamped DataTrackFrame.
  const livekit::DataTrackFrame frame{
        std::vector<std::uint8_t>(message.begin(), message.end()),
        now_microsec,
        };

  // Optionally, capture the result.
  auto push_result = data_track->tryPush(frame);
  if (!push_result) {
    const auto& error = push_result.error();
    std::cerr << "[warn] Failed to push data frame: code=" << static_cast<std::uint32_t>(error.code)
              << " message=" << error.message << "\n";
  }

  ++count;
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```

The receiver side setup is the same, except now we set callbacks to the relevant tracks.

**Initialize LiveKit and Connect to the room**

```cpp
#include "livekit/livekit.h"

// Get your url and token from env vars, args, etc.
const std::string url = "wss://hello.livekit.cloud";
const std::string token = "receiver_token";

// Start the LiveKit SDK before creating rooms or tracks.
livekit::initialize(livekit::LogLevel::Info);

// Set your room options, here we use the defaults.
livekit::RoomOptions options;

// Create the room & connect to the room using a server URL and participant token.
auto room = std::make_unique<livekit::Room>();
if (!room->connect(url, token, options)) {
  std::cerr << "Failed to connect to LiveKit\n";
  return 1;
}
```

**Set callbacks for new video and data frames**

```cpp
// The identity of the participant sending video and data frames.
const std::string sender_identity = "sender_identity";

// Set the callback for new video frames from the sender's "camera0" VideoTrack.
room->setOnVideoFrameCallback(sender_identity, "camera0", [](const livekit::VideoFrame& frame, std::int64_t) {
  std::cout << "video frame: " << frame.width() << "x" << frame.height() << "\n";
});

// Set the callback for new frames on the sender's "app-data" DataTrack.
room->addOnDataFrameCallback(sender_identity, "app-data",
                             [](const std::vector<std::uint8_t>& payload, std::optional<std::uint64_t>) {
                               const std::string message(payload.begin(), payload.end());
                               std::cout << "data message: " << message << "\n";
                             });
```

For end-to-end samples and a fuller set of demos, see the [cpp-example-collection repo](https://github.com/livekit-examples/cpp-example-collection).

## Features

- Connect to LiveKit rooms (Cloud or self-hosted)
- Receive remote audio/video tracks
- Publish local audio/video tracks
- Data tracks (low-level) and data streams (high-level)
- RPC between participants
- End-to-end encryption (E2EE)
- Hardware-accelerated codecs (via the underlying Rust SDK)
- Chromium-style tracing for performance debugging

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
scripts under `scripts/`. Set up the pre-commit auto-formatter
with:

```bash
./scripts/install-pre-commit.sh
```

See [docs/tools.md](docs/tools.md).

## Deprecation

Future deprecations and deprecation dates will be listed here for the next major release.

### `v1.0.0`

>NOTE: With the official 1.0.0 release we have introduced breaking changes to previous unofficial versions in order
to align with other LiveKit client SDKs. See the `v1.0.0` release notes for the full list of changes.

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
