# Memory Regression Testing Plan

## Purpose

Build durable CI coverage for memory regressions across the supported C++ SDK
platform matrix without depending on the legacy C FFI or shipping internal
lifecycle counters as production ABI.

The preferred long-term model has two complementary layers:

1. A bridge-independent C++ executable that repeatedly exercises public SDK
   workflows and detects process-level memory growth.
2. Platform-native profilers that attribute growing allocations during longer
   scheduled runs.

The C++ executable should remain useful after a future C FFI to UniFFI
migration. A Rust-native lifecycle suite should eventually complement it, not
replace it, because only the C++ test covers the entire deployed stack: C++
wrapper, language bridge, Rust SDK, WebRTC, and platform frameworks.

## Investigation That Motivated This Plan

The original reproduction repeatedly connected to and disconnected from a
room. RSS increased slowly on macOS even though all instrumented C++, FFI,
Rust, WebRTC, task, callback, and thread lifecycles balanced.

Two `malloc_history -allByCount` snapshots, taken 100 iterations apart in the
same process, isolated a linear allocation signature:

```text
livekit_signaling::get_livekit_url
  -> os_info::get
  -> NSPropertyListSerialization
  -> CFPropertyListCreateWithData
```

The platform lookup created autoreleased Foundation objects on Tokio worker
threads, which do not supply an Objective-C autorelease pool. The fix wraps the
Apple platform lookup in `objc2::rc::autoreleasepool` and returns owned Rust
strings from the pool.

Before the fix, the `os_info` stacks grew by approximately 500 allocations and
52.8 KiB over 100 connection cycles. After the fix, the same comparison showed
one allocation and 1.28 KiB of non-linear variation. In a subsequent
unprofiled 1,000-cycle run, allocator-reported live heap was 1.03 MiB at cycle
347 and remained 1.03 MiB at cycle 1,000.

This is an important calibration example: the current 20-iteration PR memory
smoke would accumulate only roughly 10 KiB from this leak, far below its broad
256-786 MiB final-RSS ceilings.

## Current Repository State

The repository already contains a useful foundation:

- `src/tests/manual/memory_lifecycle_tester/main.cpp` exercises the public C++
  SDK and links only against `livekit`.
- `scripts/track_process_memory.py` monitors a child process on Linux, macOS,
  and Windows.
- `.github/workflows/tests.yml` runs memory lifecycle smoke commands across:
  - Ubuntu 22.04 x64 and ARM64;
  - Ubuntu 24.04 x64 and ARM64;
  - macOS x64 and ARM64;
  - Windows x64.
- `.github/workflows/nightly.yml` supplies larger memory-test iteration counts.
- The tester supports source, connection, media, data-track, data-frame,
  receive, and repeated SDK initialization/shutdown workloads.

The current CI gate is primarily an absolute final-RSS cap. This is useful as
an out-of-control safety limit but is not a sensitive leak detector.

At the time this plan was written, the Rust submodule was on branch
`alan/bugfix-signaling-leak`, based on `main` at `bd1a27fc`. The required
autorelease-pool fix was staged without a commit in these files:

- `client-sdk-rust/Cargo.lock`
- `client-sdk-rust/livekit-signaling/Cargo.toml`
- the platform-information portion of
  `client-sdk-rust/livekit-signaling/src/lib.rs`

Temporary lifecycle instrumentation and the existing
`webrtc-sys/src/objc_video_factory.mm` work were left unstaged. A safety stash
named `codex-signaling-leak-branch-transfer` was also retained in the Rust
submodule.

## Design Principles

### Keep the durable tester bridge-independent

The permanent C++ memory tester should:

- use only public C++ SDK APIs;
- link only against `livekit`;
- avoid internal C FFI and UniFFI diagnostic methods;
- run one clearly named scenario per process;
- produce structured results that CI can evaluate and archive;
- support a profiler checkpoint protocol without exposing SDK internals.

The temporary C FFI lifecycle counters were valuable during diagnosis, but
they should not ship or become a compatibility obligation.

### Separate detection from attribution

The portable tester answers:

> Does repeatedly performing this public SDK lifecycle cause sustained process
> memory growth?

Platform-native tools answer:

