/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// End-to-end cover for VideoStream::Options::zero_copy.
//
// The unit tests build the OwnedVideoBuffer by hand; these publish a real
// track, let it travel through the SFU and the decoder, and read it back with
// a borrowed frame. That is the only way to see the pointers the FFI actually
// hands over, and the only way to know the buffer stays valid across the
// pooled-buffer recycling the Rust side now does.

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "tests/common/test_common.h"
#include "tests/common/video_utils.h"

namespace livekit::test {
namespace {

using namespace std::chrono_literals;

constexpr int kWidth = 320;
constexpr int kHeight = 240;

/// Publishes frames until stopped, so the subscriber has something to read.
class FramePublisher {
public:
  FramePublisher(std::shared_ptr<VideoSource> source, int width, int height)
      : source_(std::move(source)), width_(width), height_(height) {}

  void start() {
    thread_ = std::thread([this] {
      std::uint64_t n = 0;
      while (!stop_) {
        VideoFrame frame = VideoFrame::create(width_, height_, VideoBufferType::ARGB);
        fillWebcamLikeFrame(frame, n++);
        source_->captureFrame(frame);
        std::this_thread::sleep_for(33ms); // ~30 fps
      }
    });
  }

  ~FramePublisher() {
    stop_ = true;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

private:
  std::shared_ptr<VideoSource> source_;
  int width_;
  int height_;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

/// Finds the remote video publication named `track_name`, waiting for it.
std::shared_ptr<Track> awaitRemoteVideoTrack(Room& room, const std::string& identity,
                                             const std::string& track_name, std::chrono::seconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    auto participant = room.remoteParticipant(identity).lock();
    if (participant != nullptr) {
      for (const auto& [sid, publication] : participant->trackPublications()) {
        (void)sid;
        if (publication == nullptr || publication->kind() != TrackKind::KIND_VIDEO ||
            publication->name() != track_name) {
          continue;
        }
        if (auto track = publication->track(); track != nullptr) {
          return track;
        }
      }
    }
    std::this_thread::sleep_for(100ms);
  }
  return nullptr;
}

} // namespace

class VideoStreamZeroCopyServerTest : public LiveKitTestBase {
protected:
  /// Connects both rooms, publishes a video track, and returns the remote
  /// track the subscriber sees.
  std::shared_ptr<Track> publishAndSubscribe() {
    const std::string track_name = "zero-copy-track";

    EXPECT_TRUE(receiver_room_.connect(config_.url, config_.token_b, RoomOptions{}));
    EXPECT_TRUE(sender_room_.connect(config_.url, config_.token_a, RoomOptions{}));

    const std::string sender_identity = lockLocalParticipant(sender_room_)->identity();
    EXPECT_FALSE(sender_identity.empty());
    EXPECT_TRUE(waitForParticipant(&receiver_room_, sender_identity, 10s));

    source_ = std::make_shared<VideoSource>(kWidth, kHeight);
    auto track = LocalVideoTrack::createLocalVideoTrack(track_name, source_);

    TrackPublishOptions publish_options;
    publish_options.source = TrackSource::SOURCE_CAMERA;
    publish_options.simulcast = false;
    lockLocalParticipant(sender_room_)->publishTrack(track, publish_options);

    publisher_ = std::make_unique<FramePublisher>(source_, kWidth, kHeight);
    publisher_->start();

    return awaitRemoteVideoTrack(receiver_room_, sender_identity, track_name, 20s);
  }

