# Token lifecycle

Succinct reference for how join credentials and in-session refresh interact in
the C++ SDK. For API examples, see [authentication.md](authentication.md).

## Client responsibilities

| Layer | Role |
|---|---|
| **Your app / backend** | Mints or stores the initial JWT (`lk token create`, server SDK, etc.). Keeps API secrets off the client in production. |
| **TokenSource** (C++ only) | Optional helper to `fetch()` `{ server_url, participant_token }` **before** connect. Types: literal, endpoint, sandbox, custom, caching. |
| **`Room::connect`** | Hands `(url, token)` to Rust via FFI once. Does not retain the token source. |
| **Rust core (FFI)** | Owns the session JWT after connect. Stores server-pushed refreshes internally and uses them for signal reconnect/resume. |
| **`RoomDelegate::onTokenRefreshed`** | Optional app notification when the server pushes a new JWT. Informational only — does not trigger TokenSource. |

**TokenSource is initial-connect only.** It is not called on network blips,
automatic reconnect, or server refresh. To join again after a full disconnect,
call `Room::connect` (with or without a token source) explicitly.

## Server responsibilities

The LiveKit server pushes refreshed JWTs over the **signal WebSocket**
(`SignalResponse.refresh_token` in
[livekit_rtc.proto](https://github.com/livekit/protocol/blob/main/protobufs/livekit_rtc.proto)).

Implementation (open-source server):

1. **Immediately after join** — one refresh so clients with near-expiry join
   tokens still get a fresh credential.
2. **Periodically while connected** — `tokenRefreshInterval` (currently
   `5 * time.Minute`, hardcoded in
   [roommanager.go](https://github.com/livekit/livekit/blob/master/pkg/service/roommanager.go#L61-L64)).
3. **On participant changes** — name, permissions, or metadata updates (see
   [docs: Token refresh](https://docs.livekit.io/frontends/reference/tokens-grants/#token-refresh)).

Each refresh re-mints a JWT with the **same grants/identity** and a new
`exp`. The client SDK applies it internally; C++ forwards
`RoomEvent::TokenRefreshed` to `onTokenRefreshed`.

References:

- Session loop:
  [roommanager.go#L761-L774](https://github.com/livekit/livekit/blob/master/pkg/service/roommanager.go#L761-L774)
- Mint + send:
  [refreshToken()](https://github.com/livekit/livekit/blob/master/pkg/service/roommanager.go#L1140-L1170),
  [SendRefreshToken()](https://github.com/livekit/livekit/blob/master/pkg/rtc/participant_signal.go#L156-L157)

## Join `valid-for` vs. refresh vs. reconnect

Three different time concepts:

| Concept | Who sets it | What it bounds |
|---|---|---|
| **Join token `valid-for`** | Your app when minting the initial JWT (`lk token create --valid-for`, server SDK `SetValidFor`, etc.) | Whether the **first** `Room::connect` is accepted. |
| **Refreshed token TTL** | Server (`tokenDefaultTTL = 10 * time.Minute` in [roommanager.go](https://github.com/livekit/livekit/blob/master/pkg/service/roommanager.go#L63), or remaining join-token lifetime if longer) | How long each **server-pushed** refresh JWT remains valid. |
| **Refresh interval** | Server only (`5 * time.Minute`; not documented as a public SLA) | How often the server **re-pushes** while the session stays up. |

### How they interact

```
Join                    Connected session                    Disconnect
  |                              |                                |
  |-- connect(join JWT) -------->|                                |
  |                              |-- server: immediate refresh -->| (onTokenRefreshed)
  |                              |-- server: refresh every ~5m --->| (optional repeats)
  |                              |-- SDK stores latest JWT ------>| (Rust internal)
  |                              |                                |
  |<-------- resume / signal reconnect uses latest stored JWT ---|
  |                              |                                |
  |                              X-- join JWT may be expired ------|  OK if refresh received
```

**While connected:** Expiration of the **original** join token does not drop
the session. The server keeps issuing refreshed JWTs; the SDK keeps the latest
one for reconnect. Per
[Tokens & grants](https://docs.livekit.io/frontends/reference/tokens-grants/#token-refresh):
*"Expiration time only impacts the initial connection, and not subsequent
reconnects."*

**On disconnect/reconnect:**

- **Automatic reconnect/resume** (still in a session): Rust uses the **last
  server-refreshed** JWT, not your token endpoint and not TokenSource.
- **New `Room::connect` after teardown**: You need a **new join credential**
  (app, TokenSource, or `lk`/server SDK). Server refresh does not substitute
  for this path.

**Reconnect window:** After a network drop, reconnect succeeds if the client
received at least one refresh whose `exp` has not passed. Refreshed tokens
live up to **10 minutes** (or longer if the join token had more remaining
lifetime). A long offline gap can fail reconnect even though the session felt
"continuous" — there was no refresh while disconnected.

**Practical guidance:**

- Short join `valid-for` (e.g. 5m) is fine for connected clients; server
  refresh extends the effective reconnect horizon.
- Very short join tokens still work if the server sends its **immediate**
  post-join refresh before the join JWT expires.
- For manual rejoin after disconnect, mint a new token or call TokenSource
  again; do not rely on `onTokenRefreshed` alone unless your app cached the
  latest refreshed JWT.

## Verifying refresh in this repo

Integration test `RoomTest.ServerRefreshTokenFiresDelegate` in
`src/tests/integration/test_room.cpp` asserts `onTokenRefreshed` fires shortly
after connect against a local `livekit-server --dev` (immediate server refresh,
no 5-minute wait).
