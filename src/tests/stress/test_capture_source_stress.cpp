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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <livekit/capture_source.h>
#include <livekit/livekit.h>

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace livekit::test {
namespace {

struct CaptureCompletion {
  std::mutex mutex;
  std::condition_variable cv;
  std::optional<CaptureResult> result;
};

class CaptureSourceStressTest : public ::testing::Test {
protected:
  void SetUp() override { initialize(LogLevel::Warn); }
  void TearDown() override { shutdown(); }
};

TEST_F(CaptureSourceStressTest, ReportsRenderedFramesPerSecond) {
  using namespace std::chrono_literals;
  if constexpr (!kCaptureEnabled) {
    GTEST_SKIP() << "capture sources are disabled in this build";
  }

  std::shared_ptr<CaptureSource> source;
  try {
    source = CaptureSource::create(PatternVideoSourceConfig{{1280, 720}, 30, Pattern::Gradient}).get();
  } catch (const CaptureSourceError& error) {
    if (std::string(error.what()).find("no compatible GPU adapter") != std::string::npos) {
      GTEST_SKIP() << "GPU capture unavailable: " << error.what();
    }
    throw;
  }

  auto completion = std::make_shared<CaptureCompletion>();
  source->setOnFinishedCallback([completion](const CaptureResult& result) {
    {
      const std::scoped_lock lock(completion->mutex);
      completion->result = result;
    }
    completion->cv.notify_all();
  });

  ASSERT_TRUE(source->start());
  const auto started_at = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(2s); // Fixed measurement window for this benchmark.
  const auto stopped_at = std::chrono::steady_clock::now();
  ASSERT_TRUE(source->stop());

  std::unique_lock<std::mutex> lock(completion->mutex);
  ASSERT_TRUE(completion->cv.wait_for(lock, 10s, [&completion] { return completion->result.has_value(); }));
  const CaptureResult result = completion->result.value_or(CaptureResult{});
  lock.unlock();

  ASSERT_FALSE(result.error.has_value());
  EXPECT_EQ(result.exit, CaptureExit::Stopped);
  EXPECT_GT(result.frames_captured, 0u);
  const double elapsed_seconds = std::chrono::duration<double>(stopped_at - started_at).count();
  std::cout << "Pattern capture at 1280x720: " << static_cast<double>(result.frames_captured) / elapsed_seconds
            << " frames/s (" << result.frames_captured << " frames)\n";
}

} // namespace
} // namespace livekit::test
