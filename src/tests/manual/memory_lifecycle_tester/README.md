# Memory lifecycle tester

This standalone application repeatedly exercises the public C++ SDK lifecycle
to expose retained lower-level Rust/WebRTC resources.

Each iteration:

- calls `livekit::initialize()`;
- creates an `AudioSource` and `LocalAudioTrack`;
- creates a 1280x720 `VideoSource` and `LocalVideoTrack`;
- creates a representative `DataTrackFrame`;
- destroys those objects before calling `livekit::shutdown()`.

The video source intentionally receives no captured frame. This exercises
teardown of the Rust keepalive task that runs until the first raw video frame
arrives and previously retained roughly one 720p frame per lifecycle.

`LocalDataTrack` itself cannot be created offline: its public factory publishes
through a connected `LocalParticipant`. The `DataTrackFrame` allocation covers
the offline data API surface but does not create a Rust data-track handle.

The executable is built with the normal test targets but is not registered with
CTest, so it only runs when invoked manually. It does not connect to a server
and needs no LiveKit credentials.

## Build

```bash
./build.sh release-tests
```

## Run

The default is 1,000 iterations. An alternate iteration count may be supplied
as the only argument:

```bash
./build-release/bin/livekit_memory_lifecycle_tester
./build-release/bin/livekit_memory_lifecycle_tester 100
```

To compare memory behavior before and after a lifecycle fix:

```bash
python3 scripts/track_process_memory.py --interval 0.01 -- \
  ./build-release/bin/livekit_memory_lifecycle_tester
```

Use identical iteration counts and build configurations when comparing results.
Allocator caching means final RSS need not return to the initial value; the
useful regression signal is sustained or iteration-proportional growth.