> Which live allocation stacks grew between two lifecycle checkpoints?

Neither layer is sufficient alone. RSS sees the complete process but is noisy;
traditional leak detectors find unreachable allocations but may miss
reachable, monotonically growing caches or tasks.

### Test scenarios independently

Run separate processes for at least these scenarios:

- unused audio/video sources;
- room connect/disconnect only;
- SDK initialize/work/shutdown cycles;
- publish/capture/unpublish software audio/video;
- data-track lifecycle;
- data-frame delivery;
- two-participant software receive/decode.

Separate processes prevent one workload's caches from obscuring another and
make failures immediately attributable. In particular, basic
connect/disconnect must be its own CI scenario.

## Tester Improvements

Evolve `livekit_memory_lifecycle_tester` rather than relying on
`server_room_shutdowner` as the permanent test.

Recommended command-line and output changes:

- Rename `--ffi-cycles` to `--sdk-cycles`; temporarily retain the old spelling
  as an alias.
- Add `--warmup-iterations N`.
- Add `--settle-ms N` or a bounded "wait until quiescent" phase.
- Add `--json-output PATH` containing metadata, samples, and summary metrics.
- Record memory after iterations or small iteration batches, rather than only
  on a wall-clock timer.
- Record process thread count alongside memory.
- Add a platform-neutral checkpoint mechanism for external profilers.

A file-based checkpoint protocol is a reasonable portable option:

1. The tester writes a `ready` marker containing its PID, iteration, and phase.
2. The external driver takes the native heap snapshot.
3. The driver writes a `continue` marker.
4. The tester resumes with a bounded timeout and clear failure message.

This avoids test-only FFI ABI, Unix-only signals, and shell-specific stdin
coordination.

Suggested JSON fields:

```text
scenario
platform
architecture
build_type
iterations
warmup_iterations
iteration
elapsed_ms
rss_kib
private_bytes_kib (when available)
thread_count
initial_post_warmup_kib
final_kib
peak_kib
growth_kib
slope_kib_per_iteration
early_window_median_kib
late_window_median_kib
settled
```

## Regression Signals

Do not use final RSS alone as the primary verdict. Allocator caches, thread
stacks, framework initialization, and runner pressure all make it noisy.

After a warm-up window, calculate:

- total growth from end-of-warm-up to final;
- robust slope in KiB per iteration;
- median memory in early, middle, and late windows;
- peak RSS;
- final and peak thread counts;
- memory and thread behavior during a final settling period.

Use a dual failure condition:

```text
fail when:
    post-warmup growth > scenario/OS growth floor
and slope > scenario/OS slope limit
```

Requiring both conditions prevents a one-time cache allocation from looking
like a leak. Median-window comparison or a Theil-Sen slope is preferable to a
simple first-to-last line because it is resistant to isolated page releases
and thread-stack events.

Keep an absolute RSS cap as a secondary catastrophic safeguard.

Thresholds should be keyed by scenario, OS, architecture, and build type.
Collect approximately 20-30 successful scheduled runs before making tight
thresholds blocking. During calibration, publish warnings and artifacts.

## Proposed CI Tiers

| Tier | Matrix | Build | Typical workload | Purpose |
| --- | --- | --- | --- | --- |
| PR smoke | All seven existing entries | Release | 50-100 simple cycles; 10-20 complex cycles | Catch crashes and egregious growth |
| Nightly growth | All seven existing entries | Release | 1,000-5,000 connect cycles; 100-250 media/receive cycles | Detect slow, platform-specific trends |
| Nightly profiler | One representative runner per OS | Release with symbols | 100-500 cycles with two heap snapshots | Attribute growing allocation stacks |
| Sanitizer | Linux x64 initially | Dedicated ASan/LSan build | Short lifecycle suite | Detect unreachable leaks and memory errors |

### PR smoke

Reuse the normal release test build to avoid another expensive Rust/WebRTC
build. Run each selected scenario as a separate process.

Initial PR thresholds should intentionally catch only serious regressions. An
illustrative starting point is to require both more than 8 MiB post-warm-up
growth and more than 32 KiB per cycle before failing. Actual values must be
calibrated from CI data.

