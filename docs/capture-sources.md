# Capture sources

`CaptureSource` connects a Rust video producer to a regular C++ `VideoSource`.
The capture pump runs in Rust, so C++ does not send one FFI request per frame.
The SDK supports three source configurations:

| Configuration | Output | Runtime requirements |
| --- | --- | --- |
| `PatternVideoSourceConfig` | GPU-rendered gradient or LiveKit logo | Compatible GPU adapter |
| `ClockVideoSourceConfig` | GPU-rendered local time with millisecond precision | Compatible GPU adapter |
| `GstreamerVideoSourceConfig` | Pre-encoded video from a GStreamer pipeline | GStreamer runtime and the pipeline's plugins |

Native camera device capture and device enumeration are not part of this API.

## Build and release bundles

Only the repository's `*-tests` and `*-all` presets enable capture by default.
The `debug`, `release`, and `*-examples` presets disable it. Use `--capture`
with any build command to enable all three sources, or `--no-capture` to disable
them explicitly:

```bash
./build.sh release --capture --bundle --prefix sdk-out/livekit-sdk-capture
./build.sh debug-tests --no-capture
```

On Windows, use `build.cmd release --capture --bundle --prefix sdk-out\livekit-sdk-capture`.
Custom CMake configurations can set `LIVEKIT_BUILD_CAPTURE=ON`. The build enables
`capture-gstreamer`, `capture-pattern`, and `capture-clock` in the Rust FFI.

Release downloads with `-capture` in their names include these features, for
example `livekit-sdk-macos-arm64-capture-<version>.tar.gz`. Standard release
bundles have capture disabled. The generated numeric `LIVEKIT_CAPTURE_ENABLED` macro (0 or 1)
and `share/livekit/build-info.json` describe the selected bundle's build features;
they do not test whether a GPU or a requested GStreamer plugin is available.

Building a capture SDK requires GStreamer development libraries accessible to
`pkg-config`. On macOS, install `gstreamer` with Homebrew. On Ubuntu, install
`libgstreamer1.0-dev` and `libgstreamer-plugins-base1.0-dev`, plus the runtime
plugins your pipeline uses. On Windows, install the MSVC runtime and development
packages from the [GStreamer downloads page](https://gstreamer.freedesktop.org/download/)
and put the installation's `bin` directory on `PATH` and `lib/pkgconfig` directory
on `PKG_CONFIG_PATH`.

Capture bundles require GStreamer at runtime, including when using pattern or
clock sources. The SDK does not redistribute GStreamer libraries or plugins.

## Create and publish a source

Dimensions must be positive. Pattern and clock frame rates must also be positive.
Invalid configurations produce `CaptureSourceError` when the returned future is
awaited. For GStreamer, resolution and codec can be omitted to discover them from
negotiated pipeline caps.

```cpp
#include <livekit/livekit.h>

// The SDK is initialized, and room is connected.
auto pending = livekit::CaptureSource::create(
    livekit::PatternVideoSourceConfig{{1280, 720}, 30, livekit::Pattern::Gradient});
// Other application setup can run while the producer is being constructed.
auto capture = pending.get();
// For a wall clock, use ClockVideoSourceConfig{{1280, 720}, 30} instead.

auto track = livekit::LocalVideoTrack::createLocalVideoTrack("capture", capture->videoSource());
auto participant = room.localParticipant().lock();
if (!participant) {
  return;
}
participant->publishTrack(track, capture->publishOptions());

capture->setOnFinishedCallback([](const livekit::CaptureResult& result) {
  // Runs on the FFI callback thread. Offload work that would block.
});
if (!capture->start()) {
  // Handle the start failure.
}
```

Keep `capture` alive while its track is published. `stop()` signals the pump to
finish. The callback reports the exit reason and frame count. Destroying the
source also stops capture.

## GStreamer encoded pipelines

Provide `appsink name=lk_appsink`, or leave exactly one encoded video source pad
unlinked so the source can attach its own sink:

```cpp
livekit::GstreamerVideoSourceConfig config;
config.pipeline = "videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1 ! "
                  "vp8enc name=lk_encoder deadline=1 ! appsink name=lk_appsink";
config.rate_control = livekit::GstreamerRateControl{
    "lk_encoder", "target-bitrate", livekit::GstreamerBitrateUnit::Bps};
auto capture = livekit::CaptureSource::create(config).get();
```

The optional rate-control binding forwards WebRTC bitrate targets to the named
encoder property. Without it, the pipeline uses its configured fixed bitrate.
Use `capture->publishOptions()` when publishing: encoded sources require the
source's codec and encoder settings, which override conflicting application
options.
