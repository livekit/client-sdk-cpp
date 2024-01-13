/*
 * Copyright 2023 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the “License”);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cmath>
#include <cstdio>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

#include "livekit/livekit.h"
#include "room.pb.h"
#include "track.pb.h"

using namespace livekit;

const std::string URL = "ws://localhost:7880";
const std::string TOKEN =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE2OTk3MjM1MzQsImlzcyI6ImRldmtleSIsIm5hbWUiOiJIZW5nc3RhciIsIm5iZiI6MTY5MTA4MzUzNCwic3ViIjoiSGVuZ3N0YXIiLCJ2aWRlbyI6eyJyb29tIjoidGVzdFJvb20iLCJyb29tSm9pbiI6dHJ1ZX19.8y6Qr_Z9UNo3P0QPD_TuXai02s23n8o5GjyWXVHK2RM";

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
    for (size_t i = 0; i < frame.size; i += 4) {
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

int main() {
  std::shared_ptr<Room> room = Room::Create();
  room->Connect(URL, TOKEN);

  // TODO Non blocking ?
  while (!room->GetLocalParticipant()) {
  }

  VideoSource source{};
  std::thread t(publish_frames, &source);

  std::this_thread::sleep_for(std::chrono::seconds(2));

  std::shared_ptr<LocalVideoTrack> track =
      LocalVideoTrack::CreateVideoTrack("hue", source);

  proto::TrackPublishOptions options{};
  options.set_source(proto::SOURCE_CAMERA);
  options.set_simulcast(true);

  std::cout << "Publishing track" << std::endl;
  room->GetLocalParticipant()->PublishTrack(track, options);

  // Should we implement a mechanism to PollEvents/WaitEvents? Like SDL2/glfw
  //   - So we can remove the useless loop here
  // Or is it better to use callback based events?

  while (true) {
  }

  return 0;
}
