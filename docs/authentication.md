# Authentication

LiveKit rooms require a **WebSocket URL** and a **participant JWT** to join.
This document covers how to obtain those credentials in C++ and how that
relates to in-session token refresh.

## Initial connect vs. in-session refresh

These are separate mechanisms:

| | **Initial connect** | **In-session token refresh** |
|---|---|---|
| **When** | Before or at `Room::connect` | While already connected |
| **Who provides the JWT** | Your app, backend, or token server | LiveKit server (signal channel) |
| **C++ API** | `Room::connect(url, token, …)` or `Room::connect(token_source, …)` | `RoomDelegate::onTokenRefreshed` (informational) |
| **Rust / FFI** | JWT passed once via `connectAsync` | Handled internally; C++ receives a `TokenRefreshed` event |

**Token sources are for initial connection only.** After a successful connect,
the Rust core owns the session JWT. The SDK applies server-pushed token updates
internally (for reconnect). Your token endpoint is **not** called again unless
**you** invoke `Room::connect` again (for example after a full disconnect).

If you need the latest JWT after a server refresh, implement
`RoomDelegate::onTokenRefreshed` and store `TokenRefreshedEvent::token` in your
application.

Cross-platform background: [LiveKit authentication docs](https://docs.livekit.io/frontends/build/authentication/).

## Generating Tokens for Quick Development

For local development against `livekit-server --dev`, install
[`livekit-cli`](https://docs.livekit.io/home/cli/cli-setup/) and mint a token:

```bash
export LIVEKIT_URL=ws://localhost:7880
export LIVEKIT_TOKEN=$(lk token create \
  --api-key devkey \
  --api-secret secret \
  -i my-participant \
  --join \
  --valid-for 24h \
  --room my-room \
  --grant '{"canPublish":true,"canSubscribe":true,"canPublishData":true}' \
  --token-only)
```

For integration tests that need two participants, see
[testing.md](testing.md#generating-tokens-for-the-test-suites) and the helper
script under `.token_helpers/`.

## Direct connect (URL + token)

When you already have credentials, pass them directly — no token source
required:

```cpp
#include "livekit/livekit.h"

livekit::initialize(livekit::LogLevel::Info);

livekit::Room room;
livekit::RoomOptions options;

if (!room.connect(url, token, options)) {
  // handle failure
}
```

Use this for prototypes, tests, or when your app fetches the JWT outside the
SDK.

## Token sources (initial connect)

Token sources are C++ helpers that call `fetch()` to produce a
`TokenSourceResponse` (`server_url` + `participant_token`), then connect:

```cpp
#include <livekit/token_source.h>

auto source = /* LiteralTokenSource, EndpointTokenSource, etc. */;
if (!room.connect(*source, options)) { /* fixed source */ }
// or, for configurable sources:
if (!room.connect(*source, request_options, options)) { /* ... */ }
```

`Room` does **not** retain the token source after connect. Calling `fetch()`
again only happens when you call `Room::connect` again with that source.

### Fixed vs. configurable

| Base class | `fetch` signature | Use when |
|---|---|---|
| `TokenSourceFixed` | `fetch()` | Credentials are fully determined without per-call options |
| `TokenSourceConfigurable` | `fetch(options, force_refresh)` | Room, identity, agent dispatch, etc. vary per connect |

### Token source types

| Type | Class | Typical use |
|---|---|---|
| **Literal** | `LiteralTokenSource` | Static URL + JWT, or lazy async provider |
| **Endpoint** | `EndpointTokenSource` | Production: HTTP token server on your backend |
| **Sandbox** | `SandboxTokenSource` | Development: LiveKit Cloud sandbox token server |
| **Custom** | `CustomTokenSource` | Your own async credential logic |
| **Caching** | `CachingTokenSource` | Decorator: JWT-aware cache around a configurable source |

---

### Literal

Static credentials you already have, or credentials loaded asynchronously
(keychain, secure storage, your auth service):

```cpp
#include <livekit/token_source.h>

// Static URL + JWT
auto source = livekit::LiteralTokenSource::fromValue(url, jwt);
if (!room.connect(*source, options)) {
  return 1;
}

// Async provider (same contract as JS TokenSource.literal(async () => …))
auto source2 = livekit::LiteralTokenSource::fromProvider([]() {
  return std::async(std::launch::async, [] {
    livekit::TokenSourceResponse details;
    details.server_url = /* ... */;
    details.participant_token = /* ... */;
    return livekit::Result<livekit::TokenSourceResponse, livekit::TokenSourceError>::success(details);
  });
});
```

### Endpoint

Recommended for production. Keeps API keys on your server; the SDK POSTs (or
GETs) to your token endpoint with optional room, participant, and agent fields.

Request and response formats follow the
[standard token server contract](https://docs.livekit.io/frontends/build/authentication/endpoint/).

```cpp
livekit::TokenRequestOptions request;
request.room_name = "my-room";
request.participant_identity = "user-123";

livekit::TokenEndpointOptions endpoint_options;
endpoint_options.method = "POST";  // default; set to "GET" if your server requires it
endpoint_options.headers["Authorization"] = "Bearer your-api-token";

auto source = livekit::EndpointTokenSource::fromUrl(
    "https://your-backend.example.com/token",
    std::move(endpoint_options));

if (!room.connect(*source, request, options)) {
  return 1;
}
```

### Sandbox (development only)

Uses the LiveKit Cloud sandbox token server. Do not use in production.

```cpp
auto source = livekit::SandboxTokenSource::fromSandboxId(
    "your-sandbox-id",
    {},
    "https://cloud-api.livekit.io");  // optional base URL override

livekit::TokenRequestOptions request;
request.agent_name = "my-agent";  // optional agent dispatch

if (!room.connect(*source, request, options)) {
  return 1;
}
```

See [sandbox token server docs](https://docs.livekit.io/frontends/build/authentication/sandbox-token-server/).

### Custom

Integrate an existing auth system without adopting the standard endpoint wire
format:

```cpp
auto source = livekit::CustomTokenSource::fromCallback(
    [](const livekit::TokenRequestOptions& options)
        -> std::future<livekit::Result<livekit::TokenSourceResponse, livekit::TokenSourceError>> {
      std::promise<livekit::Result<livekit::TokenSourceResponse, livekit::TokenSourceError>> promise;
      livekit::TokenSourceResponse details;
      details.server_url = /* ... */;
      details.participant_token = /* encode options into your JWT ... */;
      promise.set_value(
          livekit::Result<livekit::TokenSourceResponse, livekit::TokenSourceError>::success(details));
      return promise.get_future();
    });
```

### Caching

Wraps another configurable source and reuses a cached JWT when options match
and the token is still valid. `force_refresh = true` bypasses the cache on the
next `fetch()` — useful when calling `Room::connect` again, **not** for
server-pushed refresh during an active session.

```cpp
auto inner = livekit::EndpointTokenSource::fromUrl("https://your-backend.example.com/token");
auto source = livekit::CachingTokenSource::wrap(std::move(inner));

if (!room.connect(*source, request, options)) {
  return 1;
}
```

## In-session token refresh

During an active session, the LiveKit server may push an updated JWT over the
signal connection so the client can reconnect if the original join token
expires. The Rust core stores and applies this token automatically.

C++ surfaces an optional notification:

```cpp
class MyDelegate : public livekit::RoomDelegate {
public:
  void onTokenRefreshed(livekit::Room&, const livekit::TokenRefreshedEvent& ev) override {
    // ev.token — latest JWT the SDK is using internally
  }
};
```

This event is **informational**. It does not invoke your token source or token
endpoint. Automatic reconnect/resume while connected uses the internally stored
JWT only.

See [token-lifecycle.md](token-lifecycle.md) for client vs. server
responsibilities, refresh timing, and how join `valid-for` relates to reconnect.
