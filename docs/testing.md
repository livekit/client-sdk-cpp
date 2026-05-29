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

__Note:__ The tests require tokens and a running LiveKit server. See [Running a local LiveKit server for tests](#running-a-local-livekit-server-for-tests) for details.

## Test binaries

| Executable | Description |
|------------|-------------|
| `livekit_unit_tests` | Pure unit tests (no server required) |
| `livekit_integration_tests` | Quick tests (~1–2 minutes) for SDK functionality |
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
source .token_helpers/set_data_track_test_tokens.bash
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
