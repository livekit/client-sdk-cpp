# Connection Lifecycle: Connect/Disconnect Paths and Test Coverage

This document maps every path by which a `livekit::Room` can become connected or
disconnected, how each path flows through the SDK layers, and where each path is
(or is not) covered by tests.

Layers involved:

```
Application code
      │
      ▼
livekit::Room (src/room.cpp)                ← public API, state machine, delegate callbacks
      │
      ▼
livekit::FfiClient (src/ffi_client.cpp)     ← request/response + async event routing, singleton
      │
      ▼
livekit_ffi (Rust, client-sdk-rust)         ← actual signaling/WebRTC, emits proto::FfiEvent
```

## Connect paths

| # | Path | Entry point | Flow | Failure behavior |
|---|------|-------------|------|------------------|
| C1 | Explicit connect | `Room::connect(url, token, options)` (`src/room.cpp:92`) | Guard: FFI initialized, state == `Disconnected` → state = `Reconnecting`* → `addListener` → `FfiClient::connectAsync().get()` (blocking) → build participants/E2EE → publish state under lock, state = `Connected` → `readyForRoomEvent` | Any throw → state reset to `Disconnected`, listener removed, participant `shutdown()`, all state cleared; returns `false` |
| C2 | Connect via token source | `Room::connect(url, TokenSource&, opts)` (`src/token_source*.cpp`) | Fetches token, then delegates to C1 | Fetch error/throw → returns `false` without touching room state |
| C3 | Connect while already connected | `Room::connect` | State guard | Throws `std::runtime_error("already connected")`; existing session untouched |
| C4 | Connect before `livekit::initialize()` | `Room::connect` | `FfiClient::isInitialized()` guard | Returns `false`, logs error |

\* Note: the SDK has no `Connecting` state; during connect the state reads as
`Reconnecting` (`ConnectionState` in `include/livekit/room_event_types.h:49`).

## Disconnect paths

### Client-initiated

| # | Path | Trigger | What happens | Delegate callbacks | Reason seen by app |
|---|------|---------|--------------|--------------------|--------------------|
| D1 | Explicit disconnect | `Room::disconnect(reason)` (`src/room.cpp:213`) | State flipped to `Disconnected` under lock (wins any race with the event path), participant RPC drain (`LocalParticipant::shutdown`, 5 s cap), `FfiClient::disconnectAsync().get()`, dispatcher stopped, listener removed, moved-out state destructs | `onDisconnected` exactly once | Caller-supplied (default `ClientInitiated`) |
| D2 | Destructor | `Room::~Room()` (`src/room.cpp:76`) | Calls D1; swallows and logs exceptions | Same as D1 | `ClientInitiated` |
| D3 | Double disconnect / never connected | `disconnect()` when state already `Disconnected` | Early return `false`; no side effects | None | — |
| D4 | Global SDK shutdown | `livekit::shutdown()` → `FfiClient::shutdown()` (`src/ffi_client.cpp:175`) | Cancels all pending async ops, drains all listeners, `livekit_ffi_dispose()`. Does **not** notify `Room` objects: a still-connected `Room` keeps state `Connected` with dead handles | None | — |
| D5 | Process signals (SIGINT/SIGTERM) | OS | **The SDK installs no signal handlers.** Default handler terminates the process; no disconnect runs, no leave message is sent — server times the participant out | None | Server-side: timeout |
| D6 | Process exit without shutdown | `exit()` / `main` return | `FfiClient::~FfiClient` prints a warning to stderr if still initialized (`src/ffi_client.cpp:166`); static-destruction order means `Room` members may already be gone | None | — |

### Server/network-initiated

All arrive on the FFI event thread as `proto::RoomEvent` and route through
`Room::onEvent` (`src/room.cpp:424`), gated on matching `room_handle`.

