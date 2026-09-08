# Memory lifecycle tester

This standalone application repeatedly exercises the public C++ SDK lifecycle
to expose retained lower-level Rust/WebRTC resources.

By default, the application calls `livekit::initialize()` once, runs every
selected workload for the configured number of iterations, and calls
`livekit::shutdown()` once. This models a long-running process that repeatedly
joins and leaves rooms without relying on global shutdown to release each
room's resources. Pass `--ffi-cycles` to initialize and shut down the SDK on
every iteration instead.

Workloads can be combined:

- `--sources`: creates an `AudioSource`/`LocalAudioTrack` and a 1280x720
  `VideoSource`/`LocalVideoTrack`;
- `--connect`: joins and leaves a LiveKit room;
- `--media`: publishes audio and video tracks, captures three frames from each,
  and unpublishes them; implies `--connect`;
- `--data-track`: publishes and unpublishes a `LocalDataTrack`; implies
  `--connect`;
- `--data-frames`: immediately sends 10 1 KiB payloads; implies
  `--data-track` and `--connect`.

The `--sources` video source intentionally receives no captured frame. This
exercises teardown of the Rust keepalive task that runs until the first raw
video frame arrives and previously retained roughly one 720p frame per
lifecycle. The separate `--media` source takes the normal first-capture path.

The executable is built with the normal test targets but is not registered with
CTest, so it only runs when invoked manually.

## Build

```bash
./build.sh release-tests
```

## Run

Start a local LiveKit server and load the test credentials:

```bash
source scripts/set-test-tokens.sh
```

`--connect`, `--media`, `--data-track`, and `--data-frames` require
`LIVEKIT_URL` and `LIVEKIT_TOKEN_A`. The default is 1,000 iterations. With no
workload flags, the tester runs every workload in one SDK lifecycle:

```bash
./build-release/bin/livekit_memory_lifecycle_tester
```

Use `--iterations N` (or the legacy single numeric argument) to set the loop
count. Examples:

```bash
# Offline audio/video-source lifecycle only.
./build-release/bin/livekit_memory_lifecycle_tester --iterations 1000 --sources

# Room setup and teardown only.
./build-release/bin/livekit_memory_lifecycle_tester --iterations 1000 --connect

# Published audio/video capture and teardown.
./build-release/bin/livekit_memory_lifecycle_tester --iterations 1000 --media

# Incrementally add data-track allocation and data-frame delivery.
./build-release/bin/livekit_memory_lifecycle_tester --iterations 1000 --data-track
./build-release/bin/livekit_memory_lifecycle_tester --iterations 1000 --data-frames

# Combine explicitly selected workloads.
./build-release/bin/livekit_memory_lifecycle_tester --iterations 100 --sources --data-frames

# Exercise full FFI initialization and disposal on every iteration.
./build-release/bin/livekit_memory_lifecycle_tester --iterations 100 --ffi-cycles
```

To compare memory behavior before and after a lifecycle fix, use identical
iteration counts and build configurations:

```bash
python3 scripts/track_process_memory.py --interval 0.01 -- \
  ./build-release/bin/livekit_memory_lifecycle_tester 100
```

Allocator caching means final RSS need not return to the initial value. The
useful regression signal is sustained or iteration-proportional growth.
