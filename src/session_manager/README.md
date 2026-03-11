# LiveKit SessionManager

A simplified, high-level C++ wrapper around the [LiveKit C++ SDK](../README.md).
The SessionManager abstracts away room lifecycle management, track creation, publishing, and subscription boilerplate so interfacing with LiveKit is just a few lines of code.

## Usage Overview

```cpp
#include "livekit/session_manager/session_manager.h"
#include "livekit/audio_frame.h"
#include "livekit/video_frame.h"
#include "livekit/track.h"

// 1. Connect
livekit::SessionManager sm;
livekit::RoomOptions options;
options.auto_subscribe = true; // automatically subscribe to all remote tracks
options.dynacast = false;
sm.connect("wss://my-server.livekit.cloud", token, options);

// 2. Create outgoing tracks (RAII-managed)
auto mic = sm.createAudioTrack("mic", 48000, 2,
    livekit::TrackSource::SOURCE_MICROPHONE);  // name, sample_rate, channels, source
auto cam = sm.createVideoTrack("cam", 1280, 720,
    livekit::TrackSource::SOURCE_CAMERA);  // name, width, height, source

// 3. Push frames to remote participants
mic->pushFrame(pcm_data, samples_per_channel);
cam->pushFrame(rgba_data, timestamp_us);

// 4. Receive frames from a remote participant
sm.setOnAudioFrameCallback("remote-peer", livekit::TrackSource::SOURCE_MICROPHONE,
    [](const livekit::AudioFrame& frame) {
        // Called on a background reader thread
    });

sm.setOnVideoFrameCallback("remote-peer", livekit::TrackSource::SOURCE_CAMERA,
    [](const livekit::VideoFrame& frame, int64_t timestamp_us) {
        // Called on a background reader thread
    });

// 5. RPC (Remote Procedure Call)
sm.registerRpcMethod("greet",
    [](const livekit::RpcInvocationData& data) -> std::optional<std::string> {
        return "Hello, " + data.caller_identity + "!";
    });

std::string response = sm.performRpc("remote-peer", "greet", "");

sm.unregisterRpcMethod("greet");

//    Controller side: send commands to the publisher
controller_bridge.requestRemoteTrackMute("robot-1", "mic");    // mute audio track "mic"
controller_bridge.requestRemoteTrackUnmute("robot-1", "mic");  // unmute it

// 7. Cleanup is automatic (RAII), or explicit:
mic.reset();       // unpublishes the audio track
cam.reset();       // unpublishes the video track
sm.disconnect();
```

### Accessing the Base SDK via getRoom()

The SessionManager owns the underlying `livekit::Room` internally. Use `getRoom()` to access base SDK features while the SessionManager manages the connection lifecycle:

```cpp
if (auto* room = sm.getRoom()) {
  auto info = room->room_info();
  // ... use info.sid, info.name, info.num_participants, etc.

  for (const auto& rp : room->remoteParticipants()) {
    // ... enumerate remote participants
  }

  room->registerTextStreamHandler("topic", [](auto reader, auto identity) {
    // ... handle incoming text streams
  });
}
```

The returned pointer is valid only while `isConnected()` is true. Do not call `setDelegate()` on the returned Room — SessionManager manages the delegate. For publishing audio/video tracks, prefer `createAudioTrack()` and `createVideoTrack()` so SessionManager can manage track lifecycle on disconnect.

## Architecture

### Data Flow Overview

```
Your Application
    |                                       |
    |  pushFrame() -----> ManagedLocalAudioTrack |  (sending to remote participants)
    |  pushFrame() -----> ManagedLocalVideoTrack |
    |                                       |
    |  callback() <------ Reader Thread     |  (receiving from remote participants)
    |                                       |
    +------- SessionManager ----------------+
                    |
              LiveKit Room
                    |
              LiveKit Server
```

### Core Components

**`SessionManager`** -- The main entry point. Owns the full room lifecycle: SDK initialization, room connection, track publishing, and frame callback management.

**`ManagedLocalAudioTrack` / `ManagedLocalVideoTrack`** -- RAII handles for published local tracks. Created via `createAudioTrack()` / `createVideoTrack()`. When the `shared_ptr` is dropped, the track is automatically unpublished and all underlying SDK resources are freed. Call `pushFrame()` to send audio/video data to remote participants.