The subtle macOS leak investigated here is below the intended PR sensitivity.
That is acceptable as long as the nightly profiler layer detects it.

### Nightly growth

Use release builds for product-like allocation behavior and faster execution.
Do not use the existing debug build as the canonical memory baseline.

Run long black-box tests on every supported OS/architecture. Use multiple
checkpoints and, for short enough scenarios, two or three repetitions. Upload
the structured sample data even when the test passes.

### Dedicated workflow

Prefer a reusable `.github/workflows/memory.yml` for the long and native
profiling jobs instead of continually expanding `tests.yml`. The lightweight
PR smoke can remain in `tests.yml` initially because it reuses that job's
existing build.

Potential reusable-workflow inputs:

```yaml
memory_mode: off | smoke | long | profile
memory_simple_iterations: 100
memory_complex_iterations: 20
memory_warmup_iterations: 20
memory_repetitions: 1
memory_fail_on_growth: true
memory_upload_native_profile: false
```

## Native Profiler Strategy

### macOS

Use the workflow proven during this investigation:

1. Build release artifacts with line-table debug information and without
   stripping symbols. Keep this as a separate build/cache key.
2. Launch the tester with `MallocStackLogging=lite`.
3. Pause after warm-up and capture `malloc_history -allByCount`.
4. Run the measurement iterations and capture the same report again.
5. Normalize stacks and compare live allocation counts and bytes.
6. Run `leaks` at the final checkpoint.
7. Upload raw reports, the normalized delta, and optionally a `.memgraph`.

Apple documents the relevant tooling:

- <https://developer.apple.com/library/archive/documentation/Performance/Conceptual/ManagingMemory/Articles/FindingLeaks.html>
- <https://developer.apple.com/library/archive/documentation/Performance/Conceptual/ManagingMemory/Articles/FindingPatterns.html>
- <https://developer.apple.com/library/archive/documentation/Performance/Conceptual/ManagingMemory/Articles/MallocDebug.html>

Profiler attachment can itself create allocations and threads. Compare exact
stacks within the same process and filter profiler/system attachment stacks.

Run the deep profiler on macOS ARM64 initially. Keep ordinary long black-box
runs on both macOS architectures.

### Linux

Use complementary tools:

- LeakSanitizer for unreachable allocations at process exit.
- Valgrind/Memcheck or `heaptrack` snapshots for deeper allocation analysis.

LeakSanitizer documentation:

- <https://clang.llvm.org/docs/LeakSanitizer.html>

Valgrind manual:

- <https://valgrind.org/docs/manual/valgrind_manual.pdf>

Start native profiling on Ubuntu x64 only. WebRTC under Valgrind is expensive,
so use isolated scenarios and bounded iteration counts. Maintain narrow,
reviewed suppressions and upload XML/text output.

A C++-only ASan/LSan build does not fully instrument Rust. A true
cross-language sanitizer build is a separate project involving Rust sanitizer
flags, compatible linking, debug symbols, and isolated caches. Do not claim
full Rust coverage until that configuration is verified.

### Windows

Use working set plus private bytes for portable growth checks. Pilot UMDH for
nightly allocation-stack comparisons:

1. Enable the user-mode stack trace database for the tester with GFlags.
2. Launch the symbolized tester.
3. Capture a UMDH snapshot after warm-up.
4. Capture a second snapshot after the measurement window.
5. Run UMDH's snapshot comparison.
6. Upload both snapshots and the comparison.
7. Disable the image-specific GFlags setting in an `always()` cleanup step.

Microsoft documents this workflow here:

- <https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/using-umdh-to-find-a-user-mode-memory-leak>

First verify that the selected GitHub Windows runner includes compatible
Debugging Tools for Windows and that both Rust and C++ frames symbolize. If
UMDH is unreliable in hosted CI, keep it as a manually dispatched diagnostic
job and retain private-byte slope checks as the Windows gate.

## Artifacts and Reporting

Upload these artifacts for every memory job:

- tester stdout/stderr;
- JSON summary;
- per-iteration or per-batch samples;
- platform and runner metadata;
- native heap snapshots and normalized deltas for profiler jobs;
- server logs on failure.

