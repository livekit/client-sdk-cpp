# Mute/Unmute Example

Demonstrates remote track control using the `SessionManager` built-in
track-control RPC. A **receiver** publishes audio and video tracks, and a
**caller** subscribes to them and toggles mute/unmute every few seconds.

## How it works

| Executable            | Role |
|-----------------------|------|
| **MuteUnmuteReceiver** | Publishes an audio track (`"mic"`) and a video track (`"cam"`) using SDL3 hardware capture when available, falling back to silence and solid-color frames otherwise. The SessionManager automatically registers a built-in `lk.session_manager.track-control` RPC handler on connect. |
| **MuteUnmuteCaller**   | Subscribes to the receiver's mic and cam tracks, renders them via SDL3 (speaker + window), and periodically calls `requestRemoteTrackMute` / `requestRemoteTrackUnmute` to toggle both tracks. |

When the caller mutes a track, the receiver's `LocalAudioTrack::mute()` or
`LocalVideoTrack::mute()` is invoked via RPC, which signals the LiveKit
server to stop forwarding that track's media. The caller's audio goes
silent and the video freezes on the last received frame. On unmute, media
delivery resumes.

## Running

Generate two tokens for the same room with different identities:

```bash
lk token create --join --room my-room --identity receiver --valid-for 24h
lk token create --join --room my-room --identity caller   --valid-for 24h
```

Start the receiver first, then the caller:

```bash
# Terminal 1
LIVEKIT_URL=wss://... LIVEKIT_TOKEN=<receiver-token> ./build-release/bin/MuteUnmuteReceiver

# Terminal 2
LIVEKIT_URL=wss://... LIVEKIT_TOKEN=<caller-token>   ./build-release/bin/MuteUnmuteCaller
```

The caller also accepts an optional third argument for the receiver's
identity (defaults to `"receiver"`):

```bash
./build-release/bin/MuteUnmuteCaller wss://... <token> my-receiver
```

## Sample output

### Receiver

```
./build-release/bin/MuteUnmuteReceiver
[receiver] Connecting to wss://sderosasandbox-15g80zq7.livekit.cloud ...
[receiver] Connected.
cs.state() is 1 connection_state_ is 1
[receiver] Published audio track "mic" and video track "cam".
[receiver] Waiting for remote mute/unmute commands...
[receiver] Using SDL microphone.
[receiver] Using SDL camera.
[receiver] Press Ctrl-C to stop.
[RpcController] Handling track control RPC: mute:mic
[RpcController] Handling track control RPC: mute:cam
[RpcController] Handling track control RPC: unmute:mic
[RpcController] Handling track control RPC: unmute:cam
```

### Caller

```
./build-release/bin/MuteUnmuteCaller
[caller] Connecting to wss://sderosasandbox-15g80zq7.livekit.cloud ...
cs.state() is 1 connection_state_ is 1
[caller] Connected.
[caller] Target receiver identity: "receiver"
[caller] Subscribed to receiver's mic + cam.
[caller] Rendering receiver feed. Toggling mute every 5s. Close window or Ctrl-C to stop.
[caller] Speaker opened: 48000 Hz, 1 ch.

[caller] --- Cycle 1: MUTE ---
[caller]   mic: muted OK
[caller]   cam: muted OK

[caller] --- Cycle 2: UNMUTE ---
[caller]   mic: unmuted OK
[caller]   cam: unmuted OK
```

## Notes

- The receiver uses SDL3 for microphone and camera capture. On macOS you
  may need to grant camera/microphone permissions.
- If no hardware is detected, the receiver falls back to sending silence
  (audio) and alternating solid-color frames (video).
- The caller opens an SDL3 window to render the received video and plays
  audio through the default speaker.