| # | Path | Event | What happens | Delegate callbacks | Reason seen by app |
|---|------|-------|--------------|--------------------|--------------------|
| S1 | Server closes room / kicks participant / duplicate identity / server shutdown / signal close, etc. | `kDisconnected` (`src/room.cpp:1170`) | If state already `Disconnected` (client won the race) → skipped. Otherwise state = `Disconnected` and delegate fired. **No teardown here** — participants, room handle, and listener stay alive until S2 arrives or the user calls `disconnect()`/destructor | `onDisconnected` once | Mapped via `toDisconnectReason` (`src/room_proto_converter.cpp:118`): `RoomDeleted`, `ParticipantRemoved`, `DuplicateIdentity`, `ServerShutdown`, `SignalClose`, `RoomClosed`, … |
| S2 | FFI event-stream end | `kEos` (`src/room.cpp:1210`) | The actual server-side teardown: dispatcher stopped, listener removed, participant RPC drain, all state moved out and destroyed, state = `Disconnected` | `onRoomEos` | — |
| S3 | Automatic reconnection | `kReconnecting` / `kReconnected` (`src/room.cpp:1189`) | Pass-through to delegate only; `connection_state_` is **not** updated (stays `Connected` while the Rust layer reconnects) | `onReconnecting` / `onReconnected` | — |
| S4 | Connection state change | `kConnectionStateChanged` (`src/room.cpp:1153`) | Pass-through to delegate only; `connection_state_` not updated (open TODO in code) | `onConnectionStateChanged` | — |
| S5 | FFI panic | `event.has_panic()` in `ffiEventCallback` (`src/ffi_client.cpp:417`) | Logs critical, flushes logger, `std::raise(SIGTERM)` — process-fatal by design | None | — |
| S6 | Peer participant leaves | `kParticipantDisconnected` (`src/room.cpp:488`) | Not a room disconnect; removes the remote participant | `onParticipantDisconnected` | Per-participant reason |

### Interaction between D and S paths

- D1 flips `connection_state_` **before** sending the FFI disconnect, so the
  echoed `kDisconnected` event is deduplicated → `onDisconnected` fires exactly once.
- Conversely, after S1 fires, a subsequent `disconnect()` returns `false`
  immediately (state already `Disconnected`) — final resource cleanup then depends
  on S2 arriving or on the `Room` destructor destroying members.
- `kEos` and `disconnect()` can race; `disconnect()` takes ownership of all state
  under the lock so only one teardown path operates on it (comment at
  `src/room.cpp:233`).

## `DisconnectReason` values

Defined in `include/livekit/room_event_types.h:80`, mirroring the server enum:
`Unknown`, `ClientInitiated`, `DuplicateIdentity`, `ServerShutdown`,
`ParticipantRemoved`, `RoomDeleted`, `StateMismatch`, `JoinFailure`, `Migration`,
`SignalClose`, `RoomClosed`, `UserUnavailable`, `UserRejected`, `SipTrunkFailure`,
`ConnectionTimeout`, `MediaFailure`. Full proto↔C++ mapping lives in
`src/room_proto_converter.cpp:118`.

## Test coverage matrix

Tests live under `src/tests/{unit,integration,stress}`; integration tests need a
live server (see `docs/testing.md`).