Write a compact Markdown table to `GITHUB_STEP_SUMMARY` showing scenario,
warm-up memory, final memory, peak, growth, slope, thread-count change, and
verdict.

GitHub artifacts alone are not a durable trend store. Initial thresholds can
be committed configuration. If historical trend analysis becomes important,
publish nightly JSON summaries to a durable dashboard or object store rather
than depending on expiring workflow artifacts.

Avoid building both a PR and its base commit in every job: that would roughly
double the expensive Rust/WebRTC build. Same-run base-versus-PR comparison can
be considered later for a single representative platform or only when memory-
sensitive paths change.

## Rust-Side Testing

Eventually add Rust-native lifecycle tests close to ownership boundaries:

- use `Weak` references and drop sentinels to assert owner release;
- retain and explicitly terminate task handles;
- use test-owned `JoinSet`, task trackers, or completion channels;
- assert that room/session teardown completes within a timeout;
- keep test counters under `cfg(test)` or a non-production test feature.

Do not expose permanent counters through C FFI merely for CI. If UniFFI later
offers a clean test-only interface, it can assist diagnosis, but black-box
process memory remains the durable cross-layer regression signal.

## Implementation Sequence

### Phase 1: Improve the existing tester

- Add an isolated connect/disconnect scenario.
- Rename `--ffi-cycles` to `--sdk-cycles` with a compatibility alias.
- Add warm-up iterations, settling, thread counts, and JSON output.
- Add iteration-aware sampling and summary statistics.
- Add a cross-platform profiler checkpoint protocol.
- Add tests for option parsing and slope/window calculations.
- Update `src/tests/manual/memory_lifecycle_tester/README.md` and
  `docs/testing.md`.

### Phase 2: Strengthen PR smoke coverage

- Continue running on the full existing matrix.
- Use separate processes per scenario.
- Run release builds.
- Retain a broad absolute cap.
- Add deliberately loose growth-plus-slope gates.
- Upload JSON results and add a job summary.

### Phase 3: Add long nightly growth tests

- Introduce `.github/workflows/memory.yml`.
- Run long release-mode tests across the full matrix.
- Separate simple and media-heavy iteration budgets.
- Begin in report-only mode.
- Collect enough runs to calibrate per-platform thresholds.

### Phase 4: Add native profiler jobs

- macOS ARM64: `MallocStackLogging=lite`, `malloc_history`, and `leaks`.
- Ubuntu x64: LSan plus a bounded Valgrind or heaptrack job.
- Windows x64: UMDH pilot with guaranteed GFlags cleanup.
- Normalize and summarize growing allocation stacks.
- Upload raw reports even on success.

### Phase 5: Make calibrated regressions blocking

- Establish scenario/OS/architecture budgets.
- Promote strong, repeatable regressions from warnings to failures.
- Keep known one-time caches separate from iteration-proportional growth.
- Periodically review suppressions and runner-image changes.

### Phase 6: Remove investigation-only instrumentation

- Remove temporary C FFI lifecycle APIs and counters.
- Remove native PeerConnection/factory diagnostic counters.
- Keep the bridge-independent lifecycle tester and profiling scripts.
- Move appropriate ownership assertions into Rust-native tests.

## Open Decisions

- Whether the PR smoke should remain in `tests.yml` or immediately move to
  `memory.yml` at the cost of another build.
- Initial warm-up and settle values per scenario.
- Which robust slope estimator to implement.
- Whether Linux deep profiling should start with Valgrind or heaptrack.
- Whether UMDH is reliable on the selected hosted Windows image.
- Where long-term nightly metrics should be stored.
- Which thresholds should initially warn versus fail.

## Resume Checklist

When work resumes:

1. Inspect the Rust submodule branch, staged diff, unstaged diagnostics, and
   safety stash before changing anything.
2. Decide whether to land the signaling fix independently of the CI work.
3. Remove or preserve investigation instrumentation deliberately; do not
   accidentally stage it with the fix.
4. Run a manually dispatched baseline campaign across the existing matrix.
5. Implement Phase 1 in the public C++ lifecycle tester.
6. Add report-only structured results before introducing tight gates.
7. Prototype native profiler automation one OS at a time, beginning with the
   already-proven macOS workflow.

