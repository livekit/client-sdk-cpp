# Testing

The SDK includes integration and stress tests using
[Google Test](https://github.com/google/googletest).

## Building the test binaries

**Linux/macOS:**
```bash
./build.sh debug-tests      # Build Debug with tests
./build.sh release-tests    # Build Release with tests
```

**Windows:**
```powershell
.\build.cmd debug-tests
.\build.cmd release-tests
```

## Running tests

After building, run tests using `ctest` or invoke the binaries directly:

```bash
# Run all tests via ctest
cd build-debug
ctest --output-on-failure

# Or run test executables directly
./build-debug/bin/livekit_integration_tests
./build-debug/bin/livekit_stress_tests

# Run specific test suites
./build-debug/bin/livekit_integration_tests --gtest_filter="*Rpc*"
./build-debug/bin/livekit_stress_tests --gtest_filter="*MaxPayloadStress*"
```

__Note:__ The tests require tokens and a running LiveKit server. See the section below for details.

## Test binaries

| Executable | Description |
|------------|-------------|
| `livekit_unit_tests` | Pure unit tests (no server required) |
| `livekit_integration_tests` | Quick tests (~1-2 minutes) for SDK functionality |
| `livekit_stress_tests` | Long-running tests (configurable, default 1 hour) |

## Running a local LiveKit server for tests

The integration and stress suites need a running LiveKit server. The easiest
path is `livekit-server --dev`, which uses the well-known dev API
key/secret (`devkey` / `secret`).

Install [`livekit-server`](https://docs.livekit.io/home/self-hosting/local/)
and start it with data tracks enabled:

```bash
livekit-server --dev
```

## Environment variables

The integration and stress test suites (data tracks, RPC, media multistream,
etc.) require a server URL and two participant tokens:

```bash
# Required
export LIVEKIT_URL="ws://localhost:7880"            # or wss://your-server.livekit.cloud
export LIVEKIT_TOKEN_A="<first participant token>"
export LIVEKIT_TOKEN_B="<second participant token>"

# Optional (for stress tests)
export RPC_STRESS_DURATION_SECONDS=3600   # Test duration (default: 1 hour)
export RPC_STRESS_CALLER_THREADS=4        # Concurrent caller threads (default: 4)
```

### Generating tokens for the test suites

The easiest path is to source the helper script, which mints both
participant tokens against a local `livekit-server --dev` and exports
`LIVEKIT_TOKEN_A`, `LIVEKIT_TOKEN_B`, and `LIVEKIT_URL` for the current shell:

```bash
source scripts/set-test-tokens.sh
```

To generate tokens manually (e.g. against a non-default server), install
[`livekit-cli`](https://docs.livekit.io/home/cli/cli-setup/) and run:

```bash
export LIVEKIT_TOKEN_A="$(lk token create --api-key devkey --api-secret secret -i cpp-test-a \
  --join --valid-for 99999h --room cpp_data_track_test \
  --grant '{"canPublish":true,"canSubscribe":true,"canPublishData":true}' \
  --token-only)"
export LIVEKIT_TOKEN_B="$(lk token create --api-key devkey --api-secret secret -i cpp-test-b \
  --join --valid-for 99999h --room cpp_data_track_test \
  --grant '{"canPublish":true,"canSubscribe":true,"canPublishData":true}' \
  --token-only)"
```

## Installed SDK packaging tests

`src/tests/packaging/` is a standalone CMake consumer of an installed SDK.
CTest registers `InstalledSdk.Capture` with the `packaging` and `integration`
labels and a 60-second timeout. It checks the bundle's exact `cargo_features`
against `LIVEKIT_EXPECT_SOURCES` (`all`, `none`, or a comma-separated source list).
If that setting is empty, `LIVEKIT_EXPECT_CAPTURE=ON` means all sources and `OFF`
means none. Explicit source selections determine the expected capture flag.
The test creates a GStreamer VP8 source when that source is selected. It also
requires each omitted backend to report the feature-disabled error. Configuration
checks that `include/livekit/capture/` is present only in capture bundles and
that direct inclusion of `livekit/capture/capture_source.h` succeeds only in
those bundles. The obsolete flat capture header must be absent. Both variants must
still compile with `livekit/livekit.h`. It needs no LiveKit server.
GStreamer-enabled runs require the pipeline's plugins. Set
`LIVEKIT_VERIFY_PIXEL_SOURCES=ON` to also require successful pattern and clock
creation when selected. This needs a compatible GPU or software adapter;
creation failures fail the test. Without that option, an explicit missing-GPU-adapter error verifies that the
backend is available but reports that creation was not exercised. Other errors
still fail. Omitted pixel sources are always tested for rejection.
The consumer also checks `share/livekit/build-info.json`. Release jobs pass
`LIVEKIT_EXPECT_VERSION` to check version metadata and the recorded Rust commit
and FFI version through this same project.

From the repository root:

```bash
source scripts/set-test-tokens.sh
./build.sh debug --capture --bundle --prefix sdk-out/livekit-sdk-capture
cmake -S src/tests/packaging -B build-packaging-tests \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$PWD/sdk-out/livekit-sdk-capture" \
  -DLIVEKIT_EXPECT_CAPTURE=ON
cmake --build build-packaging-tests --config Debug
ctest --test-dir build-packaging-tests -C Debug --output-on-failure -L packaging
```

Use `LIVEKIT_EXPECT_CAPTURE=OFF` when testing a standard bundle. A mismatch
between the expected capability and the installed header fails the test. In
capture-enabled CI release jobs, the expectation is explicitly `ON`; a disabled
bundle cannot pass by rejecting source creation. On Windows, add the installed
SDK's `bin` directory and GStreamer's `bin` directory to `PATH` before running CTest.

### Selective capture in CI

The Ubuntu 24.04 x64 test job checks `none`, each singleton source, and `all`.
Singleton builds reuse the C++ build tree and do not compile the unit/integration
suites. Pattern and clock builds run before GStreamer installation. This runner
uses Mesa software Vulkan, and pixel-source creation must succeed. The full test
suite runs once after rebuilding with all sources.

The reusable `test-capture-variants` action uses `build-sdk` and
`test-sdk-bundle`. The latter accepts `expected-sources` and
`verify-pixel-sources`, so build, test, and release workflows share one installed
consumer test. Nightly runs enable `extended_capture_sources`, adding the three
two-source combinations on Linux and selective bundles on macOS arm64 and
Windows x64. Windows validates exact metadata and source availability, allowing
an explicit missing-adapter error for enabled pixel sources. Successful pixel
creation is required on Linux and macOS. Manual test-workflow runs can enable
the same coverage.

Cargo caches include the build configuration, build type, and source selection.
Each variant reports elapsed time and cache hits in the job summary.
Each selective variant has its own cache, saved after its installed-bundle check
passes on trusted main runs. The variant's temporary Cargo directory is then
removed to bound disk usage. Standard/all outputs use the existing shared cache,
saved after the full suite passes. Rust variants still compile separately on
cold runs even though C++ dependencies are reused.

## Test coverage

- **SDK initialization**: initialize / shutdown lifecycle.
- **Room**: room creation, options, connection.
- **Audio frame**: frame creation, manipulation, edge cases.
- **RPC**: round-trip calls, max payload (15 KB), timeouts, errors, concurrent calls.
- **Stress**: high throughput, bidirectional RPC, memory pressure.

## Memory checks (valgrind)

Run `valgrind` against the test binaries to check for memory leaks and other
issues. See [tools.md](tools.md) for the recipe.