**`SessionManagerRoomDelegate`** -- Internal (not part of the public API; lives in `src/`). Listens for `onTrackSubscribed` / `onTrackUnsubscribed` events from the LiveKit SDK and wires up reader threads automatically.

### What is a Reader?

A **reader** is a background thread that receives decoded media frames from a remote participant.

When a remote participant publishes an audio or video track and the SessionManager subscribes to it (auto-subscribe is enabled by default), the SessionManager creates an `AudioStream` or `VideoStream` from that track and spins up a dedicated thread. This thread loops on `stream->read()`, which blocks until a new frame arrives. Each received frame is forwarded to the user's registered callback.

In short:

- **Sending** (you -> remote): `ManagedLocalAudioTrack::pushFrame()` / `ManagedLocalVideoTrack::pushFrame()`
- **Receiving** (remote -> you): reader threads invoke your registered callbacks

Reader threads are managed entirely by the SessionManager. They are created when a matching remote track is subscribed, and torn down (stream closed, thread joined) when the track is unsubscribed, the callback is unregistered, or `disconnect()` is called.

### Callback Registration Timing

Callbacks are keyed by `(participant_identity, track_source)`. You can register them **before** the remote participant has joined the room. The SessionManager stores the callback and automatically wires it up when the matching track is subscribed.

> **Note:** Only one callback may be set per `(participant_identity, track_source)` pair. Calling `setOnAudioFrameCallback` or `setOnVideoFrameCallback` again with the same identity and source will silently replace the previous callback. If you need to fan-out a single stream to multiple consumers, do so inside your callback.

This means the typical pattern is:

```cpp
// Register first, connect second -- or register after connect but before
// the remote participant joins.
sm.setOnAudioFrameCallback("robot-1", livekit::TrackSource::SOURCE_MICROPHONE, my_callback);
livekit::RoomOptions options;
options.auto_subscribe = true;
sm.connect(url, token, options);
// When robot-1 joins and publishes a mic track, my_callback starts firing.
```

### Thread Safety

- `SessionManager` uses a mutex to protect the callback map and active reader state.
- Frame callbacks fire on background reader threads. If your callback accesses shared application state, you are responsible for synchronization.
- `disconnect()` closes all streams and joins all reader threads before returning -- it is safe to destroy the SessionManager immediately after.

## API Reference

### `SessionManager`

| Method | Description |
|---|---|
| `connect(url, token, options)` | Connect to a LiveKit room. Initializes the SDK, creates a Room, and connects with auto-subscribe enabled. |
| `disconnect()` | Disconnect and release all resources. Joins all reader threads. Safe to call multiple times. |
| `isConnected()` | Returns whether the SessionManager is currently connected. |
| `getRoom()` | Returns a raw pointer to the underlying `livekit::Room` for direct base SDK access (e.g. `room_info()`, `remoteParticipants()`, `registerTextStreamHandler`). Returns `nullptr` when disconnected. Do not call `setDelegate()` on the returned Room. |
| `createAudioTrack(name, sample_rate, num_channels, source)` | Create and publish a local audio track with the given `TrackSource` (e.g. `SOURCE_MICROPHONE`, `SOURCE_SCREENSHARE_AUDIO`). Returns an RAII `shared_ptr<ManagedLocalAudioTrack>`. |
| `createVideoTrack(name, width, height, source)` | Create and publish a local video track with the given `TrackSource` (e.g. `SOURCE_CAMERA`, `SOURCE_SCREENSHARE`). Returns an RAII `shared_ptr<ManagedLocalVideoTrack>`. |
| `setOnAudioFrameCallback(identity, source, callback)` | Register a callback for audio frames from a specific remote participant + track source. |
| `setOnVideoFrameCallback(identity, source, callback)` | Register a callback for video frames from a specific remote participant + track source. |
| `clearOnAudioFrameCallback(identity, source)` | Clear the audio callback for a specific remote participant + track source. Stops and joins the reader thread if active. |
| `clearOnVideoFrameCallback(identity, source)` | Clear the video callback for a specific remote participant + track source. Stops and joins the reader thread if active. |
| `performRpc(destination_identity, method, payload, response_timeout?)` | Blocking RPC call to a remote participant. Returns the response payload. Throws `livekit::RpcError` on failure. |
| `registerRpcMethod(method_name, handler)` | Register a handler for incoming RPC invocations. The handler returns an optional response payload or throws `livekit::RpcError`. |
| `unregisterRpcMethod(method_name)` | Unregister a previously registered RPC handler. |
| `requestRemoteTrackMute(identity, track_name)` | Ask a remote participant to mute a track by name. Throws `livekit::RpcError` on failure. |
| `requestRemoteTrackUnmute(identity, track_name)` | Ask a remote participant to unmute a track by name. Throws `livekit::RpcError` on failure. |

