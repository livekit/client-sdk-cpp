# TokenSource SDK API Comparison

This report compares the C++ branch's public TokenSource API against the LiveKit
client SDKs that expose comparable public TokenSource surfaces. The closest
matches are Android, Swift, Flutter, and JS.

## Summary

C++ is conceptually aligned with the newer TokenSource work across LiveKit SDKs:
it has fixed and configurable token source categories, endpoint/sandbox/custom
implementations, request options for room/participant/agent fields, and
JWT-aware caching.

After the API alignment pass, the main remaining differences are:

- C++ uses concrete constructors, while Android centralizes factory methods on
  `TokenSource` and JS centralizes them on a `TokenSource` object.
- C++ uses snake_case public fields, while Swift/Android/Flutter/JS use the
  language-native field naming conventions.

## SDK Coverage

| SDK | TokenSource Shape | Factory Pattern? | Notes |
|---|---|---:|---|
| C++ branch | `TokenSourceFixed`, `TokenSourceConfigurable`, concrete classes | No | Direct constructors on concrete sources |
| Android | `TokenSource` companion factories returning `FixedTokenSource` / `ConfigurableTokenSource` | Yes | Closest factory naming precedent |
| JS | `TokenSource.literal/custom/endpoint/sandboxTokenServer` object factories | Yes | Closest conceptual taxonomy |
| Swift | Protocols plus concrete `LiteralTokenSource` / `SandboxTokenSource`; `EndpointTokenSource` is a protocol with default implementation | No | Closest protocol/native shape |
| Flutter | Abstract fixed/configurable classes plus concrete constructors | No | Very close type/class shape |
| Unity | `ITokenSourceFixed`, `ITokenSourceConfigurable`, direct constructors, and `TokenSourceComponentConfig` | Partial | Similar concepts, Unity-specific component/config surface |
| React Native | No native TokenSource surface found; component takes `serverUrl` + `token` | No | Uses URL + token directly |
| ESP32 / Unreal / Node | No first-class TokenSource surface found | No | N/A |
| Swift XCFramework / Unity Web / benchmarks / Expo plugin | No separate public TokenSource surface found | No | Packaging/support repos |

## Public Signature Comparison

| Concept | C++ Branch | Android | JS | Swift | Flutter |
|---|---|---|---|---|---|
| Fixed interface | `class TokenSourceFixed` | `interface FixedTokenSource` | `abstract class TokenSourceFixed` | `protocol TokenSourceFixed` | `abstract class TokenSourceFixed` |
| Configurable interface | `class TokenSourceConfigurable` | `interface ConfigurableTokenSource` | `abstract class TokenSourceConfigurable` | `protocol TokenSourceConfigurable` | `abstract class TokenSourceConfigurable` |
| Fixed fetch | `fetch()` | `fetch()` | `fetch()` | `fetch()` | `fetch()` |
| Config fetch | `fetch(const TokenRequestOptions& options = {})` | `fetch(options = TokenRequestOptions())` | `fetch(options, force?)` | `fetch(_ options)` | `fetch(options)` |
| Response | `server_url`, `participant_token`, `room_name?`, `participant_name?` | `serverUrl`, `participantToken`, `roomName?`, `participantName?` | `serverUrl`, `participantToken`, optional response fields | `serverURL`, `participantToken`, `participantName?`, `roomName?` | `serverUrl`, `participantToken`, `participantName?`, `roomName?` |
| Request options | `room_name`, `participant_name`, `participant_identity`, `participant_metadata`, `participant_attributes`, `agent_name`, `agent_metadata`, `agent_deployment` | `roomName`, `participantName`, `participantIdentity`, `participantMetadata`, `participantAttributes`, `agentName`, `agentMetadata`, `agentDeployment` | `roomName`, `participantName`, `participantIdentity`, `participantMetadata`, `participantAttributes`, `agentName`, `agentMetadata`, `deployment` | same as Android | same as Android |

## Factory And Construction Comparison

