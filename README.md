# LiveKit C++ Client SDK

This SDK enables native C++ applications to connect to LiveKit servers for real-time audio/video communication.

---

## 📦 Requirements
- **CMake** ≥ 3.20  
- **Rust / Cargo** (latest stable toolchain)  
- **vcpkg** (for dependency management)
- **Git LFS** (required for examples)
  Some example data files (e.g., audio assets) are stored using Git LFS.
  You must install Git LFS before cloning or pulling the repo if you want to run the examples.

**Platform-Specific Requirements:**
- **Windows:** Visual Studio 2019 or later
- **macOS:** System frameworks (CoreAudio, AudioToolbox, etc.) are automatically linked
- **Linux:** See [README_BUILD.md](README_BUILD.md) for system dependencies

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
./build.sh clean        # Clean CMake build artifacts
./build.sh clean-all    # Deep clean (C++ + Rust + generated files)
./build.sh debug        # Build Debug version
./build.sh release      # Build Release version
./build.sh verbose      # Verbose build output
```

**Windows:**
```powershell
.\build.bat clean       # Clean CMake build artifacts
.\build.bat clean-all   # Deep clean (C++ + Rust + generated files)
.\build.bat debug       # Build Debug version
.\build.bat release     # Build Release version
.\build.bat verbose     # Verbose build output
```

### Advanced Build (Using CMake Presets)

For more control and platform-specific builds, see the detailed instructions in [README_BUILD.md](README_BUILD.md).

**Prerequisites:**
- Set `VCPKG_ROOT` environment variable pointing to your vcpkg installation

```bash
# Linux/macOS
export VCPKG_ROOT=/path/to/vcpkg

# Windows PowerShell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
```

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