# LiveKit Bridge

A simplified, high-level C++ wrapper around the [LiveKit C++ SDK](../README.md). The bridge abstracts away room lifecycle management, track creation, publishing, and subscription boilerplate so that external codebases can interface with LiveKit in just a few lines. It is intended that this library will be used to bridge the LiveKit C++ SDK into other SDKs such as, but not limited to, Foxglove, ROS, and Rerun.

It is intended that this library closely matches the style of the core LiveKit C++ SDK.

# Prerequisites
Since this is an extention of the LiveKit C++ SDK, go through the LiveKit C++ SDK installation instructions first:
*__[LiveKit C++ SDK](../README.md)__*

## Usage Overview

```cpp
#include "livekit_bridge/livekit_bridge.h"
#include "livekit/audio_frame.h"
#include "livekit/video_frame.h"
#include "livekit/track.h"

// 1. Connect
livekit_bridge::LiveKitBridge bridge;
livekit::RoomOptions options;
options.auto_subscribe = true; // automatically subscribe to all remote tracks
options.dynacast = false;
bridge.connect("wss://my-server.livekit.cloud", token, options);

// 2. Create outgoing tracks (RAII-managed)
auto mic = bridge.createAudioTrack("mic", 48000, 2,
    livekit::TrackSource::SOURCE_MICROPHONE);  // name, sample_rate, channels, source
auto cam = bridge.createVideoTrack("cam", 1280, 720,
    livekit::TrackSource::SOURCE_CAMERA);  // name, width, height, source

// 3. Send frames to remote participants (core SDK track types)
livekit::AudioFrame af(std::move(pcm_data), 48000, 2, samples_per_channel);
mic->captureFrame(af);
livekit::VideoFrame vf(1280, 720, livekit::VideoBufferType::RGBA, std::move(rgba_data));
cam->captureFrame(vf, timestamp_us, livekit::VideoRotation::VIDEO_ROTATION_0);

// 4. Receive frames from a remote participant
bridge.setOnAudioFrameCallback("remote-peer", livekit::TrackSource::SOURCE_MICROPHONE,
    [](const livekit::AudioFrame& frame) {
        // Called on a background reader thread
    });

bridge.setOnVideoFrameCallback("remote-peer", livekit::TrackSource::SOURCE_CAMERA,
    [](const livekit::VideoFrame& frame, int64_t timestamp_us) {
        // Called on a background reader thread
    });

// 5. RPC (Remote Procedure Call)
bridge.registerRpcMethod("greet",
    [](const livekit::RpcInvocationData& data) -> std::optional<std::string> {
        return "Hello, " + data.caller_identity + "!";
    });

std::string response = bridge.performRpc("remote-peer", "greet", "");

bridge.unregisterRpcMethod("greet");

//    Controller side: send commands to the publisher
controller_bridge.requestRemoteTrackMute("robot-1", "mic");    // mute audio track "mic"
controller_bridge.requestRemoteTrackUnmute("robot-1", "mic");  // unmute it

// 7. Cleanup: disconnect unpublishes tracks the bridge retained and tears down the room
bridge.disconnect();
```

## Building

The bridge is a component of the `client-sdk-cpp` build. See the "⚙️ BUILD" section of the [LiveKit C++ SDK README](../README.md) for instructions on how to build the bridge.

This produces `liblivekit_bridge` (shared library) and optional `robot_stub`, `human_stub`, `robot`, and `human` executables.

### Using the bridge in your own CMake project
TODO(sderosa): add instructions on how to use the bridge in your own CMake project.

## Architecture

### Data Flow Overview

```
Your Application
    |                                       |
    |  captureFrame() --> LocalAudioTrack   |  (sending to remote participants)
    |  captureFrame() --> LocalVideoTrack   |
    |                                       |
    |  callback() <------ Reader Thread     |  (receiving from remote participants)
    |                                       |
    +------- LiveKitBridge -----------------+
                    |
              LiveKit Room
                    |
              LiveKit Server
```

### Core Components

**`LiveKitBridge`** -- The main entry point. Owns the full room lifecycle: SDK initialization, room connection, track publishing, and frame callback management.

**`LocalAudioTrack`** -- From `createAudioTrack()`. Build a `livekit::AudioFrame` (interleaved int16 PCM, sample rate, channels, samples per channel) and call `captureFrame(frame, timeout_ms)` to send audio.

**`LocalVideoTrack`** -- From `createVideoTrack()`. Build a `livekit::VideoFrame` (dimensions, `VideoBufferType`, pixel bytes) and call `captureFrame(frame, timestamp_us, rotation)` to send video.

**`BridgeRoomDelegate`** -- Internal (not part of the public API; lives in `src/`). Listens for `onTrackSubscribed` / `onTrackUnsubscribed` events from the LiveKit SDK and wires up reader threads automatically.

### What is a Reader?

A **reader** is a background thread that receives decoded media frames from a remote participant.

When a remote participant publishes an audio or video track and the bridge subscribes to it (auto-subscribe is enabled by default), the bridge creates an `AudioStream` or `VideoStream` from that track and spins up a dedicated thread. This thread loops on `stream->read()`, which blocks until a new frame arrives. Each received frame is forwarded to the user's registered callback.

In short:

- **Sending** (you -> remote): `LocalAudioTrack::captureFrame()` / `LocalVideoTrack::captureFrame()`
- **Receiving** (remote -> you): reader threads invoke your registered callbacks

Reader threads are managed entirely by the bridge. They are created when a matching remote track is subscribed, and torn down (stream closed, thread joined) when the track is unsubscribed, the callback is unregistered, or `disconnect()` is called.

### Callback Registration Timing

Callbacks are keyed by `(participant_identity, track_source)`. You can register them **before** the remote participant has joined the room. The bridge stores the callback and automatically wires it up when the matching track is subscribed.