| Mechanism | C++ Branch | Android | JS | Swift | Flutter | Unity |
|---|---|---|---|---|---|---|
| Literal static credentials | `LiteralTokenSource(url, token)` | `TokenSource.fromLiteral(url, token)` | `TokenSource.literal(valueOrFn)` | `LiteralTokenSource(serverURL:participantToken:)` | `LiteralTokenSource(serverUrl:participantToken:)` | `new TokenSourceLiteral(serverUrl, token)` |
| Literal async provider | `LiteralTokenSource(fn)` | Not found | `TokenSource.literal(async () => ...)` | Implement `TokenSourceFixed` | Implement `TokenSourceFixed` | `TokenSourceCustom` is fixed |
| Custom configurable | `CustomTokenSource(fn)` | `TokenSource.fromCustom(block)` | `TokenSource.custom(fn)` | Implement `TokenSourceConfigurable` | `CustomTokenSource(fn)` | Not configurable; `TokenSourceCustom` is fixed |
| Endpoint | `EndpointTokenSource(url, options)` | `TokenSource.fromEndpoint(url, method, headers)` | `TokenSource.endpoint(url, options)` | conform to `EndpointTokenSource` protocol | `EndpointTokenSource(url:, method:, headers:)` | `new TokenSourceEndpoint(url, headers)` |
| Sandbox | `SandboxTokenSource(id, options)` | `TokenSource.fromSandboxTokenServer(id, options)` | `TokenSource.sandboxTokenServer(id, options)` | `SandboxTokenSource(id:)` | `SandboxTokenSource(sandboxId:)` | `new TokenSourceSandbox(sandboxId)` |
| Caching | `CachingTokenSource(inner)` | `source.cached(...)` | built into configurable sources; `fetch(..., force?)` | `source.cached(...)` | `source.cached(...)` | Not found in public TokenSource surface |

## TokenSource And Connect

| SDK | Core Room Connect Shape | TokenSource Relationship |
|---|---|---|
| C++ branch | `Room::connect(url, token, options)` | User fetches credentials, then passes URL + token to `connect` |
| JS | `room.connect(url, token, opts?)` | User fetches credentials, then passes URL + token to `connect` |
| Android | `room.connect(url, token, options)` | User fetches credentials, then passes URL + token to `connect` |
| Swift | `room.connect(url:token:connectOptions:roomOptions:)` | Core `Room` takes URL + token. `Session` accepts TokenSource, fetches, then calls `Room.connect` |
| Flutter | `room.connect(url, token, connectOptions:, roomOptions:)` | Core `Room` takes URL + token. `Session` accepts TokenSource, fetches, then calls `Room.connect` |
| Unity | `Room.Connect(serverUrl, participantToken)` | `TokenSourceComponent` or direct source fetch returns connection details before connect |
| React Native | `LiveKitRoom` props include `serverUrl` and `token` | No first-class TokenSource wrapper found |

## Findings

### Closest Matches

Android is the closest API naming precedent if C++ uses factory methods.
Android uses central `TokenSource.fromLiteral`, `fromCustom`, `fromEndpoint`,
and `fromSandboxTokenServer` factories; C++ now uses constructors on the
concrete source classes instead.

Swift and Flutter are closest in type shape: both distinguish fixed and
configurable token sources and use concrete `LiteralTokenSource`,
`SandboxTokenSource`, `EndpointTokenSource`, and `CachingTokenSource` concepts.
They do not centralize creation behind a factory object.

JS is closest in user-facing taxonomy: `literal`, `custom`, `endpoint`, and
`sandboxTokenServer`.

### Remaining Naming Differences

The most visible remaining C++ construction differences are:

- C++ constructs concrete sources directly, while Android and JS centralize
  creation under `TokenSource`.
- C++ uses `LiteralTokenSource(fn)` for async fixed providers, conceptually close
  to JS `TokenSource.literal(async () => ...)`.
- C++ uses `CachingTokenSource(inner)`, while Swift/Android/Flutter expose
  `.cached(...)`.

### Connect Design

C++ now follows the common core Room pattern:

1. Fetch credentials from a token source.
2. Pass `serverUrl` and `participantToken` to `Room.connect`.

### Response Shape

C++ now matches the other SDK response shape conceptually:

- `server_url`
- `participant_token`
- `room_name`
- `participant_name`

Only `server_url` and `participant_token` are needed for connect. The optional
room and participant name fields are preserved as informational response
metadata when a token server provides them.

## Conclusion

The largest uniqueness has been removed: C++ no longer exposes TokenSource
`Room::connect` overloads. C++ now favors direct constructors for concrete
TokenSource types; the remaining alignment opportunity is centralizing factories
under a `TokenSource` facade if cross-SDK discoverability becomes more important
than C++-native construction.
