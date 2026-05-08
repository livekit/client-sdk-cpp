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

// Consumer-link canary
// --------------------
// This translation unit is compiled into livekit_consumer_link_test, a target
// that *intentionally* mirrors how an external SDK consumer links against
// liblivekit:
//
//   - Link line:    livekit + GTest::gtest_main          (no spdlog,
//                                                         no protobuf,
//                                                         no absl)
//   - Include path: ${LIVEKIT_ROOT_DIR}/include          (no src/,
//                                                         no generated/)
//   - Defines:      none of LIVEKIT_TEST_ACCESS / LIVEKIT_INTERNAL_API
//
// Two regressions we want this canary to catch fast, in every CI matrix
// entry, before the longer cpp-example-collection smoke runs:
//
//   1. A public header silently grows an `#include <spdlog/...>` (or any
//      other private dep). The compile fails because the private headers are
//      not on the include path.
//   2. A public class loses its LIVEKIT_API tag and stops being exported
//      from liblivekit. The link fails (Linux/macOS: undefined symbol;
//      Windows: unresolved external) instead of slipping through to a
//      runtime dlopen failure on a downstream user's machine.
//
// The deeper consumer-equivalent check (find_package(LiveKit CONFIG) against
// an installed SDK tree) lives in the cpp-example-collection smoke jobs.
// This canary is its lighter, in-tree, per-platform sibling.

#include <gtest/gtest.h>

// Pull in every public header. The umbrella livekit.h covers most of the
// surface; the explicit includes below cover headers it does not transitively
// reference, so a regression in any single header is caught at compile time.
#include <livekit/livekit.h>

#include <livekit/audio_frame.h>
#include <livekit/audio_processing_module.h>
#include <livekit/audio_source.h>
#include <livekit/audio_stream.h>
#include <livekit/build.h>
#include <livekit/data_stream.h>
#include <livekit/data_track_error.h>
#include <livekit/data_track_frame.h>
#include <livekit/data_track_info.h>
#include <livekit/data_track_stream.h>
#include <livekit/e2ee.h>
#include <livekit/export.h>
#include <livekit/ffi_handle.h>
#include <livekit/local_audio_track.h>
#include <livekit/local_data_track.h>
#include <livekit/local_participant.h>
#include <livekit/local_track_publication.h>
#include <livekit/local_video_track.h>
#include <livekit/logging.h>
#include <livekit/participant.h>
#include <livekit/remote_audio_track.h>
#include <livekit/remote_data_track.h>
#include <livekit/remote_participant.h>
#include <livekit/remote_track_publication.h>
#include <livekit/remote_video_track.h>
#include <livekit/result.h>
#include <livekit/room.h>
#include <livekit/room_delegate.h>
#include <livekit/room_event_types.h>
#include <livekit/rpc_error.h>
#include <livekit/stats.h>
#include <livekit/subscription_thread_dispatcher.h>
#include <livekit/track.h>
#include <livekit/track_publication.h>
#include <livekit/tracing.h>
#include <livekit/video_frame.h>
#include <livekit/video_source.h>
#include <livekit/video_stream.h>

namespace {

// Header inclusion alone is enough to exercise the compile path, but we still
// need to *call* a few exported symbols so the linker has to resolve them out
// of liblivekit. Pick a few that don't require server connectivity.
TEST(ConsumerLink, PublicEntryPointsResolve) {
  ASSERT_TRUE(livekit::initialize(livekit::LogLevel::Warn));

  livekit::setLogLevel(livekit::LogLevel::Warn);
  EXPECT_EQ(livekit::getLogLevel(), livekit::LogLevel::Warn);

  livekit::setLogCallback(nullptr);

  EXPECT_FALSE(livekit::isTracingEnabled());

  livekit::shutdown();
}

} // namespace
