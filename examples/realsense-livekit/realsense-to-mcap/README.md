# RealSense RGB-D → MCAP (Foxglove Protobuf, No ROS)

This project records RGB and depth frames from an **Intel RealSense D415** and writes them directly to an MCAP file using Foxglove Protobuf schemas — **without using ROS**.

The resulting `.mcap` file opens directly in Foxglove Studio with working Image panels.

---

## Overview

This example:

- Captures RGB (`rgb8`) and depth (`16UC1`) frames
- Serializes frames using `foxglove.Image` (Protobuf)
- Writes messages to MCAP with ZSTD compression
- Produces a file immediately viewable in Foxglove

Architecture:
RealSense D415
↓
librealsense2
↓
foxglove.Image (protobuf)
↓
MCAP writer
↓
realsense_rgbd.mcap


__NOTE: This has only been tested on Linux and MacOS.__

__NOTE: This has only been tested with a D415 camera.__

---

## Dependencies

This project uses:

- Intel RealSense D415
- librealsense2
- Protobuf
- MCAP C++ library
- Foxglove protobuf schemas
- Foxglove Studio

---

## Install Dependencies

### System packages

**Ubuntu / Debian**

```bash
sudo apt install librealsense2-dev protobuf-compiler libprotobuf-dev \
                 libzstd-dev liblz4-dev zlib1g-dev
```

**macOS (Homebrew)**

```bash
brew install librealsense protobuf zstd lz4 pkg-config
```

> zlib ships with macOS and does not need to be installed separately.

### Clone externals and generate protobuf files

A setup script handles cloning the MCAP C++ library and the Foxglove SDK
(for proto schemas), then runs `protoc` to generate C++ sources:

```bash
# From anywhere — the script locates paths relative to itself
../setup_realsense.sh
```

This populates `external/` (mcap, foxglove-sdk) and `generated/` (protobuf
C++ sources). Both directories are gitignored. The script is idempotent and
safe to re-run.

## Build

All executables are built into `build/bin/`.

1. Build the LiveKit C++ SDK (including the bridge) from the **client-sdk-cpp** repo root first (e.g. `./build.sh release`). This project links against that build.
2. From this directory:

```bash
mkdir build
cd build
cmake ..
make -j
```

When this project lives under `client-sdk-cpp/examples/realsense-livekit/realsense-to-mcap/`, the SDK build directory is auto-detected (`build-release` or `build` at repo root). Otherwise set it explicitly:

```bash
cmake -DLiveKitBuild_DIR=/path/to/client-sdk-cpp/build-release ..
```

### Executables (in `build/bin/`)

| Binary | Description |
|--------|-------------|
| `realsense_to_mcap` | Standalone recorder: captures RealSense and writes RGB + depth to an MCAP file. |
| `realsense_rgbd` | LiveKit publisher: captures RealSense, publishes RGB as video and depth as DataTrack (identity `realsense_rgbd`). |
| `rgbd_viewer` | LiveKit subscriber: subscribes to that video + data track and writes to MCAP (identity `rgbd_viewer`). |

Run the standalone recorder:

```bash
./bin/realsense_to_mcap
```

This produces `realsense_rgbd.mcap` in the current directory.

---

## LiveKit participants

To stream RGB+D over a LiveKit room and record on the viewer side:

1. Start **realsense_rgbd** (publisher), then **rgbd_viewer** (subscriber), using the same room URL and tokens with identities `realsense_rgbd` and `rgbd_viewer`.
2. Run from the build directory:

```bash
./bin/realsense_rgbd <ws-url> <token>
./bin/rgbd_viewer [output.mcap] <ws-url> <token>
```
