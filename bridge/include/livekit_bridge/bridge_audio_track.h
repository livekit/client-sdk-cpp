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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace livekit {
class AudioSource;
class LocalAudioTrack;
class LocalTrackPublication;
class LocalParticipant;
} // namespace livekit

namespace livekit_bridge {

namespace test {
class BridgeAudioTrackTest;
} // namespace test

/**
 * Handle to a published local audio track.
 *
 * Created via LiveKitBridge::createAudioTrack(). The bridge retains a
 * reference to every track it creates and will automatically release all
 * tracks when disconnect() is called. To unpublish a track mid-session,
 * call release() explicitly; dropping the shared_ptr alone is not
 * sufficient because the bridge still holds a reference.
 *
 * After release() (whether called explicitly or by the bridge on
 * disconnect), pushFrame() returns false and mute()/unmute() become
 * no-ops. The track object remains valid but inert.
 *
 * All public methods are thread-safe: it is safe to call pushFrame() from
 * one thread while another calls mute()/unmute()/release(), or to call
 * pushFrame() concurrently from multiple threads.
 *
 * Usage:
 *   auto mic = bridge.createAudioTrack("mic", 48000, 2,
 *       livekit::TrackSource::SOURCE_MICROPHONE);
 *   mic->pushFrame(pcm_data, samples_per_channel);
 *   mic->mute();
 *   mic->release();  // unpublishes the track mid-session
 */
class BridgeAudioTrack {
public:
  ~BridgeAudioTrack();

  // Non-copyable
  BridgeAudioTrack(const BridgeAudioTrack &) = delete;
  BridgeAudioTrack &operator=(const BridgeAudioTrack &) = delete;

  /**
   * Push a PCM audio frame to the track.
   *
   * @param data                Interleaved int16 PCM samples.
   *                            Must contain exactly
   *                            (samples_per_channel * num_channels) elements.
   * @param samples_per_channel Number of samples per channel in this frame.
   * @param timeout_ms          Max time to wait for FFI confirmation.
   *                            0 = wait indefinitely (default).
   * @return true if the frame was pushed, false if the track has been released.
   */
  bool pushFrame(const std::vector<std::int16_t> &data, int samples_per_channel,
                 int timeout_ms = 0);

  /**
   * Push a PCM audio frame from a raw pointer.
   *
   * @param data                Pointer to interleaved int16 PCM samples.
   * @param samples_per_channel Number of samples per channel.
   * @param timeout_ms          Max time to wait for FFI confirmation.
   * @return true if the frame was pushed, false if the track has been released.
   */
  bool pushFrame(const std::int16_t *data, int samples_per_channel,
                 int timeout_ms = 0);

  /// Mute the audio track (stops sending audio to the room).
  void mute();

  /// Unmute the audio track (resumes sending audio to the room).
  void unmute();

  /// Track name as provided at creation.
  const std::string &name() const noexcept { return name_; }

  /// Sample rate in Hz.
  int sampleRate() const noexcept { return sample_rate_; }

  /// Number of audio channels.
  int numChannels() const noexcept { return num_channels_; }

  /// Whether this track has been released / unpublished.
  bool isReleased() const noexcept;

  /**
   * Explicitly unpublish the track and release all underlying SDK resources.
   *
   * After this call, pushFrame() returns false and mute()/unmute() are
   * no-ops. Called automatically by the destructor and by
   * LiveKitBridge::disconnect(). Safe to call multiple times (idempotent).
   */
  void release();

private:
  friend class LiveKitBridge;
  friend class test::BridgeAudioTrackTest;

  BridgeAudioTrack(std::string name, int sample_rate, int num_channels,
                   std::shared_ptr<livekit::AudioSource> source,
                   std::shared_ptr<livekit::LocalAudioTrack> track,
                   std::shared_ptr<livekit::LocalTrackPublication> publication,
                   livekit::LocalParticipant *participant);

  mutable std::mutex mutex_;
  std::string name_;
  int sample_rate_;
  int num_channels_;
  bool released_ = false;

  std::shared_ptr<livekit::AudioSource> source_;
  std::shared_ptr<livekit::LocalAudioTrack> track_;
  std::shared_ptr<livekit::LocalTrackPublication> publication_;
  livekit::LocalParticipant *participant_ = nullptr; // not owned
};

} // namespace livekit_bridge
