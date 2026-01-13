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

/* Encryption algorithm type used by the underlying stack.
 * Keep this aligned with your proto enum values. */
enum class EncryptionType {
  NONE = 0,
  GCM = 1,
  CUSTOM = 2,
};

/* Defaults (match other SDKs / Python defaults). */
inline constexpr const char *kDefaultRatchetSalt = "LKFrameEncryptionKey";
inline constexpr int kDefaultRatchetWindowSize = 16;
inline constexpr int kDefaultFailureTolerance = -1;

/**
 * Options for configuring the key provider used by E2EE.
 *
 * Notes:
 * - `shared_key` is optional. If omitted, the application may set keys later
 *   (e.g. via KeyProvider::setSharedKey / per-participant keys).
 * - `ratchet_salt` may be empty to indicate "use implementation default".
 * - `ratchet_window_size` and `failure_tolerance` use SDK defaults unless
 * overridden.
 */
struct KeyProviderOptions {
  /// Shared static key for "shared-key E2EE" (optional).
  ///
  /// If set, it must be identical (byte-for-byte) across all participants
  /// that are expected to decrypt each other’s media.
  ///
  /// If not set, keys must be provided out-of-band later (e.g. via KeyProvider
  /// APIs).
  std::optional<std::vector<std::uint8_t>> shared_key;

  /// Salt used when deriving ratcheted keys.
  ///
  /// If empty, the underlying implementation default is used.
  std::vector<std::uint8_t> ratchet_salt = std::vector<std::uint8_t>(
      kDefaultRatchetSalt, kDefaultRatchetSalt + std::char_traits<char>::length(
                                                     kDefaultRatchetSalt));

  /// Controls how many previous keys are retained during ratcheting.
  int ratchet_window_size = kDefaultRatchetWindowSize;

  /// Number of tolerated ratchet failures before reporting encryption errors.
  int failure_tolerance = kDefaultFailureTolerance;
};

/**
 * End-to-end encryption (E2EE) configuration for a room.
 *
 * Provide this in RoomOptions to initialize E2EE support.
 *
 * IMPORTANT:
 * - Providing E2EEOptions means "E2EE support is configured for this room".
 * - Whether encryption is actively applied can still be toggled at runtime via
 *   E2EEManager::setEnabled().
 * - A room can be configured for E2EE even if no shared key is provided yet.
 *   In that case, the app must supply keys later via KeyProvider (shared-key or
 *   per-participant).
 */
struct E2EEOptions {
  KeyProviderOptions key_provider_options{};
  EncryptionType encryption_type = EncryptionType::GCM; // default & recommended
};

/**
 * E2EE manager for a connected room.
 *
 * Lifetime:
 * - Owned by Room. Applications must not construct E2EEManager directly.
 *
 * Enablement model:
 * - If the Room was created with `RoomOptions.e2ee` set, the room will expose
 *   a non-null E2EEManager via Room::E2eeManager().
 * - If the Room was created without E2EE options, Room::E2eeManager() may be
 * null.
 *
 * Key model:
 * - Keys are managed via KeyProvider (shared-key or per-participant).
 * - Providing a shared key up-front is convenient for shared-key E2EE, but is
 * not required by the API shape (keys may be supplied later).
 */
class E2EEManager {
public:
  /** If your application requires key rotation during the lifetime of a single
   * room or unique keys per participant (such as when implementing the MEGOLM
   * or MLS protocol), you' can do it via key provider and frame cryptor. refer
   * https://docs.livekit.io/home/client/encryption/#custom-key-provider doe
   * details
   *  */
  class KeyProvider {
  public:
    ~KeyProvider() = default;

    KeyProvider(const KeyProvider &) = delete;
    KeyProvider &operator=(const KeyProvider &) = delete;
    KeyProvider(KeyProvider &&) noexcept = default;
    KeyProvider &operator=(KeyProvider &&) noexcept = default;

    /// Returns the options used to initialize this KeyProvider.
    const KeyProviderOptions &options() const;

    /// Sets the shared key for the given key slot.
    void setSharedKey(const std::vector<std::uint8_t> &key, int key_index = 0);

    /// Exports the shared key for a given key slot.
    std::vector<std::uint8_t> exportSharedKey(int key_index = 0) const;

    /// Ratchets the shared key at key_index and returns the newly derived key.
    std::vector<std::uint8_t> ratchetSharedKey(int key_index = 0);

    /// Sets a key for a specific participant identity.
    void setKey(const std::string &participant_identity,
                const std::vector<std::uint8_t> &key, int key_index = 0);

    /// Exports a participant-specific key.
    std::vector<std::uint8_t> exportKey(const std::string &participant_identity,
                                        int key_index = 0) const;

    /// Ratchets a participant-specific key and returns the new key.
    std::vector<std::uint8_t>
    ratchetKey(const std::string &participant_identity, int key_index = 0);

  private:
    friend class E2EEManager;
    KeyProvider(std::uint64_t room_handle,
                KeyProviderOptions options);
    std::uint64_t room_handle_{0};
    KeyProviderOptions options_;
  };

  class FrameCryptor {
  public:
    FrameCryptor(std::uint64_t room_handle, std::string participant_identity,
                 int key_index, bool enabled);
    ~FrameCryptor() = default;
    FrameCryptor(const FrameCryptor &) = delete;
    FrameCryptor &operator=(const FrameCryptor &) = delete;
    FrameCryptor(FrameCryptor &&) noexcept = default;
    FrameCryptor &operator=(FrameCryptor &&) noexcept = default;

    const std::string &participantIdentity() const;
    int keyIndex() const;
    bool enabled() const;

    /// Enables or disables frame encryption/decryption for this participant.
    void setEnabled(bool enabled);

    /// Sets the active key index for this participant cryptor.
    void setKeyIndex(int key_index);

  private:
    std::uint64_t room_handle_{0};
    bool enabled_{false};
    std::string participant_identity_;
    int key_index_{0};
  };

  ~E2EEManager() = default;
  E2EEManager(const E2EEManager &) = delete;
  E2EEManager &operator=(const E2EEManager &) = delete;
  E2EEManager(E2EEManager &&) noexcept = delete;
  E2EEManager &operator=(E2EEManager &&) noexcept = delete;

  /// Returns whether E2EE is currently enabled for this room at runtime.
  bool enabled() const;

  /// Enable or disable E2EE at runtime.
  ///
  /// NOTE:
  /// - Enabling E2EE without having compatible keys set across participants
  ///   will result in undecodable media (black video / silent audio).
  void setEnabled(bool enabled);

  /// Returns the key provider if E2EE was configured for the room; otherwise
  /// nullptr.
  KeyProvider *keyProvider();
  const KeyProvider *keyProvider() const;

  /// Retrieves the current list of frame cryptors from the underlying runtime.
  std::vector<E2EEManager::FrameCryptor> frameCryptors() const;

protected:
  /// Internal constructor used by Room when E2EEOptions are provided.
  explicit E2EEManager(std::uint64_t room_handle, const E2EEOptions &options);
  friend class Room;

private:
  std::uint64_t room_handle_{0};
  bool enabled_{false};
  E2EEOptions options_;
  KeyProvider key_provider_;
};

} // namespace livekit
