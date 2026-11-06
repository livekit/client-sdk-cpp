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

// Borrowed video frames: the ones that read the SDK's buffer rather than a
// copy of it. These build the OwnedVideoBuffer the FFI would send, over a
// buffer this test owns, so the pointers stay valid however the frame behaves.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "livekit/video_frame.h"
#include "video_utils.h"

namespace livekit::test {
namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 32;
constexpr std::uint32_t kChromaW = (kWidth + 1) / 2;
constexpr std::uint32_t kChromaH = (kHeight + 1) / 2;
constexpr std::size_t kI420Size = static_cast<std::size_t>(kWidth) * kHeight + kChromaW * kChromaH * 2;

/// Exposes the protected factories so the test can build frames the way
/// VideoStream does, without making them public on the SDK type.
struct FrameFactory : VideoFrame {
  using VideoFrame::borrowOwnedInfo;
  using VideoFrame::fromOwnedInfo;
};

/// Stands in for the buffer the FFI hands over.
///
/// `handle.id` is left at 0 so the frame's FfiHandle is inert: dropping a
/// zero handle is a no-op, which keeps these tests from reaching into a
/// native handle map that was never set up.
class FakeBuffer {
public:
  explicit FakeBuffer(std::uint8_t fill = 0x7F) : storage_(kI420Size, fill) {}

  std::uint8_t* data() { return storage_.data(); }
  std::uintptr_t address() const { return reinterpret_cast<std::uintptr_t>(storage_.data()); }

  /// Planar I420, planes back to back, the layout the FFI produces.
  proto::OwnedVideoBuffer planar() const {
    proto::OwnedVideoBuffer owned;
    auto* info = owned.mutable_info();
    info->set_type(proto::VideoBufferType::I420);
    info->set_width(kWidth);
    info->set_height(kHeight);
    info->set_data_ptr(address());

    const std::size_t luma = static_cast<std::size_t>(kWidth) * kHeight;
    const std::size_t chroma = static_cast<std::size_t>(kChromaW) * kChromaH;
    addPlane(info, 0, kWidth, luma);
    addPlane(info, luma, kChromaW, chroma);
    addPlane(info, luma + chroma, kChromaW, chroma);
    return owned;
  }

  /// Packed RGBA, described only by the top-level pointer and stride.
  proto::OwnedVideoBuffer packed() const {
    proto::OwnedVideoBuffer owned;
    auto* info = owned.mutable_info();
    info->set_type(proto::VideoBufferType::RGBA);
    info->set_width(4);
    info->set_height(4);
    info->set_data_ptr(address());
    info->set_stride(4 * 4);
    return owned;
  }

private:
  void addPlane(proto::VideoBufferInfo* info, std::size_t offset, std::uint32_t stride, std::size_t size) const {
    auto* comp = info->add_components();
    comp->set_data_ptr(address() + offset);
    comp->set_stride(stride);
    comp->set_size(static_cast<std::uint32_t>(size));
  }

  std::vector<std::uint8_t> storage_;
};

} // namespace

TEST(VideoFrameBorrowTest, BorrowedFrameReadsTheNativeBufferWithoutCopying) {
  FakeBuffer native;
  VideoFrame frame = FrameFactory::borrowOwnedInfo(native.planar());

  EXPECT_TRUE(frame.isBorrowed());
  EXPECT_EQ(frame.width(), kWidth);
  EXPECT_EQ(frame.height(), kHeight);
  EXPECT_EQ(frame.dataSize(), kI420Size);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(frame.data()), native.address());

  // The giveaway that nothing was copied.
  native.data()[0] = 0x01;
  EXPECT_EQ(frame.data()[0], 0x01);
}

TEST(VideoFrameBorrowTest, OwnedFrameCopiesAndIsIndependent) {
  FakeBuffer native;
  VideoFrame frame = FrameFactory::fromOwnedInfo(native.planar());

  EXPECT_FALSE(frame.isBorrowed());
  EXPECT_NE(reinterpret_cast<std::uintptr_t>(frame.data()), native.address());

  native.data()[0] = 0x01;
  EXPECT_EQ(frame.data()[0], 0x7F);
}

TEST(VideoFrameBorrowTest, BorrowedPlaneInfoComesFromTheSdkNotFromWidthAndHeight) {
  FakeBuffer native;
  VideoFrame frame = FrameFactory::borrowOwnedInfo(native.planar());

  const auto planes = frame.planeInfos();
  ASSERT_EQ(planes.size(), 3u);
  EXPECT_EQ(planes[0].data_ptr, native.address());
  EXPECT_EQ(planes[0].stride, static_cast<std::uint32_t>(kWidth));
  EXPECT_EQ(planes[1].stride, kChromaW);
  EXPECT_EQ(planes[2].stride, kChromaW);
  EXPECT_EQ(planes[0].size + planes[1].size + planes[2].size, kI420Size);
}

