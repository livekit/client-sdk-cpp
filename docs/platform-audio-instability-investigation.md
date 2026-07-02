# macOS PlatformAudio Instability — Investigation & Fix Plan

*Author: review pass on 2026-07-01. Supersedes the ad-hoc notes in the prior Cursor sessions.*

## 1. Summary

The macOS `PlatformAudioIntegrationTest` suite has been crashing (SIGSEGV / SIGABRT)
under repeated run/teardown. Prior investigation (three Cursor sessions on
`feature/additional_ci_improvements` and `feature/platform-audio-stability`, plus a
`rust-sdks` branch of the same name) converged on a real root cause and landed a fix
bundle. This document records an independent review of that work, what I agree and
disagree with, the CI evidence, and a corrected fix plan.

**Bottom line:**

- The final root-cause theory is **correct**: the macOS CoreAudio capture worker
  (`webrtc::AudioDeviceMac::CaptureWorkerThread`) delivers recorded audio into
  `webrtc::AudioTransportImpl` while the peer-connection transports / factory that own
  that transport are being destroyed. The landed `AdmProxy::ShutdownAudioIO` fix is the
  right shape and empirically removed the segfault.
- But the investigation stopped short in three ways this review corrects:
  1. **A 2026-07-01 rebase silently reset the `client-sdk-cpp` submodule pointer to a
     `rust-sdks` commit that does not contain the fix.**
  2. **The post-fix nightlies expose a *new, deterministic* failure** in
     `PlatformAudioFramesReachRemote` that is a signaling/negotiation regression, not an
     audio-teardown crash.
  3. **A structural teardown race survives the fix** and is the most likely cause of both
     the residual "crash at the very first PlatformAudio test" symptom and the
     `DataTrackE2ETest.PublishManyTracks` teardown hang.

## 2. Confirmed root cause (crash)

Evidence (from CI `.ips` reports, LLDB captures, and the pre-fix Intel triage run):

```
webrtc::AudioDeviceMac::CaptureWorkerThread()            audio_device_mac.cc:2498
  -> AudioConverterFillComplexBuffer
     -> CrashIfClientProvidedBogusAudioBufferList         (Apple AudioToolbox)
```
concurrent with, on the teardown side:
```
webrtc::AudioTransportImpl::SendProcessedData(...)
  -> ...RecordedDataIsAvailable -> DeliverRecordedData
  ~JsepTransportController() / ~BundleManager()           (transport teardown)
```

The capture worker owned by the platform ADM keeps calling into an `AudioTransportImpl`
that a closing peer connection is tearing down → use-after-free / bogus buffer → SIGSEGV
or SIGABRT ("pointer being freed was not allocated").

## 3. The landed fix (rust-sdks `feature/platform-audio-stability`, core commit `01fc4c13`, PR-feedback `d3bb1453`)

- `webrtc-sys` `AdmProxy`: new `ShutdownAudioIO()` + `StopAudioIO()` / `StopPlatformAudioIO()`
  helpers that stop recording/playout and detach the `AudioTransport` callback. Called from
  `~AdmProxy()`, `Terminate()`, and at platform-ADM refcount 0.
- `PeerConnectionFactory::shutdown_audio_io()` runs `AdmProxy::ShutdownAudioIO()` on the
  worker thread **before** `peer_factory_` is destroyed (`~PeerConnectionFactory`).
- `LkRuntime::Drop` calls `shutdown_audio_io()`.
- `PlatformAdmHandle::Drop` stops recording before releasing the ADM reference.
- `FfiRoom::close()` now closes the room (unpublishing tracks, stopping capture) **before**
  dropping FFI track handles.
- `FfiServer::dispose()` clears `ffi_handles` / `handle_dropped_txs`.
- (client-sdk-cpp) `PlatformAudioSource` member order swapped so the FFI handle drops before
  the shared `PlatformAudioState`.

**Verdict: keep all of it.** It is correct and directly targets the confirmed race. Two
minor corrections (see §6, Step 2).

Also landed and **correct** — keep: the `LocalParticipant::published_tracks_by_sid_` mutex
(`5380e42`), an ASan-proven data race between `publishTrack` (app thread) and
`trackPublications()`/`findTrackPublication()` (FFI callback thread). It is a genuine
general-purpose SIGSEGV source, though independent of the ADM crash.

## 4. Theories that were correctly abandoned

- **Dangling FFI handles at dispose** — instrumentation showed 0 leaked handles across
  738+ dispose samples. The `ffi_handles.clear()` hygiene was kept anyway (harmless).
- **mach-port / fd / thread leaks** (CoreAudio HAL client leak) — sampler showed all flat.
- **Progressive memory growth** — real (~1 retained `PeerConnection` per connect cycle;
  `leaks` clean → reachable retention), but *orthogonal to the crash* (crash hits the first
  PlatformAudio test, so it is not cumulative). Root cause identified in this review: see §5.

## 5. What the prior work missed

### 5.1 The rebase dropped the fix from the C++ branch

`feature/platform-audio-stability` (client-sdk-cpp) now pins submodule
`client-sdk-rust` at `dad794d4` — plain `rust-sdks` main, **without** the ADM fix.
`feature/additional_ci_improvements` still pins `d3bb1453` (has the fix). The stability
branch as it stands does not build the fix it claims to test.

### 5.2 A new, deterministic `FramesReachRemote` regression (not a crash)

