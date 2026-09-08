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
  `--data-track` and `--connect`;
- `--receive`: connects a second participant, publishes modest synthetic
  audio/video (640x360, default software codec), and waits until the subscriber
  receives a few frames via `Room` frame callbacks. Implies `--connect` and
  requires `LIVEKIT_TOKEN_B`. This is opt-in and is **not** part of the default
  all-workloads run.

The `--sources` video source intentionally receives no captured frame. This
exercises teardown of the Rust keepalive task that runs until the first raw
video frame arrives and previously retained roughly one 720p frame per
lifecycle. The separate `--media` source takes the normal first-capture path.
`--receive` uses a smaller I420 source so CI can cover subscribe/decode and
`SubscriptionThreadDispatcher` teardown without NVIDIA hardware.

This tester stays on the software media path. It does not set
`LIVEKIT_PREFERRED_HW_ENCODER` and does not exercise `PlatformAudio`. CUDA
encode/decode shakeouts belong in `src/tests/manual/cuda_video_lifecycle_tester`.

The executable is built with the normal test targets but is not registered with
CTest. CI invokes it as a short smoke test; longer runs are manual.

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
`LIVEKIT_URL` and `LIVEKIT_TOKEN_A`. `--receive` also requires
`LIVEKIT_TOKEN_B`. The default is 1,000 iterations. With no workload flags, the
tester runs every workload except `--receive` in one SDK lifecycle:

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

# Software subscribe/decode path with a second participant.
./build-release/bin/livekit_memory_lifecycle_tester --iterations 100 --receive

# Combine explicitly selected workloads.
./build-release/bin/livekit_memory_lifecycle_tester --iterations 100 --sources --data-frames

# Exercise full FFI initialization and disposal on every iteration.
./build-release/bin/livekit_memory_lifecycle_tester --iterations 100 --ffi-cycles
```

The tester prints RSS at progress intervals and `RSS final` at the end. Set
`LIVEKIT_MEMORY_MAX_FINAL_RSS_KIB` to fail if that final sample exceeds the
limit. Leave it unset for local and hardware-lab runs; CI sets it per OS.

```bash
LIVEKIT_MEMORY_MAX_FINAL_RSS_KIB=1048576 \
  ./build-release/bin/livekit_memory_lifecycle_tester --iterations 20 --sources --media --data-frames --receive
```

To compare memory behavior before and after a lifecycle fix, use identical
iteration counts and build configurations:

```bash
python3 scripts/track_process_memory.py --interval 0.01 -- \
  ./build-release/bin/livekit_memory_lifecycle_tester 100
```

The helper accepts `--max-final-rss-kib N` and honors `LIVEKIT_MEMORY_MAX_FINAL_RSS_KIB`
when the flag is omitted.

Allocator caching means final RSS need not return to the initial value. The
useful regression signal is sustained or iteration-proportional growth. The
optional RSS cap is a smoke ceiling, not a tight leak bound.

## CI smoke

PR CI runs two short invocations after the integration tests (release build,
software codecs only):

```bash
./build-release/bin/livekit_memory_lifecycle_tester --iterations 30 --ffi-cycles --sources
./build-release/bin/livekit_memory_lifecycle_tester --iterations 20 --sources --media --data-frames --receive
```

Nightly uses the same commands with 50 iterations each on a debug build. Neither
job is a hardware shakeout.