  Room sender_room_;
  Room receiver_room_;
  std::shared_ptr<VideoSource> source_;
  std::unique_ptr<FramePublisher> publisher_;
};

TEST_F(VideoStreamZeroCopyServerTest, BorrowedFramesCarryDecodedPixels) {
  failIfNotConfigured();

  auto remote_track = publishAndSubscribe();
  ASSERT_NE(remote_track, nullptr) << "the subscriber never saw the published video track";

  VideoStream::Options options;
  options.format = VideoBufferType::I420;
  options.zero_copy = true;
  options.capacity = 8;
  auto stream = VideoStream::fromTrack(remote_track, options);
  ASSERT_NE(stream, nullptr);

  // Read a run of frames. Each one borrows the SDK's buffer, and the previous
  // one's buffer has already gone back to the Rust pool to be handed out
  // again -- so reading valid pixels here is what shows the recycling is safe.
  constexpr int kFramesToRead = 20;
  int read = 0;
  VideoFrameEvent event;
  while (read < kFramesToRead && stream->read(event)) {
    ASSERT_TRUE(event.frame.isBorrowed()) << "frame " << read << " was copied, not borrowed";
    ASSERT_NE(event.frame.data(), nullptr);

    const std::size_t expected = static_cast<std::size_t>(kWidth) * kHeight +
                                 2 * (static_cast<std::size_t>((kWidth + 1) / 2) * ((kHeight + 1) / 2));
    EXPECT_EQ(event.frame.dataSize(), expected);
    EXPECT_EQ(event.frame.width(), kWidth);
    EXPECT_EQ(event.frame.height(), kHeight);

    const auto planes = event.frame.planeInfos();
    ASSERT_EQ(planes.size(), 3u) << "I420 should report three planes";
    for (const auto& plane : planes) {
      EXPECT_NE(plane.data_ptr, 0u);
      EXPECT_GT(plane.stride, 0u);
    }

    // The publisher paints a green-ish frame, so the luma plane must not be
    // uniformly zero. This is the check that the pointer really addresses
    // decoded pixels rather than a buffer that was released underneath us.
    const std::uint8_t* luma = event.frame.data();
    std::uint64_t luma_sum = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(kWidth) * kHeight; ++i) {
      luma_sum += luma[i];
    }
    EXPECT_GT(luma_sum, 0u) << "frame " << read << " decoded to an all-black luma plane";

    ++read;
  }

  EXPECT_EQ(read, kFramesToRead) << "the stream ended after " << read << " frames";
  stream->close();
}

TEST_F(VideoStreamZeroCopyServerTest, DroppedFrameCountIsReportedAndStartsAtZero) {
  failIfNotConfigured();

  auto remote_track = publishAndSubscribe();
  ASSERT_NE(remote_track, nullptr);

  VideoStream::Options options;
  options.format = VideoBufferType::I420;
  options.capacity = 8;
  auto stream = VideoStream::fromTrack(remote_track, options);
  ASSERT_NE(stream, nullptr);

  // A reader that keeps up should see no drops, and the counter must never
  // run backwards: it is cumulative over the stream's life.
  std::uint64_t previous = 0;
  VideoFrameEvent event;
  for (int i = 0; i < 20 && stream->read(event); ++i) {
    EXPECT_GE(event.dropped_frames, previous) << "the counter went backwards at frame " << i;
    previous = event.dropped_frames;
  }

  EXPECT_EQ(previous, 0u) << "a reader keeping up with the stream should not lose frames";
  stream->close();
}

TEST_F(VideoStreamZeroCopyServerTest, OwningFramesOutliveTheStream) {
  failIfNotConfigured();

  auto remote_track = publishAndSubscribe();
  ASSERT_NE(remote_track, nullptr);

  VideoStream::Options options;
  options.format = VideoBufferType::I420;
  options.zero_copy = false; // the default: each frame owns a copy
  options.capacity = 8;
  auto stream = VideoStream::fromTrack(remote_track, options);
  ASSERT_NE(stream, nullptr);

  VideoFrameEvent event;
  ASSERT_TRUE(stream->read(event));
  ASSERT_FALSE(event.frame.isBorrowed());
  const std::vector<std::uint8_t> snapshot(event.frame.data(), event.frame.data() + event.frame.dataSize());

  stream->close();
  stream.reset();

  // An owning frame is independent of the stream, so its pixels survive.
  ASSERT_EQ(event.frame.dataSize(), snapshot.size());
  EXPECT_EQ(std::vector<std::uint8_t>(event.frame.data(), event.frame.data() + event.frame.dataSize()), snapshot);
}

} // namespace livekit::test
