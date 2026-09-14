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

#pragma once

#include <livekit/livekit.h>

#include <atomic>
#include <cstddef>
#include <mutex>

#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/subscription_thread_dispatcher.h"

namespace livekit {

// RoomTestAccess deliberately reaches into the (deprecated-for-external-use)
// SubscriptionThreadDispatcher as part of exercising Room's internals; suppress
// the deprecation warning for that in-tree, intentional use.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

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

  /// Number of live audio/video reader threads owned by the room's dispatcher.
  ///
  /// Used by integration tests to prove that replacing a frame callback stops
  /// the previous reader rather than leaking one per registration.
  static std::size_t activeReaderCount(const Room& room) {
    const auto& dispatcher = room.subscription_thread_dispatcher_;
    if (!dispatcher) {
      return 0;
    }
    const std::scoped_lock<std::mutex> guard(dispatcher->lock_);
    return dispatcher->active_readers_.size();
  }

  /// Number of live data track reader threads owned by the room's dispatcher.
  static std::size_t activeDataReaderCount(const Room& room) {
    const auto& dispatcher = room.subscription_thread_dispatcher_;
    if (!dispatcher) {
      return 0;
    }
    const std::scoped_lock<std::mutex> guard(dispatcher->lock_);
    return dispatcher->active_data_readers_.size();
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
    const auto it = dispatcher->subscribed_tracks_.find(key);
    return it != dispatcher->subscribed_tracks_.end() && it->second != nullptr;
  }

  /// Number of audio/video keys whose previous reader is being joined. Zero
  /// whenever no replacement, clear, or resubscribe is mid-flight.
  static std::size_t drainingReaderCount(const Room& room) {
    const auto& dispatcher = room.subscription_thread_dispatcher_;
    if (!dispatcher) {
      return 0;
    }
    const std::scoped_lock<std::mutex> guard(dispatcher->lock_);
    return dispatcher->draining_readers_.size();
  }
};

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace livekit