| Path | Scenario | Test(s) | Coverage |
|------|----------|---------|----------|
| C1 | Successful connect | `integration/test_room.cpp` `ConnectToServer` | ✅ |
| C1 | Bad token / bad URL | `ConnectWithInvalidToken`, `ConnectWithInvalidUrl` | ✅ |
| C1 | Failed connect cleans up listener | `integration/test_room_listener_cleanup.cpp` (3 tests) | ✅ |
| C2 | Token-source connect (literal + custom + error paths) | `integration/test_room.cpp`, `unit/test_room.cpp` | ✅ |
| C3 | Connect while connected throws, no leak | `AlreadyConnectedConnectDoesNotReplaceOrLeakListener` | ✅ |
| C4 | Connect without initialize | `unit/test_room.cpp` `ConnectWithoutInitialize` | ✅ |
| D1 | Explicit disconnect: state, callback-once, reason | `integration/test_room.cpp` `UserDisconnect` | ✅ |
| D2 | Destructor-driven disconnect | `DestructorDisconnect`, `ParticipantHandlesExpireOnRoomDestruction` | ✅ |
| D3 | Double disconnect idempotent | asserted inside `UserDisconnect` | ✅ |
| D1/D2 | Repeated connect/disconnect cycles | `stress/test_room_stress.cpp` `RepeatedConnectDisconnect` | ✅ (stress) |
| D4 | `livekit::shutdown()` while a Room is still connected | — | ❌ none |
| D5 | SIGINT/SIGTERM during a session | — | ❌ none (by design: app responsibility, but undocumented) |
| D6 | Process exit without shutdown (warning path) | — | ❌ none |
| S1 | Server-initiated `kDisconnected` (kick, room deletion, duplicate identity) | — | ❌ none — no test kicks a participant or deletes a room |
| S2 | `kEos` teardown | — | ❌ none |
| S1×D1 | Race: server disconnect vs. client disconnect | — | ❌ none |
| S3 | Reconnecting/Reconnected callbacks | — | ❌ none |
| S4 | ConnectionStateChanged callback | — | ❌ none |
| S5 | FFI panic → SIGTERM | `unit/test_ffi_client.cpp` (SIGTERM handler + flag) | ✅ |
| S6 | Remote participant disconnects | — | ❌ none (only reachable with a second participant) |
| — | Listener use-after-free during destroy | `unit/test_ffi_client.cpp` FakeRoom race/stress tests | ✅ |
| — | `connectionState()` thread safety | `unit/test_room_callbacks.cpp` | ✅ |

### Coverage summary

**Well covered:** every *client-initiated* path — connect success/failure/misuse,
explicit disconnect, destructor teardown, idempotency, listener cleanup, and the
FFI-panic path.

**Not covered at all:** every *server-initiated* disconnect path. No test (unit or
integration) exercises `kDisconnected`, `kEos`, `kReconnecting`/`kReconnected`, or
the client-vs-server disconnect race. There is no mock/fake FFI event injector, so
these paths can currently only be reached against a live server plus a server-side
API call (delete room / remove participant).

## Gaps and observations (code)

1. **`kDisconnected` performs no teardown** (`src/room.cpp:1170`): after a
   server-side disconnect, FFI handles, participants, and the listener remain
   alive until `kEos` arrives. If `kEos` were not delivered, cleanup falls to the
   `Room` destructor. Worth documenting the assumption that Rust always follows
   `Disconnected` with `Eos`.
2. **`connection_state_` is not updated by `kConnectionStateChanged` or
   `kReconnecting`** (TODO at `src/room.cpp:1158`): during a reconnect the public
   `connectionState()` still reports `Connected`.
3. **No `Connecting` state**: `connect()` reuses `Reconnecting` while a first-time
   connection is in flight.
4. **`onDisconnected` is not fired by the `kEos` path**: if an application only
   listens for `onDisconnected` it will be told once (S1), but resources vanish
   later (S2, `onRoomEos`) — the two-phase nature is not documented in the
   delegate header.
5. **`livekit::shutdown()` doesn't coordinate with live `Room`s** — rooms are left
   `Connected` with dead handles; subsequent calls fail gracefully but noisily.
6. **Duplicate RPC drain in `kEos`**: `old_local_participant->shutdown()` is called
   twice (`src/room.cpp:1247` and `src/room.cpp:1256`). Harmless (idempotent) but
   redundant.
7. **Dead file**: `src/room_event_converter.cpp` is not in the CMake build,
   includes a nonexistent header, and contains a stub `toDisconnectReason` that
   always returns `Unknown`. Should be deleted to avoid confusion with the real
   converter in `src/room_proto_converter.cpp`.
8. **No signal handling guidance**: neither the SDK nor the README pattern shows a
   SIGINT-safe shutdown (`signal → stop main loop → room.disconnect() →
   livekit::shutdown()`); apps that Ctrl-C simply drop the connection and rely on
   server timeout.

