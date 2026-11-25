# Agent dispatch

Use `livekit::api::AgentDispatchClient` to send a named agent into a room from your backend. You can also list, get, and delete dispatches. A dispatch is a server record that tells LiveKit to start an agent job in a room.

The client calls the [AgentDispatchService API](https://docs.livekit.io/reference/agents/agent-dispatch-service-api/) over HTTPS. It is not part of `livekit/livekit.h`. Include its own header:

```cpp
#include <livekit/api/agent_dispatch_client.h>
```

The agent must register with an agent name. Explicit dispatch does not work for an agent without a name.

## Create a client

Each call needs a token with the `roomAdmin` grant for the room. The client creates this token for each request from your API key and secret.

```cpp
using namespace livekit::api;

AgentDispatchClient client("https://my-project.livekit.cloud", api_key, api_secret);
```

To read the key and secret from `LIVEKIT_API_KEY` and `LIVEKIT_API_SECRET`, use `fromEnv`:

```cpp
auto client = AgentDispatchClient::fromEnv("https://my-project.livekit.cloud");
if (!client) {
  // client.error().kind == AgentDispatchError::Kind::InvalidEnvironment
}
```

If your process must not hold the API secret, use `AgentDispatchClient::withToken(host, token)`. The token must have the `roomAdmin` grant.

Keep the API secret on your server. Do not ship it in a client application.

## Dispatch an agent

```cpp
CreateAgentDispatchRequest req;
req.agent_name = "my-agent";
req.room = "my-room";
req.metadata = R"({"user_id": "42"})";

auto result = client.createDispatch(req).get();
if (result) {
  const AgentDispatch& dispatch = result.value();
  // dispatch.id identifies the dispatch for getDispatch and deleteDispatch.
} else {
  // result.error().kind, result.error().code, result.error().message
}
```

If the room does not exist, the server creates it.

## List, get, and delete

```cpp
auto all = client.listDispatch("my-room").get();
auto one = client.getDispatch(dispatch_id, "my-room").get();    // std::optional
auto removed = client.deleteDispatch(dispatch_id, "my-room").get();
```

If the room has no dispatch with that ID, `getDispatch` returns `std::nullopt`.

## Threads and blocking

Every call returns a `std::future` at once. The request runs in the Rust core, and no C++ thread waits on it. The `get()` call blocks until the result is available.

The client is safe to share between threads. Do not call `get()` inside a `RoomDelegate` callback, because that callback runs on the FFI callback thread.

## Errors

The calls do not throw. Each result holds an `AgentDispatchError` with one of these kinds:

| Kind | Cause |
|------|-------|
| `InvalidEnvironment` | `fromEnv` did not find `LIVEKIT_API_KEY` or `LIVEKIT_API_SECRET`. |
| `InvalidAccessToken` | The client did not sign the request token. |
| `Server` | The server returned an error. `code` holds the Twirp code, for example `not_found` or `unauthenticated`. |
| `Request` | The connection, TLS, timeout, or response decode failed. |

## Configuration

`AgentDispatchClientOptions` has two fields:

- `request_timeout`: the timeout of each HTTP request. The default is 10 seconds.
- `failover`: if true (the default), a failed request on LiveKit Cloud retries on another region.

## Same API in other SDKs

The method names match the Rust (`livekit_api`) and Python (`livekit.api`) server SDKs: `create_dispatch`, `delete_dispatch`, `list_dispatch`, and `get_dispatch`.
