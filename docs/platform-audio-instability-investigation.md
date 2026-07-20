# macOS PlatformAudio Instability — Investigation and Remediation

*Updated 2026-07-19 after reproducing the failure matrix on macOS ARM64.*

## 1. Outcome

Two separate defects had been conflated:

1. **Confirmed teardown crash.** WebRTC's macOS `CaptureWorkerThread` could
   deliver recorded audio through a stale `AudioTransportImpl` while peer
   transports were being destroyed.
2. **Release/reacquire frame regression.** The first attempted fix stopped the
   capture worker and detached the transport callback when the final
   `PlatformAudio` reference was released, but retained the platform ADM. A
   later acquire reused that ADM without restoring its callback, so publication
   and subscription succeeded while decoded frames never arrived.

The corrected lifecycle separates reusable quiescing from terminal shutdown:

- Releasing the final platform-ADM reference stops recording and playout and
  waits for their worker threads, but retains the transport callback.
- Acquiring an inactive platform ADM explicitly ensures the current callback is
  bound before capture can restart.
- Terminal peer-connection-factory teardown stops all audio I/O first, then
  detaches platform and synthetic callbacks before destroying the factory.
- `LkRuntime` generations cannot overlap teardown and construction. Generation
  logs make creation and completed teardown observable.

## 2. Reproduction matrix

The exact seam was run in one process and in declaration order:

```text
MediaMultiStreamIntegrationTest.PublishTwoVideoAndTwoAudioTracks_SinglePeerConnection
PlatformAudioIntegrationTest.PublishPlatformAudioTrackEndToEnd
```

The decoded-frame canary was also repeated independently.

| Rust revision | Seam result on ARM64 | Frame-flow result |
| --- | --- | --- |
| `da3ee007` (C++ main pin, no fix) | SIGSEGV on iteration 35 | Repeated frame flow passes |
| `fd48e5c2` (original fix tip) | 30 iterations pass | First run passes; later runs deterministically receive no frames |
| Corrected rebased candidate | Pending final stress totals below | Full suite and repeated frame flow pass |

The `da3ee007` crash report records:

- `EXC_BAD_ACCESS` / `SIGSEGV`
- faulting thread `CaptureWorkerThread`
- crash while `PublishPlatformAudioTrackEndToEnd` was starting immediately
  after the MediaMultiStream test

The original fix's full PlatformAudio suite creates one `LkRuntime` and reuses
it across all tests. Its first three subscription/lifecycle assertions pass;
`PlatformAudioFramesReachRemote` then waits 20 seconds and fails. Running that
test alone repeatedly gives the sharper signature: iteration 1 passes and
iterations 2 onward receive no frames. This proves the regression is callback
lifecycle state, not the previously suspected publisher negotiation change.

## 3. Confirmed crash mechanism

The pre-fix crash family includes stacks of this shape:

```text
webrtc::AudioDeviceMac::CaptureWorkerThread()
  -> AudioDeviceBuffer::DeliverRecordedData()
  -> AudioTransportImpl::RecordedDataIsAvailable()
```

concurrent with peer transport teardown:

```text
~JsepTransportController()
  -> ~BundleManager()
  -> transport / audio state destruction
```

Stopping capture is the synchronization boundary: WebRTC's
`StopRecording()`/`StopPlayout()` joins the platform workers. Callback
detachment must happen only after those calls because `AudioDeviceBuffer`
refuses callback changes while media is active.

## 4. Corrected implementation

The Rust SDK stability work is rebased onto Rust main `62359c35`.

### Reusable PlatformAudio release

`AdmProxy::ReleasePlatformAdm()` calls `StopPlatformAudioIO()` at refcount zero.
That helper stops and joins platform recording/playout but does **not** detach
the callback or destroy the ADM. This preserves iOS ADM reuse and allows a
later acquire on the same runtime.

`AdmProxy::AcquirePlatformAdm()` binds the saved `audio_transport_` on every
inactive-to-active transition. Besides making reacquire explicit, this covers
Android's lazily created platform ADM, which did not exist when the factory
originally registered its callback.

### Terminal factory shutdown

`PeerConnectionFactory::~PeerConnectionFactory()` invokes terminal audio
shutdown on WebRTC's worker thread before releasing `peer_factory_`.
`AdmProxy::StopAudioIO()` stops both ADMs, detaches both callbacks, and clears
the stored transport only after workers have stopped.

### Runtime generation gate

`LkRuntime` has a teardown guard that is declared after the peer connection
factory. Its static counter reaches zero only after factory and ADM destruction
complete. `LkRuntime::instance()` waits on that gate before constructing a new
generation after the prior weak reference stops upgrading.

The gate closes create-during-drop overlap; it does not force a still-referenced
runtime to drop. Debug logs identify runtime generation creation and completed
teardown so these cases can be distinguished without sleeps.

## 5. Regression coverage

`PlatformAudioIntegrationTest.ReleaseAndReacquirePreserveFrameFlow` performs two
complete publish/subscribe/decoded-frame cycles separated by destruction of the
last `PlatformAudio`, source, track, and rooms. It fails deterministically with
the callback-detaching release implementation and passes with the corrected
split lifecycle.

The Tests workflow has an opt-in `platform_audio_stress` input. On macOS x64 it
runs:

- 20 release/reacquire decoded-frame iterations
- 100 exact MediaMultiStream-to-PlatformAudio seam iterations

On failure it uploads the XML results, macOS `.ips` reports, image UUIDs, and
the matching test and dylib binaries for symbolication.

## 6. Runtime retention correction

Closed engines could remain alive briefly in detached reconnect or close tasks.
Keeping a strong `Arc<LkRuntime>` directly in `EngineInner` therefore retained
the peer connection factory after the active-session guard had stopped audio
I/O. Forcing the singleton weak reference to reset was rejected: instrumentation
showed that it created overlapping runtime generations after bounded teardown
waits expired.

`EngineInner` now stores only a weak runtime reference. The
`ActiveRtcSessionGuard` remains the authoritative strong owner while an RTC
session can use the factory, and `AudioCapturePauseGuard` owns a temporary
strong reference while sender teardown is in progress. Detached engine tasks
can finish without retaining a closed factory.

In the macOS ARM64 Release stress run, every instrumented runtime generation
completed teardown before the next generation was created. All 20
release/reacquire iterations and all 100 exact seam iterations passed without a
teardown timeout or crash.

## 7. Validation criteria

The remediation is considered ready when all of the following pass:

- Full PlatformAudio integration suite.
- Repeated release/reacquire decoded-frame test.
- At least 100 exact seam iterations in Release on macOS ARM64.
- Opt-in 100-iteration macOS x64 Actions stress run.
- C++ formatting, Rust formatting, workflow validation, and the normal test
  build.