TEST(VideoFrameBorrowTest, BorrowedPlaneInfoPreservesAPaddedStride) {
  // A decoder aligns its rows, so the stride the SDK reports can exceed the
  // frame width. Recomputing the layout from width/height would lose that.
  FakeBuffer native;
  proto::OwnedVideoBuffer owned = native.planar();
  owned.mutable_info()->mutable_components(0)->set_stride(kWidth + 16);

  VideoFrame frame = FrameFactory::borrowOwnedInfo(owned);

  EXPECT_EQ(frame.planeInfos()[0].stride, static_cast<std::uint32_t>(kWidth + 16));
}

TEST(VideoFrameBorrowTest, PackedFormatsBorrowTheTopLevelPointer) {
  FakeBuffer native;
  VideoFrame frame = FrameFactory::borrowOwnedInfo(native.packed());

  EXPECT_TRUE(frame.isBorrowed());
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(frame.data()), native.address());
  EXPECT_EQ(frame.dataSize(), 4u * 4u * 4u);
  EXPECT_FALSE(frame.planeInfos().empty());
}

TEST(VideoFrameBorrowTest, ReleaseEmptiesTheFrame) {
  FakeBuffer native;
  VideoFrame frame = FrameFactory::borrowOwnedInfo(native.planar());

  frame.release();

  EXPECT_FALSE(frame.isBorrowed());
  EXPECT_EQ(frame.dataSize(), 0u);
  EXPECT_TRUE(frame.planeInfos().empty());

  frame.release(); // idempotent
  EXPECT_FALSE(frame.isBorrowed());
}

TEST(VideoFrameBorrowTest, ReleaseOnAnOwningFrameLeavesItUsable) {
  FakeBuffer native;
  VideoFrame frame = FrameFactory::fromOwnedInfo(native.planar());

  frame.release();

  EXPECT_FALSE(frame.isBorrowed());
  EXPECT_EQ(frame.dataSize(), kI420Size);
  EXPECT_EQ(frame.data()[0], 0x7F);
}

TEST(VideoFrameBorrowTest, MovingABorrowedFrameLeavesTheSourceEmpty) {
  FakeBuffer native;
  VideoFrame source = FrameFactory::borrowOwnedInfo(native.planar());
  const auto address = reinterpret_cast<std::uintptr_t>(source.data());

  VideoFrame moved(std::move(source));

  EXPECT_TRUE(moved.isBorrowed());
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(moved.data()), address);
  // NOLINTNEXTLINE(bugprone-use-after-move) -- asserting the moved-from state
  EXPECT_FALSE(source.isBorrowed());
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_EQ(source.dataSize(), 0u);
}

TEST(VideoFrameBorrowTest, MoveAssigningABorrowedFrameLeavesTheSourceEmpty) {
  FakeBuffer native;
  VideoFrame source = FrameFactory::borrowOwnedInfo(native.planar());
  VideoFrame target;

  target = std::move(source);

  EXPECT_TRUE(target.isBorrowed());
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_FALSE(source.isBorrowed());
}

TEST(VideoFrameBorrowTest, ConvertingABorrowedFrameToItsOwnFormatClonesTheSdkBuffer) {
  // The same-format fast path used to clone the frame's own vector, which is
  // empty for a borrowed frame.
  FakeBuffer native;
  VideoFrame frame = FrameFactory::borrowOwnedInfo(native.planar());

  const VideoFrame copy = frame.convert(VideoBufferType::I420);

  EXPECT_FALSE(copy.isBorrowed());
  EXPECT_EQ(copy.dataSize(), kI420Size);
  EXPECT_EQ(copy.data()[0], 0x7F);
  EXPECT_NE(reinterpret_cast<std::uintptr_t>(copy.data()), native.address());
}

TEST(VideoFrameBorrowTest, AnUndescribableBufferYieldsAnEmptyFrameRatherThanABorrow) {
  proto::OwnedVideoBuffer owned;
  auto* info = owned.mutable_info();
  info->set_type(proto::VideoBufferType::I420);
  info->set_width(kWidth);
  info->set_height(kHeight);
  info->set_data_ptr(0);

  VideoFrame frame = FrameFactory::borrowOwnedInfo(owned);

  EXPECT_FALSE(frame.isBorrowed());
  EXPECT_EQ(frame.dataSize(), 0u);
}

} // namespace livekit::test
