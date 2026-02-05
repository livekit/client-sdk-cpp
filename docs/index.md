# LiveKit C++ Client SDK

Build real-time audio/video applications in C++ with LiveKit.

> **Note:** This SDK is currently in Developer Preview. APIs may change before the stable release.

## Quick Start

```cpp
#include "livekit/livekit.h"

bool initializeLivekit(const std::string& url, const std::string& token) {
  // Init LiveKit
  livekit::initialize(livekit::LogSink::kConsole);

  room_ = std::make_unique<livekit::Room>();
  livekit::RoomOptions options;
  options.auto_subscribe = true;
  options.dynacast = false;
  if (!room_->Connect(url, token, options)) {
    std::cerr << "Failed to connect\n";
    livekit::shutdown();
    return false;
  }

  std::cout << "Connected.\n";

  // ---- Create & publish AUDIO ----
  // Note: Hook up your own audio capture flow to |audioSource_|
  audioSource_ = std::make_shared<livekit::AudioSource>(48000, 1, 10);
  auto audioTrack = livekit::LocalAudioTrack::createLocalAudioTrack("noise", audioSource_);

  livekit::TrackPublishOptions audioOpts;
  audioOpts.source = livekit::TrackSource::SOURCE_MICROPHONE;

  try {
    audioPub_ = room_->localParticipant()->publishTrack(audioTrack, audioOpts);
    std::cout << "Published audio: sid=" << audioPub_->sid() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "Failed to publish audio: " << e.what() << "\n";
    return false;
  }

  // ---- Create & publish VIDEO ----
  // Note: Hook up your own video capture flow to |videoSource_|
  videoSource_ = std::make_shared<livekit::VideoSource>(1280, 720);
  auto videoTrack = livekit::LocalVideoTrack::createLocalVideoTrack("rgb", videoSource_);

  livekit::TrackPublishOptions videoOpts;
  videoOpts.source = livekit::TrackSource::SOURCE_CAMERA;

  try {
    videoPub_ = room_->localParticipant()->publishTrack(videoTrack, videoOpts);
    std::cout << "Published video: sid=" << videoPub_->sid() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "Failed to publish video: " << e.what() << "\n";
    return false;
  }
  return true;
}

void shutdownLivekit() {
  // Best-effort unpublish
  try {
    if (room_ && audioPub_)
      room_->localParticipant()->unpublishTrack(audioPub_->sid());
    if (room_ && videoPub_)
      room_->localParticipant()->unpublishTrack(videoPub_->sid());
  } catch (...) {
  }

  audioPub_.reset();
  videoPub_.reset();
  audioSource_.reset();
  videoSource_.reset();
  room_.reset();

  livekit::shutdown();
}
```

## Key Classes

| Class | Description |
|-------|-------------|
| @ref livekit::Room | Main entry point - connect to a LiveKit room |
| @ref livekit::RoomOptions | Configuration for room connection (auto_subscribe, dynacast, etc.) |
| @ref livekit::LocalParticipant | The local user - publish tracks and send data |
| @ref livekit::RemoteParticipant | Other participants in the room |
| @ref livekit::AudioSource | Audio input source for publishing (sample rate, channels) |
| @ref livekit::VideoSource | Video input source for publishing (width, height) |
| @ref livekit::LocalAudioTrack | Local audio track created from AudioSource |
| @ref livekit::LocalVideoTrack | Local video track created from VideoSource |
| @ref livekit::LocalTrackPublication | Handle to a published local track |
| @ref livekit::TrackPublishOptions | Options for publishing (source type, codec, etc.) |
| @ref livekit::AudioStream | Receive audio from remote participants |
| @ref livekit::VideoStream | Receive video from remote participants |
| @ref livekit::RoomDelegate | Callbacks for room events |

## Installation

See the [GitHub README](https://github.com/livekit/client-sdk-cpp#readme) for build instructions.

**Requirements:**
- CMake ≥ 3.20
- Rust/Cargo (latest stable)
- Platform: Windows, macOS, or Linux

## Examples

- [SimpleRoom](https://github.com/livekit/client-sdk-cpp/tree/main/examples/simple_room) - Basic room connection with audio/video
- [SimpleRpc](https://github.com/livekit/client-sdk-cpp/tree/main/examples/simple_rpc) - Remote procedure calls between participants
- [SimpleDataStream](https://github.com/livekit/client-sdk-cpp/tree/main/examples/simple_data_stream) - Send text and binary data streams

## Resources

- [GitHub Repository](https://github.com/livekit/client-sdk-cpp)
- [LiveKit Documentation](https://docs.livekit.io/)
- [Community Slack](https://livekit.io/join-slack)