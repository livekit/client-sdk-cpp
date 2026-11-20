# Data track dynacast in the C++ SDK

A data track carries a stream of application data. A subscriber is a participant who requests that stream. See the [LiveKit data track guide](https://docs.livekit.io/transport/data/data-tracks/).

Dynacast pauses sending when the server reports zero subscribers. The track stays published, so participants can still find it and subscribe. Rust rejects new frames while sending is paused.

## Match the upstream behavior

The C++ SDK currently pins Rust commit `2811b4be`. The local Rust branch `ladvoc/data-track-dynacast` adds `NO_SUBSCRIBERS` to the protocol in `b4ef0127`. The corresponding server branch sends a Boolean that reports whether a track has any subscribers. Rust accepts pushes until the server reports that the track has zero subscribers. A server that sends no updates keeps the existing behavior.

Use a released Rust revision that includes the demand update and the new error code. The demand update is the server message about subscribers. Use a compatible LiveKit Server release for the end-to-end test. The current test server pin, `v1.13.3`, lacks the demand update in the local server source. Record both release versions before enabling the new test in CI.

## Change the C++ SDK

Add `LocalDataTrackTryPushErrorCode::NO_SUBSCRIBERS = 5` in `include/livekit/data_track_error.h`. Keep the existing numeric values. Map the new protocol code in `src/data_track_error.cpp` and preserve the message from Rust. Let the SDK build generate the C++ protocol code from the Rust revision.

Keep the subscriber state and the decision to accept frames in Rust. Keep `RoomOptions::dynacast` for video. Document the new error on all three `LocalDataTrack::tryPush()` overloads. Explain that callers can drop that frame and try again with a fresh frame. Preserve the success result for an empty payload on a valid handle.

## Test the behavior

### Unit test

A unit test exercises one function without a server. Add one case to `src/tests/unit/test_data_track_error.cpp`. Pass the new protocol error to `LocalDataTrackTryPushError::fromProto()`. Make sure that it returns `NO_SUBSCRIBERS` and preserves the message. Keep the existing test for the `UNKNOWN` fallback.

### Integration test

An integration test exercises the C++ and Rust boundary. Extend `UnpublishUpdatesPublishedStateEndToEnd` in `src/tests/integration/test_data_track.cpp`. After unpublish, make sure that a nonempty `tryPush()` returns `TRACK_UNPUBLISHED`. Make sure that an empty `tryPush()` still succeeds on the same handle. Retain the published-state assertions and remove the fixed delay before unpublish.

### End-to-end test

An end-to-end test exercises two SDK clients through a live server. Extend `CanResubscribeToRemoteDataTrack` in the same file. Use one publisher and one subscriber. After publication, wait for `NO_SUBSCRIBERS` and make sure that the track stays published. Subscribe and make sure that the receiver gets a fresh frame with the expected data. Close the subscription and wait for `NO_SUBSCRIBERS` again. Subscribe again and make sure that the receiver gets a frame with a different marker.

Use the existing fixture and a bounded wait for each state change. During subscription changes, allow `NO_SUBSCRIBERS` while waiting for the next frame. Fail on any other push error. Use one thread to call `tryPush()` and close streams on timeout to release blocked readers. Run this case in the existing `e2e-testing` jobs with the compatible server pin.

## Update callers and measure cost

Keep `PublishManyTracks` focused on publication and unpublication. Use an active subscriber in the memory test to preserve its coverage of frame enqueueing. In delivery tests, retry with fresh frames if `NO_SUBSCRIBERS` occurs before subscription takes effect. Keep other push failures visible.

Use `benchmarks/data_track_throughput` to compare the current and updated SDK with one active subscriber. Measure accepted frames per second and the time from send to receipt under the same load. Use the same payload size and push rate with zero subscribers. Count `NO_SUBSCRIBERS` separately from queue-full errors and record the time per push. Disable per-frame error logging for the expected no-subscriber result during the measurement.

## Verify before merging

Initialize the shared tools with `git submodule update --init cpp-tools`. If the root `.clang-format` and `.clang-tidy` links are missing, run `./cpp-tools/install.sh`. Initialize the Rust submodule and its dependencies at the selected revision. Build with `./build.sh debug-tests` or `build.cmd debug-tests` on Windows. Run `source scripts/set-test-tokens.sh` before the test commands. Run the targeted tests and the existing supported-platform test matrix.

Run `./scripts/generate-docs.sh` to build the API reference. Run the formatter and static analysis commands in `docs/tools.md`.

With the updated SDK, run the existing data track delivery test once against `v1.13.3`. Use this run to make sure that delivery still works on a server that sends no demand updates. Record the server version, test results, and benchmark results in the pull request.

## Local verification results

On macOS, the pinned Rust build passed 364 unit tests and skipped the new protocol mapping test. LiveKit Server `v1.13.3` passed all 27 data track integration tests in that build. The preview Rust build passed all 365 unit tests and three compatibility tests against `v1.13.3`. The preview pair passed 28 data track integration tests, including the dynacast test. The dynacast test also passed five consecutive runs. The memory tester passed a syntax build, and its runtime test still needs Linux.

The preview used Rust `84a7d969` and LiveKit Server `50304fd0`. Both upstream changes still need releases before the default build and CI can enable dynacast. [Rust change](https://github.com/livekit/rust-sdks/pull/1696) · [Server change](https://github.com/livekit/livekit/pull/4656)

The benchmark sent 300 frames of 1 KiB at 100 frames per second on the preview server. With one subscriber, both builds accepted about 100 frames per second. The pinned build delivered 300 frames, and the preview delivered 299 frames. Median delivery time was 1.82 ms for the pinned build and 2.04 ms for the preview. The 95th percentile was 2.89 ms for the pinned build and 3.23 ms for the preview.

With zero subscribers, the pinned build accepted all 300 pushes and averaged 133 microseconds per push. The preview returned `NO_SUBSCRIBERS` for all 300 pushes and averaged 119 microseconds per push. Neither run returned `QUEUE_FULL`.
