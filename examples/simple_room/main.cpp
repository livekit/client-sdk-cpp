#include <cmath>
#include <cstdio>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

#include "livekit/livekit.h"
#include "room.pb.h"

using namespace livekit;

const std::string URL = "ws://localhost:7880";
const std::string TOKEN =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJleHAiOjE5MDY2MTMyODgsImlzcyI6IkFQSVRzRWZpZFpqclFvWSIsIm5hbWUiOiJuYXRpdm"
    "UiLCJuYmYiOjE2NzI2MTMyODgsInN1YiI6Im5hdGl2ZSIsInZpZGVvIjp7InJvb20iOiJ0ZXN0"
    "Iiwicm9vbUFkbWluIjp0cnVlLCJyb29tQ3JlYXRlIjp0cnVlLCJyb29tSm9pbiI6dHJ1ZSwicm"
    "9vbUxpc3QiOnRydWV9fQ.uSNIangMRu8jZD5mnRYoCHjcsQWCrJXgHCs0aNIgBFY";

std::vector<int> hsv_to_rgb(float H, float S, float V) {
  std::vector<int> rgb(3);
  float C = S * V;
  float X = C * (1 - std::abs(fmod(H * 6, 2) - 1));
  float m = V - C;

  float R, G, B;
  if (0 <= H && H < 1 / 6.0) {
    R = C, G = X, B = 0;
  } else if (1 / 6.0 <= H && H < 2 / 6.0) {
    R = X, G = C, B = 0;
  } else if (2 / 6.0 <= H && H < 3 / 6.0) {
    R = 0, G = C, B = X;
  } else if (3 / 6.0 <= H && H < 4 / 6.0) {
    R = 0, G = X, B = C;
  } else if (4 / 6.0 <= H && H < 5 / 6.0) {
    R = X, G = 0, B = C;
  } else {
    R = C, G = 0, B = X;
  }

  rgb[0] = (R + m) * 255;
  rgb[1] = (G + m) * 255;
  rgb[2] = (B + m) * 255;

  return rgb;
}

void publish_frames(VideoSource* source) {
  ArgbFrame frame(proto::FORMAT_ARGB, 1280, 720);
  double framerate = 1.0 / 30;
  double hue = 0.0;
  while (true) {
    std::vector<int> rgb = hsv_to_rgb(hue, 1.0, 1.0);
    for (int i = 0; i < frame.data.size(); i += 4) {
      frame.data[i] = 255;
      frame.data[i + 1] = rgb[0];
      frame.data[i + 2] = rgb[1];
      frame.data[i + 3] = rgb[2];
    }

    hue += framerate / 3;

    if (hue >= 1.0) {
      hue = 0.0;
    }

    VideoFrame i420(0, proto::VIDEO_ROTATION_0, frame.ToI420());
    source->CaptureFrame(i420);

    std::this_thread::sleep_for(
        std::chrono::milliseconds(int(framerate * 1000)));
  }
}

int main(int argc, char* argv[]) {
  std::shared_ptr<Room> room = std::make_shared<Room>();
  room->Connect(URL, TOKEN);

  ArgbFrame argbFrame(proto::FORMAT_ARGB, 1280, 720);
  VideoSource source{};

  std::shared_ptr<LocalVideoTrack> track =
      LocalVideoTrack::CreateVideoTrack("hue", source);

  // Create a new thread and call publish_freames
  std::thread t1(publish_frames, &source);

  proto::TrackPublishOptions options;
  options.set_source(proto::SOURCE_CAMERA);

  // TODO Non blocking ?
  while (!room->GetLocalParticipant()) {
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));

  std::cout << "Publishing track" << std::endl;
  room->GetLocalParticipant()->PublishTrack(track, options);

  // Should we implement a mechanism to PollEvents/WaitEvents? Like SDL2/glfw
  //   - So we can remove the useless loop here
  // Or is it better to use callback based events?

  while (true) {
  }

  return 0;
}
