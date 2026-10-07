# Built-in capture sources

`CaptureSource` connects a Rust video producer to a regular C++ `VideoSource`.
Pattern and clock sources render offscreen on the GPU. Their frames stay in
Rust until they enter the WebRTC video source, so C++ does not send one FFI
request per frame.

The pattern source offers `Pattern::Gradient` and `Pattern::Logo`. The clock
source displays the local time with millisecond precision.

Only the repository's `*-tests` and `*-all` build presets enable
`LIVEKIT_ENABLE_CAPTURE`. Other presets and custom CMake configurations must set
`-DLIVEKIT_ENABLE_CAPTURE=ON`; otherwise source creation fails. A GPU adapter must be available at runtime.

Create a source, publish its video source as a local track, and start capture
after publication:

```cpp
#include <livekit/livekit.h>

auto capture = livekit::CaptureSource::create(
    livekit::PatternVideoSourceConfig{{1280, 720}, 30, livekit::Pattern::Gradient}).get();
// For a local wall clock, use:
// livekit::CaptureSource::create(livekit::ClockVideoSourceConfig{{1280, 720}, 30}).get();

auto track = livekit::LocalVideoTrack::createLocalVideoTrack("capture", capture->videoSource());
auto participant = room.localParticipant().lock();
participant->publishTrack(track, capture->publishOptions());

capture->setOnFinishedCallback([](const livekit::CaptureResult& result) {
  // Runs on the FFI callback thread. Hand work to another thread if it blocks.
});
if (!capture->start()) {
  // Handle the start failure.
}
```

The example assumes `room` is connected and the SDK is initialized. Keep
`capture` alive while its track is published. Call `stop()` to finish capture;
the callback reports the exit reason and number of captured frames. Destroying
the source also stops capture. The clock displays the local time of the machine
running the Rust FFI.
