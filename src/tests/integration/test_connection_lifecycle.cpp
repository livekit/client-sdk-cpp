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

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include "../common/tcp_fault_proxy.h"
#include "../common/test_common.h"
#include "ffi_client.h"

namespace livekit::test {
namespace {

using namespace std::chrono_literals;

struct ServerAddress {
  std::string host;
  std::uint16_t port;
  std::string path;
};

ServerAddress parseWsUrl(const std::string& url) {
  constexpr char kScheme[] = "ws://";
  if (url.compare(0, sizeof(kScheme) - 1, kScheme) != 0) {
    throw std::invalid_argument("offline unpublish test requires a ws:// URL");
  }

  const std::string authority_and_path = url.substr(sizeof(kScheme) - 1);
  const auto path_start = authority_and_path.find('/');
  const std::string authority = authority_and_path.substr(0, path_start);
  const auto colon = authority.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 == authority.size()) {
    throw std::invalid_argument("offline unpublish test URL must include an explicit port");
  }

  const auto port = std::stoul(authority.substr(colon + 1));
  if (port == 0 || port > 65535) {
    throw std::invalid_argument("offline unpublish test URL port is outside 1..65535");
  }
  return {authority.substr(0, colon), static_cast<std::uint16_t>(port),
          path_start == std::string::npos ? "" : authority_and_path.substr(path_start)};
}

class ReconnectDelegate final : public RoomDelegate {
public:
  void onReconnecting(Room&, const ReconnectingEvent&) override {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      reconnecting_ = true;
    }
    cv_.notify_all();
  }

  bool waitForReconnecting(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this]() { return reconnecting_; });
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool reconnecting_{false};
};

class ConnectionLifecycleIntegrationTest : public LiveKitTestBase {};

// Covers case where unpublishTrack() is called after a disconnect and hangs indefinitely.
// Reported by user: https://github.com/livekit/client-sdk-cpp/issues/304
TEST_F(ConnectionLifecycleIntegrationTest, DisconnectedUnpublishTrackDoesNotBlock) {
  failIfNotConfigured();

  const ServerAddress upstream = parseWsUrl(config_.url);
  TcpFaultProxy proxy(upstream.host, upstream.port);
  proxy.start();
  const std::string proxy_url = "ws://127.0.0.1:" + std::to_string(proxy.listenPort()) + upstream.path;

  ReconnectDelegate delegate;
  auto room = std::make_unique<Room>();
  room->setDelegate(&delegate);
  ASSERT_TRUE(room->connect(proxy_url, config_.token_a, {}));

  auto participant = lockLocalParticipant(*room);
  constexpr char kTrackName[] = "offline-unpublish-integration";
  constexpr int kVideoWidth = 320;
  constexpr int kVideoHeight = 180;
  auto source = std::make_shared<VideoSource>(kVideoWidth, kVideoHeight);
  auto track = participant->publishVideoTrack(kTrackName, source, TrackSource::SOURCE_CAMERA);
  ASSERT_NE(track, nullptr);
  ASSERT_NE(track->publication(), nullptr);
  const std::string track_sid = track->publication()->sid();

  proxy.pause();
  const bool reconnecting = delegate.waitForReconnecting(60s);
  if (!reconnecting) {
    proxy.resume();
  }
  ASSERT_TRUE(reconnecting) << "room did not enter Reconnecting while proxy traffic was paused";

  // Exercise the same FFI operation as LocalParticipant::unpublishTrack(),
  // without blocking the GTest thread indefinitely on the public method's
  // future.get(). A discarded std::future returned by the promise does not
  // wait in its destructor, so the test can report a bounded failure.
  auto unpublish =
      FfiClient::instance().unpublishTrackAsync(participant->ffiHandleId(), track_sid, /*stop_on_unpublish=*/true);
  const auto status = unpublish.wait_for(5s);
  proxy.resume();

  if (status == std::future_status::ready) {
    EXPECT_NO_THROW(unpublish.get());
  }
  EXPECT_EQ(status, std::future_status::ready)
      << "unpublishTrack FFI callback did not arrive within 5 seconds while signaling was offline";

  track.reset();
  source.reset();
  room->disconnect();
  room.reset();
}

} // namespace
} // namespace livekit::test
