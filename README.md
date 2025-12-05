# LiveKit C++ Client SDK

This SDK enables native C++ applications to connect to LiveKit servers for real-time audio/video communication.

---

## 📦 Requirements
- **CMake** ≥ 4.0  
- **Rust / Cargo** (latest stable toolchain)  
- **Protobuf** compiler (`protoc`)  
- **macOS** users: System frameworks (CoreAudio, AudioToolbox, etc.) are automatically linked via CMake.
- **Git LFS** (required for examples)
  Some example data files (e.g., audio assets) are stored using Git LFS.
  You must install Git LFS before cloning or pulling the repo if you want to run the examples.


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

All build actions are managed by the provided build.sh script.
```bash
./build.sh clean        # Clean CMake build artifacts
./build.sh clean-all    # Deep clean (C++ + Rust + generated files)
./build.sh debug        # Build Debug version
./build.sh release      # Build Release version
./build.sh verbose      # Verbose build output
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

Press Ctrl-C to exit the example.

### SimpleRpc
The SimpleRpc example demonstrates how to:
- Connect multiple participants to the same LiveKit room
- Register RPC handlers (e.g., arrival, square-root, divide, long-calculation)
- Send RPC requests from one participant to another
- Handle success, application errors, unsupported methods, and timeouts
- Observe round-trip times (RTT) for each RPC call

#### 🔑 Generate Tokens
Before running any participant, create JWT tokens with "caller", "greeter" and "math-genius" identities and room name.
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