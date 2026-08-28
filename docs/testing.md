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

## Offline room-operation reproducer

`livekit_disconnect_offline_tester` is a standalone POSIX-only tester for an
offline `Room::disconnect()` or `LocalParticipant::unpublishTrack()` call. It
publishes a local video track, places a loopback TCP fault proxy in front of a
`ws://` LiveKit server, resets the active signal connection, waits for the room
to enter `Reconnecting`, and invokes the selected operation while reconnect
traffic remains frozen. Forwarding resumes after the chosen observation
period.

The tester keeps its `RoomDelegate` alive throughout disconnect. This follows
the corrected shutdown ordering from issue #222 and isolates the blocking
behavior from the original report's separate dangling-delegate risk.

Build it with the normal test build:

```bash
./build.sh debug-tests
```

Supply a non-TLS (`ws://`) server URL and token, either directly or through the
normal test environment:

```bash
export LIVEKIT_URL=ws://localhost:7880
export LIVEKIT_TOKEN_A='<token with room-join permission>'
./build-debug/bin/livekit_disconnect_offline_tester \
  --operation disconnect --offline-duration-ms 10000

# Exercise the related unpublishTrack wait reported in the follow-up.
./build-debug/bin/livekit_disconnect_offline_tester \
  --operation unpublish-track --offline-duration-ms 10000
```

The proxy cannot be used with `wss://`: it tunnels raw TCP through
`127.0.0.1`, which does not preserve the server hostname required for TLS
certificate validation. The program prints the elapsed time spent inside
the selected operation; an elapsed time near or above the offline duration
reproduces the reported blocking behavior. A healthy implementation should
return promptly without waiting for proxy forwarding to resume. With the
current Rust FFI, the unpublish variant can remain blocked even after forwarding
resumes and must be terminated manually; this reproduces the missing
`UnpublishTrackCallback`, not a fault in the tester.

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

## Test coverage

- **SDK initialization**: initialize / shutdown lifecycle.
- **Room**: room creation, options, connection.
- **Audio frame**: frame creation, manipulation, edge cases.
- **RPC**: round-trip calls, max payload (15 KB), timeouts, errors, concurrent calls.
- **Stress**: high throughput, bidirectional RPC, memory pressure.

## Memory checks (valgrind)

Run `valgrind` against the test binaries to check for memory leaks and other
issues. See [tools.md](tools.md) for the recipe.
