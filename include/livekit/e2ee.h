/*
 * Copyright 2025 LiveKit
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

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace livekit {

/* Encryption algorithm type used by the underlying stack. Keep this aligned
 * with your proto enum. */
enum class EncryptionType { NONE = 0, GCM = 1, CUSTOM = 2 };

/// End-to-end encryption (E2EE) configuration.
///
/// When enabled, media frames are encrypted before being sent and
/// decrypted on receipt. Keys may be provided up-front (shared-key mode)
/// or supplied by other mechanisms supported by the underlying runtime.
struct E2EEOptions {
  bool enabled;
  /// Encryption algorithm to use.
  ///
  /// GCM is the default and recommended option.
  EncryptionType encryption_type = EncryptionType::GCM;

  /// Shared static key for shared-key E2EE.
  ///
  /// When using shared-key E2EE, this key must be provided and must be
  /// identical (byte-for-byte) for all participants in the room in order
  /// to successfully encrypt and decrypt media.
  ///
  /// If this is empty while E2EE is enabled, media cannot be decrypted
  /// and participants will not be able to communicate (e.g. black video /
  /// silent audio).
  std::vector<std::uint8_t> shared_key;

  /// Optional salt used when deriving ratcheted encryption keys.
  ///
  /// If empty, a default salt is used by the underlying implementation.
  std::vector<std::uint8_t> ratchet_salt;

  /// Optional ratchet window size.
  ///
  /// Controls how many previous keys are retained during ratcheting.
  /// A value of 0 indicates that the implementation default is used.
  int ratchet_window_size = 0;

  /// Optional failure tolerance for ratcheting.
  ///
  /// Specifies how many consecutive ratcheting failures are tolerated
  /// before encryption errors are reported. A value of 0 indicates
  /// that the implementation default is used.
  int failure_tolerance = 0;
};

class E2EEManager {
public:
  virtual ~E2EEManager();

  E2EEManager(const E2EEManager &) = delete;
  E2EEManager &operator=(const E2EEManager &) = delete;

  E2EEManager(E2EEManager &&) noexcept;
  E2EEManager &operator=(E2EEManager &&) noexcept;

  /**
   * Returns whether end-to-end encryption (E2EE) is currently enabled.
   *
   * This reflects the runtime encryption state for media tracks
   * associated with the room.
   */
  bool enabled() const;

  /**
   * Enable or disable end-to-end encryption at runtime.
   *
   * Disabling E2EE will stop encrypting outgoing media and stop
   * decrypting incoming media.
   *
   * NOTE:
   * - All participants must agree on E2EE state and keys in order
   *   to successfully exchange media.
   * - Disabling E2EE while other participants still have it enabled
   *   will result in media being undecodable.
   */
  void setEnabled(bool enabled);

  /**
   * Set or replace the shared encryption key at the given key index.
   *
   * This is typically used for:
   * - Manual key rotation
   *
   * The provided key MUST be identical across all participants
   * using shared-key E2EE, otherwise media decryption will fail.
   *
   * @param key        Raw key bytes
   * @param key_index Index of the key to set (default: 0)
   */
  void setSharedKey(const std::vector<std::uint8_t> &key, int key_index = 0);

  /**
   * Export the currently active shared key at the given key index.
   *
   * This API is primarily intended for debugging, verification,
   * or diagnostics. Applications should avoid exporting keys
   * unless absolutely necessary.
   *
   * @param key_index Index of the key to export (default: 0)
   * @return          Raw key bytes
   */
  std::vector<std::uint8_t> exportSharedKey(int key_index = 0) const;

  /**
   * Ratchet (derive) a new shared key at the given key index.
   *
   * This advances the key forward and returns the newly derived key.
   * All participants must ratchet keys in the same order to remain
   * in sync.
   *
   * @param key_index Index of the key to ratchet (default: 0)
   * @return          Newly derived key bytes
   */
  std::vector<std::uint8_t> ratchetSharedKey(int key_index = 0);

protected:
  /*
   * Construct an E2EE manager for a connected room.
   *
   * This constructor are intended for internal use by room.
   * Applications should NOT create their own E2EEManager instances.
   *
   * After successfully connecting to a room with E2EE enabled,
   * obtain the E2EE manager via the Room:
   *
   *   auto e2ee_manager = room->e2eeManager();
   *
   * The Room owns and manages the lifetime of the E2EEManager and ensures
   * it is correctly wired to the underlying room handle and track lifecycle.
   */
  explicit E2EEManager(std::uint64_t room_handle, E2EEOptions config);
  friend class Room;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace livekit