Post-fix nightlies (`additional_ci_improvements`, rust `d3bb1453`), 2026-06-28 and 06-30:

- **The segfault is gone** — 100+ PlatformAudio iterations, zero SIGSEGV.
- **`PlatformAudioFramesReachRemote` fails 0/26** (all attempts, including the very first),
  debug builds only. The log shows, at publish time:
  ```
  WARN livekit::rtc_engine::peer_transport] peer connection is closed, cannot create offer
  ERROR livekit::rtc_engine::rtc_session] failed to negotiate the publisher:
        Rtc(RtcError { InvalidState, "Failed to set local offer sdp: Called in wrong state" })
  ```
  This is a **signaling / negotiation** failure, not audio teardown. It rode in on the
  upstream `rust-sdks` delta between the old pin (`8e551062`) and the new base
  (`2e83ff6b`). Prime suspect: **#1148 "harden reconnect behaviour"** (also #996 publisher
  offer with join, single-PC default). Release PR CI passes; only debug nightly reproduces.

### 5.3 A structural teardown race survives the fix

- The FFI tokio runtime is a process-static and is **never shut down**; `dispose()` clears
  handles but does **not** wait for `LkRuntime` (and thus the `PeerConnectionFactory` + ADM
  + its capture worker) to finish dropping.
- `LK_RUNTIME` is a `Mutex<Weak<LkRuntime>>`: the moment the last strong ref hits zero, a new
  `instance()` call constructs a **new** factory/ADM — potentially **while the old one's
  `Drop` (including the capture-worker stop) is still running on another thread.**
- `EngineInner::close` deliberately retains `RtcSession` ("so we can still access stats"),
  which keeps a whole `PeerConnection` (and its transports) alive per connect cycle — this
  is the source of the "reachable growth" from §4, and it defers transport destruction to
  arbitrary threads at arbitrary times.

Together these mean runtime/ADM/transport teardown from test *N* can overlap init from test
*N+1*. This matches the "crash at the start of the first PlatformAudio test" signature and is
the most plausible family for the still-open `PublishManyTracks` teardown hang (120-min
silent timeout on macos-x64; watchdog SIGABRT on linux-x64, where `gdb` is not installed so
no backtrace is produced).

## 6. Corrected fix plan

**Step 0 — hygiene (blocking):**
- Re-point the `client-sdk-cpp` `feature/platform-audio-stability` submodule at the fixed
  `rust-sdks` tip (currently `d3bb1453`, or the new tip from Step 2/3).
- Install `gdb` on the Linux CI runner so the stall watchdog's cores produce backtraces.

**Step 1 — attribute the `FramesReachRemote` regression before trusting the fix branch.**
Run the triage workflow (both arches, isolated-frames arm) across three rust pins:
`8e551062` (old baseline), `2e83ff6b` (new upstream base, *no* fix commits), `d3bb1453`
(fix). Signature to look for: `peer connection is closed, cannot create offer` ~6 s into the
test, **debug builds only**. If `2e83ff6b` alone reproduces it, the ADM fix is exonerated and
the regression is an upstream bug to bisect (start at #1148).

**Step 2 — correct the ADM fix (rust-sdks), low risk:**
- In `StopAudioIO()` / `StopPlatformAudioIO()`: call `StopRecording()`/`StopPlayout()`
  **first** (this joins the worker threads), *then* `RegisterAudioCallback(nullptr)`.
  WebRTC's `AudioDeviceBuffer` refuses a callback change while media is active, so the
  detach-before-stop ordering is a silent no-op today. Stopping first makes the detach real.
- (Investigated and **rejected** as changes: `PlatformAdmHandle::Drop` calling a global
  `stop_recording()` is safe — the handle is shared via a static `Weak`, so `Drop` only runs
  when the *last* `PlatformAudio` is gone. `AdmProxy::StopRecording()` stopping both ADMs is
  benign — the synthetic ADM has no microphone.)

**Step 3 — close the surviving teardown race (rust-sdks), the genuinely new fix:**
- Add a runtime teardown-completion signal to `LkRuntime`: a static counter + condvar,
  incremented when a runtime is created in `instance()` and decremented at the **end** of
  `Drop`. `instance()` waits (bounded, ~10 s, with a loud log on timeout) for the previous
  runtime to finish dropping before constructing a new factory/ADM — closing the
  `Mutex<Weak>` create-during-teardown window. Optionally, `FfiServer::dispose()` waits on
  the same signal after clearing handles for a clean process-exit ordering.
- **(Deferred, higher risk — recommend as follow-up, not landed automatically)** Drop the
  retained `RtcSession` in `EngineInner::close` (or snapshot stats at close instead of
  retaining the session). This removes the per-cycle `PeerConnection` retention and makes
  transport destruction deterministic. It touches reconnection/stats semantics that external
  consumers may depend on, so it should be a separately reviewed change with its own CI.

## 7. Open items / risks

- `FramesReachRemote` regression root cause (Step 1) — **must** be settled before the branch
  can be considered green; it is currently a hard, deterministic failure in debug CI.
- `PublishManyTracks` teardown hang — persists in both post-fix nightlies; needs a Linux
  backtrace (blocked on `gdb` install, Step 0).
- The teardown-completion signal adds a bounded wait in `instance()`/`dispose()`; verified by
  code inspection to be free of lock-ordering deadlock (the dropping thread never takes the
  `LK_RUNTIME` lock), but should be exercised under the triage matrix.
