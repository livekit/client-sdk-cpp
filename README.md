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

## 📦 Requirements
- **CMake** ≥ 3.20
- **Rust / Cargo** (latest stable toolchain)
- **Git LFS** (required for examples)
  Some example data files (e.g., audio assets) are stored using Git LFS.
  You must install Git LFS before cloning or pulling the repo if you want to run the examples.
- **livekit-cli** install livekit-cli by following the (official livekit docs)[https://docs.livekit.io/intro/basics/cli/start/]
- **livekit-server** install livekit-server by following the (official livekit docs)[https://docs.livekit.io/transport/self-hosting/local/]

**Platform-Specific Requirements:**

### For Building the SDK:
- **Windows:** Visual Studio 2019+, vcpkg
- **Linux:** `sudo apt install libprotobuf-dev libssl-dev` (protobuf 3.x)
- **macOS:** `brew install protobuf` (protobuf 3.x)

### For Using the Pre-built SDK:
- **Windows:** ✅ All dependencies included (DLLs bundled) - ready to use
- **Linux:** ⚠️ Requires `libprotobuf` and `libssl-dev`; deploy `liblivekit_ffi.so` with your executable
- **macOS:** ⚠️ Requires `protobuf`; deploy `liblivekit_ffi.dylib` with your executable

> **Note**: If the SDK was built with Protobuf 6.0+, you also need `libabsl-dev` (Linux) or `abseil` (macOS).

## 🧩 Clone the Repository

Make sure to initialize the Rust submodule (`client-sdk-rust`):

```bash
# Option 1: Clone with submodules in one step
git clone --recurse-submodules https://github.com/livekit/client-sdk-cpp.git

# Option 2: Clone first, then initialize submodules
git clone https://github.com/livekit/client-sdk-cpp.git
cd client-sdk-cpp
git submodule update --init --recursive
```

## ⚙️ BUILD

### Quick Build (Using Build Scripts)

**Linux/macOS:**
```bash
./build.sh clean          # Clean CMake build artifacts
./build.sh clean-all      # Deep clean (C++ + Rust + generated files)
./build.sh debug          # Build Debug version
./build.sh release        # Build Release version
./build.sh debug-tests    # Build Debug with tests
./build.sh release-tests  # Build Release with tests
```
**Windows**
Using build scripts:
```powershell
.\build.cmd clean          # Clean CMake build artifacts
.\build.cmd clean-all      # Deep clean (C++ + Rust + generated files)
.\build.cmd debug          # Build Debug version
.\build.cmd release        # Build Release version
.\build.cmd debug-tests    # Build Debug with tests
.\build.cmd release-tests  # Build Release with tests
```

### Windows build using cmake/vcpkg
```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake"  # Generate Makefiles in build folder
# Build (Release or Debug)
cmake --build build --config Release
# or:
cmake --build build --config Debug
# Clean CMake build artifacts
Remove-Item -Recurse -Force build
```
Note (Windows), This assumes vcpkg is checked out in the repo root at .\vcpkg\.
You must install protobuf via vcpkg (so CMake can find ProtobufConfig.cmake and protoc), for example:
```bash
.\vcpkg\vcpkg install protobuf:x64-windows
```

### Advanced Build (Using CMake Presets)

For more control and platform-specific builds, see the detailed instructions in [README_BUILD.md](README_BUILD.md).

**Prerequisites (Windows only):**
- Set `VCPKG_ROOT` environment variable pointing to your vcpkg installation

```powershell
# Windows PowerShell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
```

**Prerequisites (Linux/macOS):**
- Install system dependencies (see above)

**Quick start:**
```bash
# Windows
cmake --preset windows-release
cmake --build --preset windows-release

# Linux
cmake --preset linux-release
cmake --build --preset linux-release

# macOS
cmake --preset macos-release
cmake --build --preset macos-release
```

📖 **For complete build instructions, troubleshooting, and platform-specific notes, see [README_BUILD.md](README_BUILD.md)**

### Building with Docker
The Dockerfile COPYs folders/files required to build the CPP SDK into the image. 
 **NOTE:** this has only been tested on Linux
```bash
docker build -t livekit-cpp-sdk . -f docker/Dockerfile
docker run -it --network host livekit-cpp-sdk:latest bash
```

__NOTE:__ if you are building your own Dockerfile, you will likely need to set the same `ENV` variables as in `docker/Dockerfile`, but to the relevant directories:
```bash
export CC=$HOME/gcc-14/bin/gcc
export CXX=$HOME/gcc-14/bin/g++
export LD_LIBRARY_PATH=$HOME/gcc-14/lib64:$LD_LIBRARY_PATH
export PATH=$HOME/.cargo/bin:$PATH
export PATH=$HOME/cmake-3.31/bin:$PATH
```

## 🧪 Run Example

### Generate Tokens
Before running any participant, create JWT tokens with the proper identity and room name, example
```bash
lk token create -r test -i your_own_identity  --join --valid-for 99999h --dev --room=your_own_room
```

### SimpleRoom

```bash
./build/examples/SimpleRoom --url $URL --token <jwt-token>
```

You can also provide the URL and token via environment variables:
```bash
export LIVEKIT_URL=ws://localhost:7880
export LIVEKIT_TOKEN=<jwt-token>
./build/examples/SimpleRoom
```

**End-to-End Encryption (E2EE)**
You can enable E2E encryption for the streams via --enable_e2ee and --e2ee_key flags,
by running the following cmds in two terminals or computers. **Note, jwt_token needs to be different identity**
```bash
./build/examples/SimpleRoom --url $URL --token <jwt-token> --enable_e2ee --e2ee_key="your_key"
```
**Note**, **all participants must use the exact same E2EE configuration and shared key.**
If the E2EE keys do not match between participants:
- Media cannot be decrypted
- Video tracks will appear as a black screen
- Audio will be silent
- No explicit error may be shown at the UI level

Press Ctrl-C to exit the example.

### SimpleRpc
The SimpleRpc example demonstrates how to:
- Connect multiple participants to the same LiveKit room
- Register RPC handlers (e.g., arrival, square-root, divide, long-calculation)
- Send RPC requests from one participant to another
- Handle success, application errors, unsupported methods, and timeouts
- Observe round-trip times (RTT) for each RPC call

#### 🔑 Generate Tokens
Before running any participant, create JWT tokens with **caller**, **greeter** and **math-genius** identities and room name.
```bash
lk token create -r test -i caller --join --valid-for 99999h --dev --room=your_own_room
lk token create -r test -i greeter --join --valid-for 99999h --dev --room=your_own_room
lk token create -r test -i math-genius --join --valid-for 99999h --dev --room=your_own_room
```

#### ▶ Start Participants
Every participant is run as a separate terminal process, note --role needs to match the token identity.
```bash
./build/examples/SimpleRpc --url $URL --token <jwt-token> --role=math-genius
```
The caller will automatically:
- Wait for the greeter and math-genius to join
- Perform RPC calls
- Print round-trip times
- Annotate expected successes or expected failures

### SimpleDataStream
- The SimpleDataStream example demonstrates how to:
- Connect multiple participants to the same LiveKit room
- Register text stream and byte stream handlers by topic (e.g. "chat", "files")
- Send a text stream (chat message) from one participant to another
- Send a byte stream (file/image) from one participant to another
- Attach custom stream metadata (e.g. sent_ms) via stream attributes
- Measure and print one-way latency on the receiver using sender timestamps
- Receive streamed chunks and reconstruct the full payload on the receiver

#### 🔑 Generate Tokens
Before running any participant, create JWT tokens with caller and greeter identities and your room name.
```bash
lk token create -r test -i caller  --join --valid-for 99999h --dev --room=your_own_room
lk token create -r test -i greeter --join --valid-for 99999h --dev --room=your_own_room
```

#### ▶ Start Participants
Start the receiver first (so it registers stream handlers before messages arrive):
```bash
./build/examples/SimpleDataStream --url $URL --token <jwt-token>
```
On another terminal or computer, start the sender
```bash
./build/examples/SimpleDataStream --url $URL --token <jwt-token>
```

**Sender** (e.g. greeter)
- Waits for the peer, then sends a text stream ("chat") and a file stream ("files") with timestamps and metadata, logging stream IDs and send times.

**Receiver** (e.g. caller)
- Registers handlers for text and file streams, logs stream events, computes one-way latency, and saves the received file locally.


## Logging

The SDK uses [spdlog](https://github.com/gabime/spdlog) internally but does
**not** expose it in public headers. All log output goes through a thin public
API in `<livekit/logging.h>`.

### Two-tier filtering

| Tier | When | How | Cost |
|------|------|-----|------|
| **Compile-time** | CMake configure | `-DLIVEKIT_LOG_LEVEL=WARN` | Zero -- calls below the level are stripped from the binary |
| **Runtime** | Any time after `initialize()` | `livekit::setLogLevel(LogLevel::Warn)` | Minimal -- a level check before formatting |

#### Compile-time level (`LIVEKIT_LOG_LEVEL`)

Set once when you configure CMake. Calls below this threshold are completely
removed by the preprocessor -- no format-string evaluation, no function call.

```bash
# Development (default): keep everything available
cmake -DLIVEKIT_LOG_LEVEL=TRACE ..

# Release: strip TRACE / DEBUG / INFO
cmake -DLIVEKIT_LOG_LEVEL=WARN ..

# Production: only ERROR and CRITICAL survive
cmake -DLIVEKIT_LOG_LEVEL=ERROR ..
```

Valid values: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `CRITICAL`, `OFF`.

#### Runtime level (`setLogLevel`)

Among the levels that survived compilation you can still filter at runtime
without rebuilding:

```cpp
#include <livekit/livekit.h>

livekit::initialize();                           // default level: Info
livekit::setLogLevel(livekit::LogLevel::Debug);  // show more detail
livekit::setLogLevel(livekit::LogLevel::Warn);   // suppress info chatter
```

### Custom log callback

Replace the default stderr sink with your own handler. This is the integration
point for frameworks like ROS2 (`RCLCPP_*` macros), Android logcat, or any
structured-logging pipeline:

```cpp
#include <livekit/livekit.h>

livekit::initialize();
livekit::setLogLevel(livekit::LogLevel::Trace);

livekit::setLogCallback(
    [](livekit::LogLevel level,
       const std::string &logger_name,
       const std::string &message) {
      // Route to your framework, e.g.:
      //   RCLCPP_INFO(get_logger(), "[%s] %s", logger_name.c_str(), message.c_str());
      myLogger.log(level, logger_name, message);
    });

// Pass nullptr to restore the default stderr sink:
livekit::setLogCallback(nullptr);
```

See [`examples/logging_levels/custom_sinks.cpp`](examples/logging_levels/custom_sinks.cpp)
for three copy-paste-ready patterns: **file logger**, **JSON structured lines**,
and a **ROS2 bridge** that maps `LogLevel` to `RCLCPP_*` macros.

### Available log levels

| Level | Typical use |
|-------|-------------|
| `Trace` | Per-frame / per-packet detail (very noisy) |
| `Debug` | Diagnostic info useful during development |
| `Info` | Normal operational messages (connection, track events) |
| `Warn` | Unexpected but recoverable situations |
| `Error` | Failures that affect functionality |
| `Critical` | Unrecoverable errors |
| `Off` | Suppress all output |

---

## 🧪 Integration & Stress Tests

The SDK includes integration and stress tests using Google Test (gtest).

#### Build Tests

**Linux/macOS:**
```bash
./build.sh debug-tests      # Build Debug with tests
./build.sh release-tests    # Build Release with tests
```

**Windows:**
```powershell
.\build.cmd debug-tests
.\build.cmd release-tests
```

### Run Tests

After building, run tests using ctest or directly:

```bash
# Run all tests via ctest
cd build-debug
ctest --output-on-failure

# Or run test executables directly
./build-debug/bin/livekit_integration_tests
./build-debug/bin/livekit_stress_tests

# Run specific test suites
./build-debug/bin/livekit_integration_tests --gtest_filter="*Rpc*"
./build-debug/bin/livekit_stress_tests --gtest_filter="*MaxPayloadStress*"
```

### Test Types

| Executable | Description |
|------------|-------------|
| `livekit_integration_tests` | Quick tests (~1-2 minutes) for SDK functionality |
| `livekit_stress_tests` | Long-running tests (configurable, default 1 hour) |

### RPC Test Environment Variables

RPC integration and stress tests require a LiveKit server and two participant tokens:

```bash
# Required
export LIVEKIT_URL="wss://your-server.livekit.cloud"
export LIVEKIT_CALLER_TOKEN="<token with caller identity>"
export LIVEKIT_RECEIVER_TOKEN="<token with receiver identity>"

# Optional (for stress tests)
export RPC_STRESS_DURATION_SECONDS=3600   # Test duration (default: 1 hour)
export RPC_STRESS_CALLER_THREADS=4        # Concurrent caller threads (default: 4)
```

**Generate tokens for RPC tests:**
```bash
lk token create -r test -i rpc-caller --join --valid-for 99999h --dev --room=rpc-test-room
lk token create -r test -i rpc-receiver --join --valid-for 99999h --dev --room=rpc-test-room
```

### Test Coverage

- **SDK Initialization**: Initialize/shutdown lifecycle
- **Room**: Room creation, options, connection
- **Audio Frame**: Frame creation, manipulation, edge cases
- **RPC**: Round-trip calls, max payload (15KB), timeouts, errors, concurrent calls
- **Stress Tests**: High throughput, bidirectional RPC, memory pressure

##  🧰 Recommended Setup
### macOS
```bash
brew install cmake protobuf rust
```

### Ubuntu / Debian
```bash
sudo apt update
sudo apt install -y cmake protobuf-compiler build-essential
curl https://sh.rustup.rs -sSf | sh
```

## 🛠️ Development Tips
###  Update Rust version
```bash
cd client-sdk-cpp
git fetch origin
git switch -c try-rust-main origin/main

# Sync submodule URLs and check out what origin/main pins (recursively):
git submodule sync --recursive
git submodule update --init --recursive --checkout

# Now, in case the nested submodule under yuv-sys didn’t materialize, force it explicitly:
cd ..
git -C client-sdk-rust/yuv-sys submodule sync --recursive
git -C client-sdk-rust/yuv-sys submodule update --init --recursive --checkout

# Sanity check:
git submodule status --recursive
```

###  If yuv-sys fails to build
```bash
cargo clean -p yuv-sys
cargo build -p yuv-sys -vv
```

### Full clean (Rust + C++ build folders)
In some cases, you may need to perform a full clean that deletes all build artifacts from both the Rust and C++ folders:
```bash
./build.sh clean-all
```

### Clang format
CPP SDK is using clang C++ format
```bash
brew install clang-format
```


#### Memory Checks
Run valgrind on various examples or tests to check for memory leaks and other issues.
```bash
valgrind --leak-check=full ./build-debug/bin/BridgeRobot
valgrind --leak-check=full ./build-debug/bin/BridgeHuman
valgrind --leak-check=full ./build-debug/bin/livekit_integration_tests
valgrind --leak-check=full ./build-debug/bin/livekit_stress_tests
```

# Running locally
1. Install the livekit-server
https://docs.livekit.io/transport/self-hosting/local/

Start the livekit-server with data tracks enabled:
```bash
LIVEKIT_CONFIG="enable_data_tracks: true" livekit-server --dev
```

```bash
# generate tokens, do for all participants
lk token create \
  --api-key devkey \
  --api-secret secret \
  -i robot \
  --join \
  --valid-for 99999h \
  --room robo_room \
  --grant '{"canPublish":true,"canSubscribe":true,"canPublishData":true}'
```

<!--BEGIN_REPO_NAV-->
<br/><table>
<thead><tr><th colspan="2">LiveKit Ecosystem</th></tr></thead>
<tbody>
<tr><td>Agents SDKs</td><td><a href="https://github.com/livekit/agents">Python</a> · <a href="https://github.com/livekit/agents-js">Node.js</a></td></tr><tr></tr>
<tr><td>LiveKit SDKs</td><td><a href="https://github.com/livekit/client-sdk-js">Browser</a> · <a href="https://github.com/livekit/client-sdk-swift">Swift</a> · <a href="https://github.com/livekit/client-sdk-android">Android</a> · <a href="https://github.com/livekit/client-sdk-flutter">Flutter</a> · <a href="https://github.com/livekit/client-sdk-react-native">React Native</a> · <a href="https://github.com/livekit/rust-sdks">Rust</a> · <a href="https://github.com/livekit/node-sdks">Node.js</a> · <a href="https://github.com/livekit/python-sdks">Python</a> · <a href="https://github.com/livekit/client-sdk-unity">Unity</a> · <a href="https://github.com/livekit/client-sdk-unity-web">Unity (WebGL)</a> · <a href="https://github.com/livekit/client-sdk-esp32">ESP32</a> · <b>C++</b></td></tr><tr></tr>
<tr><td>Starter Apps</td><td><a href="https://github.com/livekit-examples/agent-starter-python">Python Agent</a> · <a href="https://github.com/livekit-examples/agent-starter-node">TypeScript Agent</a> · <a href="https://github.com/livekit-examples/agent-starter-react">React App</a> · <a href="https://github.com/livekit-examples/agent-starter-swift">SwiftUI App</a> · <a href="https://github.com/livekit-examples/agent-starter-android">Android App</a> · <a href="https://github.com/livekit-examples/agent-starter-flutter">Flutter App</a> · <a href="https://github.com/livekit-examples/agent-starter-react-native">React Native App</a> · <a href="https://github.com/livekit-examples/agent-starter-embed">Web Embed</a></td></tr><tr></tr>
<tr><td>UI Components</td><td><a href="https://github.com/livekit/components-js">React</a> · <a href="https://github.com/livekit/components-android">Android Compose</a> · <a href="https://github.com/livekit/components-swift">SwiftUI</a> · <a href="https://github.com/livekit/components-flutter">Flutter</a></td></tr><tr></tr>
<tr><td>Server APIs</td><td><a href="https://github.com/livekit/node-sdks">Node.js</a> · <a href="https://github.com/livekit/server-sdk-go">Golang</a> · <a href="https://github.com/livekit/server-sdk-ruby">Ruby</a> · <a href="https://github.com/livekit/server-sdk-kotlin">Java/Kotlin</a> · <a href="https://github.com/livekit/python-sdks">Python</a> · <a href="https://github.com/livekit/rust-sdks">Rust</a> · <a href="https://github.com/agence104/livekit-server-sdk-php">PHP (community)</a> · <a href="https://github.com/pabloFuente/livekit-server-sdk-dotnet">.NET (community)</a></td></tr><tr></tr>
<tr><td>Resources</td><td><a href="https://docs.livekit.io">Docs</a> · <a href="https://docs.livekit.io/mcp">Docs MCP Server</a> · <a href="https://github.com/livekit/livekit-cli">CLI</a> · <a href="https://cloud.livekit.io">LiveKit Cloud</a></td></tr><tr></tr>
<tr><td>LiveKit Server OSS</td><td><a href="https://github.com/livekit/livekit">LiveKit server</a> · <a href="https://github.com/livekit/egress">Egress</a> · <a href="https://github.com/livekit/ingress">Ingress</a> · <a href="https://github.com/livekit/sip">SIP</a></td></tr><tr></tr>
<tr><td>Community</td><td><a href="https://community.livekit.io">Developer Community</a> · <a href="https://livekit.io/join-slack">Slack</a> · <a href="https://x.com/livekit">X</a> · <a href="https://www.youtube.com/@livekit_io">YouTube</a></td></tr>
</tbody>
</table>
<!--END_REPO_NAV-->
