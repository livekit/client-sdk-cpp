# Capture sources

> **NOTE:** LiveKit Capture is not officially supported on Intel-based Macs.

The capture APIs connect Rust media producers to regular C++ track
types. Media frames stay in Rust, so C++ does not send one FFI request per
frame. The SDK supports video capture and GStreamer PCM audio capture.

| Configuration | Output | Runtime requirements |
| --- | --- | --- |
| `PatternVideoSourceConfig` | GPU-rendered gradient or LiveKit logo | Compatible GPU adapter |
| `ClockVideoSourceConfig` | GPU-rendered local time with millisecond precision | Compatible GPU adapter |
| `GstreamerVideoSourceConfig` | Pre-encoded video from a GStreamer pipeline | GStreamer runtime and the pipeline's plugins |
| `GstreamerAudioSourceConfig` | PCM S16LE audio into an `AudioSource` | GStreamer runtime and the pipeline's plugins |

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

| Header | Contents |
| --- | --- |
| `capture_source.h` | Umbrella for all capture APIs; existing callers can keep this include |
| `audio_source.h` | Audio capture and its configurations |
| `video_source.h` | Video capture and its configurations |
| `capture_types.h` | Shared capture results, errors, and exit reasons |

Include the umbrella for the full API, or the audio and video headers directly:

```cpp
#include <livekit/capture/video_source.h>
#include <livekit/capture/audio_source.h>
```

Standard SDK bundles omit the `capture/` directory, and their other installed
headers do not name it.

## Create and publish a source

Rust validates capture configurations, including zero dimensions and frame rates,
and reports failures as `std::runtime_error` when the returned future is awaited.
Resolution dimensions use `std::uint32_t`, matching the protobuf and Rust types.
The C++ wrapper rejects invalid enum values that cannot be serialized, using
`std::invalid_argument`. For GStreamer, resolution and codec can be omitted to
discover them from negotiated pipeline caps.
Producer setup failures reported by Rust also throw `std::runtime_error` through
that future.

Discarding a creation future does not wait for creation to finish. Creation
continues, and the source is released when the callback arrives.

```cpp
#include <livekit/livekit.h>
#include <livekit/capture/video_source.h>

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

## GStreamer audio capture

`GstreamerAudioSourceConfig` follows the naming of `GstreamerVideoSourceConfig`.
Both source APIs create asynchronously and use
the same `start()`, `stop()`, and `join()` lifecycle, `CaptureResult`, and
`CaptureError` types.

`GstreamerAudioCaptureSource` asynchronously creates a fixed-format PCM S16LE
pipeline and owns the `AudioSource` fed by its Rust capture pump. Publish that
source through a normal `LocalAudioTrack`, then use the same start, stop, and
join lifecycle as video capture:

```cpp
livekit::capture::GstreamerAudioSourceConfig config;
config.pipeline = "audiotestsrc ! audioconvert ! "
                  "audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=1 ! "
                  "appsink name=lk_audio_appsink";
config.sample_rate = 48000;
config.num_channels = 1;

auto capture = livekit::capture::GstreamerAudioCaptureSource::create(config).get().value();
auto track = livekit::LocalAudioTrack::createLocalAudioTrack("gstreamer", capture->audioSource());
participant->publishTrack(track);
capture->start().value();

capture->stop().value();
auto result = capture->join().get().value();
// result contains frames_captured (one frame = 10 ms of audio) and exit.
```

The negotiated `format()` reports sample rate, channel count, and samples per
channel. Rust validates that pipeline caps match the requested fixed format.
Unlike video resolution and codec discovery, audio requires an explicit fixed
format (48 kHz and mono by default). Audio frames are always 10 ms. The pipeline
must contain the named appsink. Audio capture does not expose video's `publishOptions()` helper;
publish it with normal audio track options.

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
name and diagnostic. Invalid enum values that cannot be serialized throw
`std::invalid_argument`. Rust configuration validation errors, uninitialized SDK
use, transport failures, malformed responses, shutdown cancellation, and untyped
errors from Rust throw `std::runtime_error`.
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
