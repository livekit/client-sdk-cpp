# Capture sources

> **NOTE:** LiveKit Capture is not officially supported on Intel-based Macs.

`livekit::capture::CaptureSource` connects a Rust video producer to a regular C++ `VideoSource`.
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
a `std::runtime_error` through the returned future. Pattern/clock-only builds do
not require GStreamer.

Native camera device capture and device enumeration are not part of this API.

## Build and release bundles

For build flags, release bundle naming, GStreamer prerequisites, and runtime
requirements, see [Building — Capture sources](building.md#capture-sources).

Capture types live in the `livekit::capture` namespace. Capture-enabled bundles
install the API in `include/livekit/capture/`:

```cpp
#include <livekit/capture/capture_source.h>
```

Standard SDK bundles omit the `capture/` directory, and their other installed
headers do not name it.

## Create and publish a source

Dimensions must be positive. Pattern and clock frame rates must also be positive.
Invalid C++ configurations throw `std::invalid_argument` when the returned
future is awaited. For GStreamer, resolution and codec can be omitted to discover
them from negotiated pipeline caps. Producer setup failures reported by Rust
throw `std::runtime_error` through that future.

Discarding a creation future does not wait for creation to finish. Creation
continues, and the source is released when the callback arrives.

```cpp
#include <livekit/livekit.h>
#include <livekit/capture/capture_source.h>

// The SDK is initialized, and room is connected.
try {
  auto pending = livekit::capture::CaptureSource::create(
      livekit::capture::PatternVideoSourceConfig{{1280, 720}, 30, livekit::capture::Pattern::Gradient});
  // Other application setup can run while the producer is being constructed.
  auto capture = pending.get().value();
  // For a wall clock, use ClockVideoSourceConfig{{1280, 720}, 30} instead.

  auto track = livekit::LocalVideoTrack::createLocalVideoTrack("capture", capture->videoSource());
  auto participant = room.localParticipant().lock();
  if (!participant) {
    return;
  }
  participant->publishTrack(track, capture->publishOptions());
  capture->start().value(); // Rust rejection or a binding failure throws.

  // Later, when shutting down:
  capture->stop().value(); // Signals without waiting.
  auto result = capture->join().get(); // Waits for cleanup.
  if (!result) {
    // Handle result.error().kind and result.error().message: a typed pump failure.
    return;
  }
  auto stats = result.value(); // frames_captured and exit.
} catch (const std::invalid_argument& error) {
  // Handle an invalid C++ capture configuration using error.what().
} catch (const std::runtime_error& error) {
  // Handle setup, control, transport, protocol, or shutdown failure using error.what().
}
```

Keep `capture` alive while its track is published. `stop()` signals the pump to
finish without waiting. `join()` consumes the running pump, waits for its
thread to exit, and reports the exit reason and frame count. For unbounded
sources, call `stop()` before `join()`. Destroying the source also stops
capture.

## GStreamer encoded pipelines

Provide `appsink name=lk_appsink`, or leave exactly one encoded video source pad
unlinked so the source can attach its own sink:

```cpp
livekit::capture::GstreamerVideoSourceConfig config;
config.pipeline = "videotestsrc ! video/x-raw,width=1280,height=720,framerate=30/1 ! "
                  "vp8enc name=lk_encoder deadline=1 ! appsink name=lk_appsink";
config.rate_control = livekit::capture::GstreamerRateControl{
    "lk_encoder", "target-bitrate", livekit::capture::GstreamerBitrateUnit::Bps};
// Use the exception handling shown above for configuration and setup failures.
auto capture = livekit::capture::CaptureSource::create(config).get().value();
```

The optional rate-control binding forwards WebRTC bitrate targets to the named
encoder property. Without it, the pipeline uses its configured fixed bitrate.
Use `capture->publishOptions()` when publishing: encoded sources require the
source's codec, the `PreEncoded` encoder, and disabled simulcast, which override
conflicting application options. All other application options are preserved.
Pattern and clock sources preserve every application option, including explicit
codec and simulcast choices. Rust supplies defaults for fields left unset.

## Error contract

`CaptureErrorKind` matches the protobuf enum exactly:

| C++ value | Protobuf value | Meaning |
| --- | --- | --- |
| `Source = 0` | `CAPTURE_ERROR_KIND_SOURCE = 0` | The running capture source failed or violated its contract. |
| `Rtc = 1` | `CAPTURE_ERROR_KIND_RTC = 1` | The RTC source rejected a frame. |
| `Panicked = 2` | `CAPTURE_ERROR_KIND_PANICKED = 2` | The pump thread panicked. |

A structured pump error received from Rust becomes a failed
`Result<CaptureResult, CaptureError>` from `join()`. Its message and kind are
preserved. Check the result before calling `value()` or `error()`.

Failures without a protobuf error kind throw a clear exception with the operation
name and diagnostic. C++ configuration validation throws `std::invalid_argument`.
Uninitialized SDK use, transport failures, malformed responses, shutdown
cancellation, and untyped errors from Rust throw `std::runtime_error`.
`create()` and `join()` deliver these exceptions from the returned future's
`get()`; `start()` and `stop()` throw directly. Exceptions are captured at the
FFI callback boundary. They never unwind through Rust callbacks.

| Operation | Return type | Failure handling |
| --- | --- | --- |
| `create(config)` | `std::future<Result<std::shared_ptr<CaptureSource>, CaptureError>>` | Configuration and untyped setup errors throw from `get()`. |
| `start()` / `stop()` | `Result<void, CaptureError>` | Current control responses carry untyped strings, so rejection throws directly. |
| `join()` | `std::future<Result<CaptureResult, CaptureError>>` | Structured pump errors use `error()`; binding failures throw from `get()`. |

`CaptureResult` contains successful statistics only. Creation and control return
successful results when their requests succeed; the current protocol supplies
structured error kinds only for the terminal pump result. The SDK does not
classify untyped Rust messages by parsing their text. Allocation failures may
also throw.

This replaces the unreleased C++-only error categories. Code that handled
`InvalidConfiguration`, `NotInitialized`, `Rejected`, `Transport`, `Protocol`, or
`Cancelled` as enum values must now catch the corresponding exception. Existing
`Source`, `Rtc`, and `Panicked` pump-error handling remains unchanged.
