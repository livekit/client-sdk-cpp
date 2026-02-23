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

### Install librealsense

```bash
sudo apt install librealsense2-dev
```

### Install MCAP C++ Lib
```bash
git clone https://github.com/foxglove/mcap.git
cd mcap/cpp
cmake -B build
cmake --build build
sudo cmake --install build
```

### Clone Foxglove SDK for Schemas
git clone https://github.com/foxglove/foxglove-sdk.git

We will use:

foxglove-sdk/schemas/proto/foxglove/Image.proto
foxglove-sdk/schemas/proto/foxglove/Time.proto
### 2. Generate C++ Protobuf Files

```bash
mkdir generated

protoc \
  --cpp_out=generated \
  -I foxglove-sdk/schemas/proto \
  foxglove-sdk/schemas/proto/foxglove/*.proto
```

This generates:

- generated/foxglove/Image.pb.cc
- generated/foxglove/Image.pb.h
- generated/foxglove/Time.pb.cc
- generated/foxglove/Time.pb.h


## Build

```bash
mkdir build
cd build
cmake ..
make -j
```

Run:

```bash
./realsense_mcap
```

This produces `realsense_rgbd.mcap`.

---

## LiveKit participants (optional)

This project also includes two LiveKit participants that stream RGB+D over a room:

- **realsense_rgbd** — Captures RealSense, publishes RGB as a video track and depth as a DataTrack (RawImage proto). Identity: `realsense_rgbd`.
- **rgbd_viewer** — Subscribes to that video and data track and writes both to an MCAP file. Identity: `rgbd_viewer`.

They are **not** built by the steps above. To build them:

1. Build the LiveKit SDK (including the bridge) from the **repo root** (e.g. using the main SDK build script). That produces a build directory with `lib/liblivekit_bridge.so` and `lib/liblivekit.so`.
2. In this directory, configure with the same `mkdir build && cd build`, then point CMake at that SDK build and enable the participants:

```bash
cd ~/workspaces/client-sdk-cpp/bridge/realsense-livekit/realsense-to-mcap/build
cmake -DBUILD_LIVEKIT_PARTICIPANTS=ON -DLiveKitBuild_DIR=$HOME/workspaces/sderosa-client-sdk-cpp/build-release ..
make -j
```

Then run e.g.:

- `./realsense_rgbd <ws-url> <token>` (token identity `realsense_rgbd`)
- `./rgbd_viewer [output.mcap] <ws-url> <token>` (token identity `rgbd_viewer`)

Use the same room and URL; start `realsense_rgbd` first, then `rgbd_viewer` to record to MCAP.