## The Rust `SimulateScenario` hook

The Rust core exposes a chaos/E2E testing primitive that is directly relevant to
the server-initiated gaps above. It exists at the FFI boundary — proto
`SimulateScenarioRequest { room_handle, scenario }` → async
`SimulateScenarioCallback` (`livekit-ffi/protocol/room.proto`), handled by
`on_simulate_scenario` in `livekit-ffi/src/server/requests.rs`. It is the same
request/async-callback shape the C++ `FfiClient` already uses for connect/disconnect.

`SimulateScenarioKind` (FFI proto enum):

| Value | Kind | Drives | Path exercised |
|-------|------|--------|----------------|
| 0 | `SIGNAL_RECONNECT` | Closes the signal channel locally; engine attempts a **resume** | S3 reconnecting→reconnected |
| 1 | `SPEAKER` | Simulated speaker activity | `onActiveSpeakersChanged` (not lifecycle) |
| 2 | `NODE_FAILURE` | Simulated media node death → reconnection | S3 |
| 3 | `SERVER_LEAVE` | Server sends a Leave | S1 `kDisconnected` (+`kEos`) — server-initiated disconnect |
| 4 | `MIGRATION` | Server migration | S3, `Migration` reason |
| 5 | `FORCE_TCP` | Force ICE over TCP | transport reconnect |
| 6 | `FORCE_TLS` | Force ICE over TLS | transport reconnect |
| 7 | `FULL_RECONNECT` | Server sends `Leave{Reconnect}` → new `RtcSession`, tracks republished | S3, full-reconnect + republish |
| 8 | `DISCONNECT_SIGNAL_ON_RESUME` | Drops signalling mid-resume → forces resume→full escalation | S3, escalation path |

**Status in this SDK: not wrapped.** There is no reference to `simulate` anywhere
in `src/`, and `FfiClient` has no `simulateScenarioAsync`. The Rust core supports
it (and the rust-sdks own `reconnection_test.rs`, `data_channel_test.rs`, etc. use
it), but the C++ layer has never surfaced it.

**Important:** `SimulateScenario` is an *in-band* hook forwarded over the live
signalling connection — it still requires a real server. It does **not** replace a
client-only fake event injector; it replaces the need for out-of-band server-admin
API calls (DeleteRoom / RemoveParticipant) to drive the *reconnect and leave*
scenarios.

## Suggested next steps for coverage

Two complementary mechanisms, at different layers:

1. **Fake FFI event injector (unit, no server)** — the FakeRoom pattern in
   `unit/test_ffi_client.cpp` is a starting point. Lets unit tests synthesize
   `kDisconnected`, `kEos`, and reconnect events and assert callback-once
   semantics per reason, teardown on `kEos`, and the S1×D1 race. This is the only
   way to cover S2/S1×D1 deterministically and offline. `SimulateScenario` does
   *not* help here (it needs a server).

2. **Wrap `SimulateScenario` (integration, live server)** — add a thin
   `FfiClient::simulateScenarioAsync(room_handle, kind)` plus a `Room` hook (or a
   test-only accessor), mirroring `disconnectAsync` exactly; the proto is already
   generated from the submodule. This is the clean way to drive the currently
   **zero-coverage S3 reconnect paths** (`SIGNAL_RECONNECT`, `NODE_FAILURE`,
   `FULL_RECONNECT`, `DISCONNECT_SIGNAL_ON_RESUME`) and a server-initiated
   disconnect (`SERVER_LEAVE` → S1/S2) end-to-end, without server-admin calls.
   It is the primary unlock for `onReconnecting`/`onReconnected` coverage, which no
   C++ test currently touches.

Reason-specific disconnects that `SimulateScenario` does **not** cover
(`ParticipantRemoved`, `RoomDeleted`, `DuplicateIdentity`) still need either the
server-admin API or the fake event injector.

3. Add a test for `livekit::shutdown()` with a still-connected room (D4).
4. Document (and add an example for) signal-driven graceful shutdown (D5).
