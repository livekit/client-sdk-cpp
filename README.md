# LiveKit C++ Client SDK

A C++ wrapper around the [LiveKit Rust Client SDK](https://github.com/livekit/client-sdk-rust), built using **CMake** and integrated with the Rust FFI layer.  
This SDK enables native C++ applications to connect to LiveKit servers for real-time audio/video communication.

---

## 📦 Requirements
- **CMake** ≥ 4.0  
- **Rust / Cargo** (latest stable toolchain)  
- **Protobuf** compiler (`protoc`)  
- **macOS** users: System frameworks (CoreAudio, AudioToolbox, etc.) are automatically linked via CMake.


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

```bash
./build/examples/SimpleRoom --url ws://localhost:7880 --token <jwt-token>
```

You can also provide the URL and token via environment variables:
```bash
export LIVEKIT_URL=ws://localhost:7880
export LIVEKIT_TOKEN=<jwt-token>
./build/examples/SimpleRoom
```

Press Ctrl-C to exit the example.


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