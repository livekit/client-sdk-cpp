# CUDA video lifecycle tester

This standalone application repeatedly creates two C++ SDK rooms, publishes a
synthetic I420 video track from one, verifies that the other receives a frame,
and tears down both rooms and the SDK. It is intended to exercise the CUDA
video codec lifecycle on an NVIDIA system.

It requires a CUDA-enabled SDK build, an NVIDIA driver with NVENC and NVDEC
support, a running LiveKit server, and two tokens for the same room. The
existing `memory_lifecycle_tester` remains the hardware-neutral lifecycle
test; this tester adds real video publication and reception.

## Build

```bash
./build.sh release-tests
```

The Rust build must not print the following message:

```text
cuda.h not found; building without hardware accelerated video codec support for NVidia GPUs
```

## Run

Start a local development server in one terminal:

```bash
livekit-server --dev
```

In another terminal, load the test credentials and prefer the NVIDIA encoder:

```bash
source scripts/set-test-tokens.sh
export LIVEKIT_PREFERRED_HW_ENCODER=nvenc
export RUST_LOG=libwebrtc=debug

./build-release/bin/livekit_cuda_video_lifecycle_tester --iterations 100
```

Confirm that stderr includes messages such as `Using NVIDIA HW encoder (NVENC)
for H264` and `Using NVIDIA HW decoder (NVDEC) for H264`. The native WebRTC
logs are forwarded to Rust with the `libwebrtc` target and Debug severity, so
`RUST_LOG=libwebrtc=debug` must be set before starting the tester.
The tester explicitly requests H264 because NVENC does not support the default
VP8 codec. `LIVEKIT_PREFERRED_HW_ENCODER=nvenc` selects the NVIDIA backend for
that codec, but it does not turn an unavailable NVENC backend into a hard
failure, so those log messages are the current proof that the negotiated video
path used NVIDIA hardware.

For a memory regression run:

```bash
python3 scripts/track_process_memory.py --interval 0.01 -- \
  ./build-release/bin/livekit_cuda_video_lifecycle_tester --iterations 1000
```

Monitor NVIDIA memory concurrently:

```bash
watch -n 1 nvidia-smi
```

Allocator caching means final RSS need not return to the initial value. Look
for sustained or iteration-proportional host RSS or GPU-memory growth.