### `ManagedLocalAudioTrack`

| Method | Description |
|---|---|
| `pushFrame(data, samples_per_channel, timeout_ms)` | Push interleaved int16 PCM samples. Accepts `std::vector<int16_t>` or raw pointer. |
| `mute()` / `unmute()` | Mute/unmute the track (stops/resumes sending audio). |
| `release()` | Explicitly unpublish and free resources. Called automatically by the destructor. |
| `name()` / `sampleRate()` / `numChannels()` | Accessors for track configuration. |

### `ManagedLocalVideoTrack`

| Method | Description |
|---|---|
| `pushFrame(data, timestamp_us)` | Push RGBA pixel data. Accepts `std::vector<uint8_t>` or raw pointer + size. |
| `mute()` / `unmute()` | Mute/unmute the track (stops/resumes sending video). |
| `release()` | Explicitly unpublish and free resources. Called automatically by the destructor. |
| `name()` / `width()` / `height()` | Accessors for track configuration. |

## Running the examples

```
export LIVEKIT_URL="wss://your-server.livekit.cloud"
export LIVEKIT_TOKEN=<token>
./build-release/bin/<executables>
```
(Or pass `<ws-url> <token>` as positional arguments.)

1. create a `robo_room`
```
lk token create \                                                                            
  --join --room robo_room --identity test_user \
  --valid-for 24h
```

2. generate tokens for the robot and human
```
lk token create --api-key <key> --api-secret <secret> \
    --join --room robo_room --identity robot --valid-for 24h

lk token create --api-key <key> --api-secret <secret> \
    --join --room robo_room --identity human --valid-for 24h
```

save these tokens as you will need them to run the examples.

3. kick off the robot:
```
export LIVEKIT_URL="wss://your-server.livekit.cloud"
export LIVEKIT_TOKEN=<token>
./build-release/bin/robot_stub
```

4. kick off the human (in a new terminal):
```
export LIVEKIT_URL="wss://your-server.livekit.cloud"
export LIVEKIT_TOKEN=<token>
./build-release/bin/HumanRobotHuman
```

The human will print periodic summaries like:

```
[human] Audio frame #1: 480 samples/ch, 48000 Hz, 1 ch, duration=0.010s
[human] Video frame #1: 640x480, 1228800 bytes, ts=0 us
[human] Status: 500 audio frames, 150 video frames received so far.
```

## Testing

The SessionManager includes a unit test suite built with [Google Test](https://github.com/google/googletest). Tests cover
1. `CallbackKey` hashing/equality,
2. `ManagedLocalAudioTrack`/`ManagedLocalVideoTrack` state management, and
3. `SessionManager` pre-connection behaviour (callback registration, error handling).

### Building and running tests

SessionManager tests are automatically included when you build with the `debug-tests` or `release-tests` command:

```bash
./build.sh debug-tests
```

Then run them directly:

```bash
./build-debug/bin/session_manager_tests
```

### Standalone SessionManager tests only

If you want to build SessionManager tests independently (without the parent SDK tests), set `SESSION_MANAGER_BUILD_TESTS=ON`:

```bash
cmake --preset macos-debug -DSESSION_MANAGER_BUILD_TESTS=ON
cmake --build build-debug --target session_manager_tests
```

## Limitations

The SessionManager is designed for simplicity and currently only supports limited audio and video features. It does not expose:

- We dont support all events defined in the RoomDelegate interface.
- E2EE configuration
- data tracks
- Simulcast tuning
- Video format selection (RGBA is the default; no format option yet)
- Custom `RoomOptions` or `TrackPublishOptions`
- **One callback per (participant, source):** Only a single callback can be registered for each `(participant_identity, track_source)` pair. Re-registering with the same key silently replaces the previous callback. To fan-out a stream to multiple consumers, dispatch from within your single callback.

For advanced use cases, use the full `client-sdk-cpp` API directly, or expand the SessionManager to support your use case.
