# Capture sources

> **NOTE:** LiveKit Capture is not officially supported on Intel-based Macs.

`CaptureSource` connects a Rust video producer to a regular C++ `VideoSource`.
The capture pump runs in Rust, so C++ does not send one FFI request per frame.
The SDK supports three source configurations:

| Configuration | Output | Runtime requirements |
| --- | --- | --- |
| `PatternVideoSourceConfig` | GPU-rendered gradient or LiveKit logo | Compatible GPU adapter |
| `ClockVideoSourceConfig` | GPU-rendered local time with millisecond precision | Compatible GPU adapter |
| `GstreamerVideoSourceConfig` | Pre-encoded video from a GStreamer pipeline | GStreamer runtime and the pipeline's plugins |

A custom SDK build can include all sources or a subset selected with
`--capture=pattern,clock` or `--capture=gstreamer`. Release `-capture` bundles
include all sources. `LIVEKIT_CAPTURE_ENABLED` indicates that at least one
source is enabled; creating a source omitted from the build reports
`CaptureSourceError` through the returned future. Pattern/clock-only builds do
not require GStreamer.

Native camera device capture and device enumeration are not part of this API.

## Build and release bundles

For build flags, release bundle naming, GStreamer prerequisites, and runtime
requirements, see [Building — Capture sources](building.md#capture-sources).

Capture-enabled bundles install the capture API in `include/livekit/capture/`:

```cpp
#include <livekit/capture/capture_source.h>
```

You can also include `<livekit/livekit.h>`, which includes capture declarations
when capture is enabled. Standard SDK bundles do not contain the `capture/`
header directory.

## Create and publish a source

Dimensions must be positive. Pattern and clock frame rates must also be positive.
Invalid configurations produce `CaptureSourceError` when the returned future is
awaited. For GStreamer, resolution and codec can be omitted to discover them from
negotiated pipeline caps.

Discarding a creation future does not wait for creation to finish. Creation
continues, and the source is released when the callback arrives.

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
source's codec, the `PreEncoded` encoder, and disabled simulcast, which override
conflicting application options. All other application options are preserved.
Pattern and clock sources preserve every application option, including explicit
codec and simulcast choices. Rust supplies defaults for fields left unset.