> **Note:** Only one callback may be set per `(participant_identity, track_source)` pair. Calling `setOnAudioFrameCallback` or `setOnVideoFrameCallback` again with the same identity and source will silently replace the previous callback. If you need to fan-out a single stream to multiple consumers, do so inside your callback.

This means the typical pattern is:

```cpp
// Register first, connect second -- or register after connect but before
// the remote participant joins.
bridge.setOnAudioFrameCallback("robot-1", livekit::TrackSource::SOURCE_MICROPHONE, my_callback);
livekit::RoomOptions options;
options.auto_subscribe = true;
bridge.connect(url, token, options);
// When robot-1 joins and publishes a mic track, my_callback starts firing.
```

### Thread Safety

- `LiveKitBridge` uses a mutex to protect the callback map and active reader state.
- Frame callbacks fire on background reader threads. If your callback accesses shared application state, you are responsible for synchronization.
- `disconnect()` closes all streams and joins all reader threads before returning -- it is safe to destroy the bridge immediately after.

## API Reference

### `LiveKitBridge`

| Method | Description |
|---|---|
| `connect(url, token, options)` | Connect to a LiveKit room. Initializes the SDK, creates a Room, and connects with auto-subscribe enabled. |
| `disconnect()` | Disconnect and release all resources. Joins all reader threads. Safe to call multiple times. |
| `isConnected()` | Returns whether the bridge is currently connected. |
| `createAudioTrack(name, sample_rate, num_channels, source)` | Create and publish a local audio track with the given `TrackSource` (e.g. `SOURCE_MICROPHONE`, `SOURCE_SCREENSHARE_AUDIO`). Returns an RAII `shared_ptr<BridgeAudioTrack>`. |
| `createVideoTrack(name, width, height, source)` | Create and publish a local video track with the given `TrackSource` (e.g. `SOURCE_CAMERA`, `SOURCE_SCREENSHARE`). Returns `shared_ptr<livekit::LocalVideoTrack>`. |
| `setOnAudioFrameCallback(identity, source, callback)` | Register a callback for audio frames from a specific remote participant + track source. |
| `setOnVideoFrameCallback(identity, source, callback)` | Register a callback for video frames from a specific remote participant + track source. |
| `clearOnAudioFrameCallback(identity, source)` | Clear the audio callback for a specific remote participant + track source. Stops and joins the reader thread if active. |
| `clearOnVideoFrameCallback(identity, source)` | Clear the video callback for a specific remote participant + track source. Stops and joins the reader thread if active. |
| `performRpc(destination_identity, method, payload, response_timeout?)` | Blocking RPC call to a remote participant. Returns the response payload. Throws `livekit::RpcError` on failure. |
| `registerRpcMethod(method_name, handler)` | Register a handler for incoming RPC invocations. The handler returns an optional response payload or throws `livekit::RpcError`. |
| `unregisterRpcMethod(method_name)` | Unregister a previously registered RPC handler. |
| `requestRemoteTrackMute(identity, track_name)` | Ask a remote participant to mute a track by name. Throws `livekit::RpcError` on failure. |
| `requestRemoteTrackUnmute(identity, track_name)` | Ask a remote participant to unmute a track by name. Throws `livekit::RpcError` on failure. |

### `LocalAudioTrack` / `LocalVideoTrack` (outgoing)

Returned by `createAudioTrack()` / `createVideoTrack()`. These are the same types as the core LiveKit C++ SDK: send media with `captureFrame(...)`, control mute with `mute()` / `unmute()`, and read layout from `audioSource()` / `videoSource()` when building frames.

## Examples
- examples/robot.cpp: publishes video and audio from a webcam and microphone. This requires a webcam and microphone to be available.
- examples/human.cpp: receives and renders video to the screen, receives and plays audio through the speaker.

### Running the examples:
Note: the following workflow works for both `human` and `robot`.

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
./build-release/bin/human
```

The human will print periodic summaries like:

```
[human] Audio frame #1: 480 samples/ch, 48000 Hz, 1 ch, duration=0.010s
[human] Video frame #1: 640x480, 1228800 bytes, ts=0 us
[human] Status: 500 audio frames, 150 video frames received so far.
```

## Testing

The bridge includes a unit test suite built with [Google Test](https://github.com/google/googletest). Tests cover
1. `CallbackKey` hashing/equality,
2. `LocalAudioTrack` / `LocalVideoTrack` retention until `disconnect()`, and
3. `LiveKitBridge` pre-connection behaviour (callback registration, error handling).

### Building and running tests

Bridge tests are automatically included when you build with the `debug-tests` or `release-tests` command:

```bash
./build.sh debug-tests
```

Then run them directly:

```bash
./build-debug/bin/livekit_bridge_tests
```

### Standalone bridge tests only

If you want to build bridge tests independently (without the parent SDK tests), set `LIVEKIT_BRIDGE_BUILD_TESTS=ON`:

```bash
cmake --preset macos-debug -DLIVEKIT_BRIDGE_BUILD_TESTS=ON
cmake --build build-debug --target livekit_bridge_tests
```

## Limitations

The bridge is designed for simplicity and currently only supports limited audio and video features. It does not expose:

- We dont support all events defined in the RoomDelegate interface.
- E2EE configuration
- data tracks
- Simulcast tuning
- Video format selection (RGBA is the default; no format option yet)
- Custom `RoomOptions` or `TrackPublishOptions`
- **One callback per (participant, source):** Only a single callback can be registered for each `(participant_identity, track_source)` pair. Re-registering with the same key silently replaces the previous callback. To fan-out a stream to multiple consumers, dispatch from within your single callback.

For advanced use cases, use the full `client-sdk-cpp` API directly, or expand the bridge to support your use case.
