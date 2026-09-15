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

/// @file room_test_access.h
/// @brief In-tree test access to Room internals.
///
/// Room declares this struct a friend, as does SubscriptionThreadDispatcher, so
/// tests can inspect state that is deliberately not part of the public API.
/// The test binaries define LIVEKIT_TEST_ACCESS, which also compiles away the
/// dispatcher's direct-use deprecation attribute.

#pragma once

#include <livekit/livekit.h>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>

#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/subscription_thread_dispatcher.h"

namespace livekit {

struct RoomTestAccess {
  static void installConnectedListener(Room& room, std::atomic<int>& callback_count) {
    const auto listener_id = FfiClient::instance().addListener([&room, &callback_count](const proto::FfiEvent& event) {
      callback_count.fetch_add(1, std::memory_order_relaxed);
      room.onEvent(event);
    });

    const std::scoped_lock<std::mutex> guard(room.lock_);
    room.connection_state_ = ConnectionState::Connected;
    room.room_handle_ = std::make_shared<FfiHandle>();
    room.listener_id_ = listener_id;
  }

  static bool hasRoomHandle(const Room& room) {
    const std::scoped_lock<std::mutex> guard(room.lock_);
    return static_cast<bool>(room.room_handle_);
  }

  static int listenerId(const Room& room) {
    const std::scoped_lock<std::mutex> guard(room.lock_);
    return room.listener_id_;
  }

  /// Number of audio/video reader threads owned by the room's dispatcher
  /// (subscription slots that currently hold a reader thread).
  ///
  /// Used by integration tests to prove that replacing a frame callback stops
  /// the previous reader rather than leaking one per registration.
  static std::size_t activeReaderCount(const Room& room) {
    const auto& dispatcher = room.subscription_thread_dispatcher_;
    if (!dispatcher) {
      return 0;
    }
    const std::scoped_lock<std::mutex> guard(dispatcher->lock_);
    std::size_t count = 0;
    for (const auto& [key, slot] : dispatcher->active_readers_) {
      (void)key;
      if (slot.thread.joinable()) {
        ++count;
      }
    }
    return count;
  }

  /// Number of live data track reader threads owned by the room's dispatcher.
  static std::size_t activeDataReaderCount(const Room& room) {
    const auto& dispatcher = room.subscription_thread_dispatcher_;
    if (!dispatcher) {
      return 0;
    }
    const std::scoped_lock<std::mutex> guard(dispatcher->lock_);
    std::size_t count = 0;
    for (const auto& [id, reader] : dispatcher->active_data_readers_) {
      (void)id;
      if (reader && reader->thread.joinable() && !reader->finished()) {
        ++count;
      }
    }
    return count;
  }

  /// Whether the room's dispatcher has retained a subscribed audio/video track
  /// for the given participant and track name. This retention is what lets a
  /// frame callback registered after the subscription event start a reader
  /// immediately (GitHub issue #235).
  static bool hasRetainedSubscribedTrack(const Room& room, const std::string& participant_identity,
                                         const std::string& track_name) {
    const auto& dispatcher = room.subscription_thread_dispatcher_;
    if (!dispatcher) {
      return false;
    }
    const std::scoped_lock<std::mutex> guard(dispatcher->lock_);
    const SubscriptionThreadDispatcher::CallbackKey key{participant_identity, track_name};
    const auto it = dispatcher->active_readers_.find(key);
    return it != dispatcher->active_readers_.end() && it->second.track != nullptr;
  }

  /// Number of audio/video keys whose previous reader is being joined. Zero
  /// whenever no replacement, clear, or resubscribe is mid-flight.
  static std::size_t drainingReaderCount(const Room& room) {
    const auto& dispatcher = room.subscription_thread_dispatcher_;
    if (!dispatcher) {
      return 0;
    }
    const std::scoped_lock<std::mutex> guard(dispatcher->lock_);
    std::size_t count = 0;
    for (const auto& [key, slot] : dispatcher->active_readers_) {
      (void)key;
      if (slot.draining) {
        ++count;
      }
    }
    return count;
  }
};

} // namespace livekit
