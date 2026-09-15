/*
 * Copyright 2026 LiveKit, Inc.
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

#include <livekit/livekit.h>
#include <livekit/local_data_track.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(_WIN32)
#define NOMINMAX
#include <psapi.h>
#include <windows.h>
#else
#include <dirent.h>
#include <malloc.h>

#include <fstream>
#include <sstream>
#endif

namespace {

using namespace std::chrono_literals;
using StreamId = std::size_t;

constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 1;
constexpr int kAudioSamplesPerChannel = 480;
constexpr char kAudioTrackName[] = "lifecycle-audio";
constexpr char kDataTrackName[] = "lifecycle-data";
constexpr char kControlTopic[] = "lifecycle-control";
constexpr char kStatusTopic[] = "lifecycle-status";

struct StreamConfig {
  StreamId id = 0;
  std::string track_name;
  int width = 1280;
  int height = 720;
  std::uint64_t max_bitrate = 2'500'000;
  double max_framerate = 30.0;
  bool initially_muted = false;
};

struct Configuration {
  std::string livekit_url;
  std::string livekit_token;
  std::string receiver_token;
  std::vector<StreamConfig> video_streams;
  std::string data_track_name = kDataTrackName;
  std::string control_topic = kControlTopic;
  std::string status_topic = kStatusTopic;
  int audio_sample_rate = kAudioSampleRate;
  int audio_channels = kAudioChannels;
  int audio_capture_timeout_ms = 1000;
  std::chrono::milliseconds delegate_detach_grace_period{10};
  std::chrono::milliseconds media_drain_period{250};
  bool enable_receiver = false;
  bool enable_data_track = false;
  bool enable_control_packets = false;
  bool enable_log_callback = false;
};

struct ConnectionRequest {
  std::string url;
  std::string token;

  const std::string& livekit_url() const noexcept { return url; }     // NOLINT(readability-identifier-naming)
  const std::string& livekit_token() const noexcept { return token; } // NOLINT(readability-identifier-naming)
};

struct VideoFrame {
  int width = 0;
  int height = 0;
  livekit::VideoBufferType format = livekit::VideoBufferType::I420;
  std::vector<std::uint8_t> data;
};

struct AudioChunk {
  std::vector<std::int16_t> samples;
  int sample_rate = kAudioSampleRate;
  int channels = kAudioChannels;
  int samples_per_channel = kAudioSamplesPerChannel;
};

struct StatusMessage {
  bool enabled = true;
};

struct ProcessSample {
  std::uint64_t rss_kib = 0;
  std::optional<std::uint64_t> pss_kib;
  std::optional<std::uint64_t> private_dirty_kib;
  std::optional<std::uint64_t> threads;
  std::optional<std::uint64_t> file_descriptors;
};

class SingleThreadExecutor {
public:
  SingleThreadExecutor() : worker_([this] { run(); }) {}

  ~SingleThreadExecutor() { join(); }

  SingleThreadExecutor(const SingleThreadExecutor&) = delete;
  SingleThreadExecutor& operator=(const SingleThreadExecutor&) = delete;

  void add(std::function<void()> task) {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (stopping_) {
        throw std::runtime_error("executor is stopping");
      }
      tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
  }

  template <typename Function>
  auto submit(Function&& function) -> std::future<decltype(function())> {
    using Result = decltype(function());
    auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Function>(function));
    auto future = task->get_future();
    add([task] { (*task)(); });
    return future;
  }

  void flush() {
    submit([] {}).get();
  }

  void join() {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

private:
  void run() {
    while (true) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
        if (stopping_ && tasks_.empty()) {
          return;
        }
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }
      task();
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> tasks_;
  bool stopping_ = false;
  std::thread worker_;
};

class SynchronizedDataConsumer {
public:
  void consume(const std::vector<std::uint8_t>&, std::optional<std::uint64_t>) {
    const std::scoped_lock<std::mutex> lock(mutex_);
    if (enabled_) {
      ++frames_;
    }
  }

  void resetForNewSession() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    enabled_ = true;
    frames_ = 0;
  }

  void disable() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    enabled_ = false;
  }

private:
  std::mutex mutex_;
  bool enabled_ = true;
  std::uint64_t frames_ = 0;
};

class ReceiveState {
public:
  explicit ReceiveState(std::size_t stream_count) : frame_counts_(stream_count, 0) {}

  void onFrame(StreamId id) {
    {
      const std::scoped_lock<std::mutex> lock(mutex_);
      ++frame_counts_.at(id);
    }
    cv_.notify_all();
  }

  bool hasEveryStream() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    return hasEveryStreamLocked();
  }

  bool waitForEveryStream(std::chrono::seconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return hasEveryStreamLocked(); });
  }

private:
  bool hasEveryStreamLocked() const {
    for (const int count : frame_counts_) {
      if (count == 0) {
        return false;
      }
    }
    return true;
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<int> frame_counts_;
};

class RoomSession;

class SessionOwner {
public:
  explicit SessionOwner(Configuration config) : config_(std::move(config)) {}
  ~SessionOwner();

  void requestConnect(const ConnectionRequest& command);
  void requestDisconnect();
  void disconnectOnExecutor();

  void pushMedia(int frame_count);
  void flush() { livekit_executor_.flush(); }
  bool connected() const noexcept { return room_connected_.load(); }
  std::string lastError() const;

private:
  mutable std::mutex error_mutex_;
  Configuration config_;
  SingleThreadExecutor livekit_executor_;
  std::unique_ptr<RoomSession> room_;
  std::shared_ptr<SynchronizedDataConsumer> data_consumer_;
  std::atomic<bool> room_connected_{false};
  std::atomic<bool> livekit_connect_requested_{false};
  std::string active_livekit_url_;
  std::string active_livekit_token_;
  std::string last_error_;
};

class RoomSession : public livekit::RoomDelegate {
public:
  using DataCallback = std::function<void(const std::vector<std::uint8_t>&, std::optional<std::uint64_t>)>;
  using DataSessionStartedCallback = std::function<void()>;
  using StreamStateCallback = std::function<void(bool, const std::string&)>;
  using RoomDisconnectedCallback = std::function<void(livekit::DisconnectReason)>;

  RoomSession(Configuration config, SingleThreadExecutor& executor, DataCallback data_callback,
              DataSessionStartedCallback data_session_started_callback, StreamStateCallback stream_state_callback,
              RoomDisconnectedCallback room_disconnected_callback)
      : config_(std::move(config)),
        executor_(&executor),
        data_callback_(std::move(data_callback)),
        data_session_started_callback_(std::move(data_session_started_callback)),
        stream_state_callback_(std::move(stream_state_callback)),
        room_disconnected_callback_(std::move(room_disconnected_callback)),
        receive_state_(std::make_shared<ReceiveState>(config_.video_streams.size())) {
    try {
      constructor_body();
    } catch (...) {
      close();
      throw;
    }
  }

  ~RoomSession() override {
    try {
      close();
    } catch (...) {
      std::fputs("RoomSession teardown failed\n", stderr);
    }
  }

  void initializeSdk();
  void constructor_body(); // NOLINT(readability-identifier-naming)
  void publishOneTrack(const StreamConfig& spec);
  void subscribeToDataFromParticipant(const std::string& identity);
  void publishStatus(const StatusMessage& status, std::optional<std::string> destination_identity);
  void pushFrame(StreamId id, VideoFrame frame, std::int64_t capture_time_us);
  void pushAudio(const AudioChunk& chunk);
  void clearDataSubscriptions();
  void close();

  void onParticipantConnected(livekit::Room&, const livekit::ParticipantConnectedEvent& event) override;
  void onParticipantDisconnected(livekit::Room&, const livekit::ParticipantDisconnectedEvent& event) override;
  void onUserPacketReceived(livekit::Room&, const livekit::UserDataPacketEvent& event) override;
  void onDisconnected(livekit::Room&, const livekit::DisconnectedEvent& event) override;

  bool waitForDecodedFrames() { return receive_state_->waitForEveryStream(10s); }
  bool hasDecodedFrames() { return receive_state_->hasEveryStream(); }

private:
  struct VideoStreamState {
    std::shared_ptr<livekit::VideoSource> source;
    std::shared_ptr<livekit::LocalVideoTrack> track;
  };

  Configuration config_;
  SingleThreadExecutor* executor_ = nullptr;
  DataCallback data_callback_;
  DataSessionStartedCallback data_session_started_callback_;
  StreamStateCallback stream_state_callback_;
  RoomDisconnectedCallback room_disconnected_callback_;
  std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
  std::unique_ptr<livekit::Room> room_;
  std::unique_ptr<livekit::Room> receiver_room_;
  std::shared_ptr<livekit::LocalDataTrack> receiver_data_track_;
  std::shared_ptr<livekit::AudioSource> audio_source_;
  std::shared_ptr<livekit::LocalAudioTrack> audio_track_;
  std::unordered_map<StreamId, VideoStreamState> video_streams_;
  std::shared_ptr<ReceiveState> receive_state_;
  std::optional<livekit::DataFrameCallbackId> data_callback_id_;
  std::string subscribed_identity_;
  std::string sender_identity_;
  bool optional_stream_enabled_ = true;
  bool sdk_initialized_ = false;
  bool closed_ = false;
};

template <typename LocalTrack>
void unpublishLocalTrack(const std::shared_ptr<LocalTrack>& track,
                         const std::shared_ptr<livekit::LocalParticipant>& participant) {
  if (!track) {
    return;
  }
  const auto publication = track->publication();
  if (participant && publication && !publication->sid().empty()) {
    try {
      participant->unpublishTrack(publication->sid());
    } catch (const std::exception& error) {
      std::clog << "unpublish failed: " << error.what() << '\n';
    }
  }
  track->setPublication(nullptr);
}

void RoomSession::initializeSdk() {
  if (!livekit::initialize(livekit::LogLevel::Warn)) {
    throw std::runtime_error("LiveKit SDK initialization failed");
  }
  sdk_initialized_ = true;
  if (config_.enable_log_callback) {
    livekit::setLogCallback([](livekit::LogLevel, const std::string&, const std::string& message) {
      std::clog << "[livekit] " << message << '\n';
    });
  }
}

void RoomSession::constructor_body() { // NOLINT(readability-identifier-naming)
  initializeSdk();

  if (config_.enable_receiver) {
    // Test-only peer: it guarantees the customer's NVENC output is subscribed
    // and decoded, without changing the RoomSession API or teardown sequence.
    receiver_room_ = std::make_unique<livekit::Room>();
    if (!receiver_room_->connect(config_.livekit_url, config_.receiver_token, {})) {
      throw std::runtime_error("CUDA receiver failed to connect");
    }
    auto receiver = receiver_room_->localParticipant().lock();
    if (!receiver) {
      throw std::runtime_error("CUDA receiver local participant unavailable");
    }
    if (config_.enable_data_track) {
      auto data_result = receiver->publishDataTrack(config_.data_track_name);
      if (!data_result) {
        throw std::runtime_error("CUDA receiver data-track publication failed");
      }
      receiver_data_track_ = data_result.value();
    }
  }

  room_ = std::make_unique<livekit::Room>();
  room_->setDelegate(this);
  if (!room_->connect(config_.livekit_url, config_.livekit_token, {})) {
    throw std::runtime_error("Room::connect failed");
  }

  auto participant = room_->localParticipant().lock();
  if (!participant) {
    throw std::runtime_error("No local participant");
  }
  sender_identity_ = participant->identity();

  audio_source_ =
      std::make_shared<livekit::AudioSource>(config_.audio_sample_rate, config_.audio_channels, /*queue_size_ms=*/0);
  audio_track_ =
      participant->publishAudioTrack(kAudioTrackName, audio_source_, livekit::TrackSource::SOURCE_MICROPHONE);
  if (!audio_track_) {
    throw std::runtime_error("Audio publication failed");
  }

  for (const auto& spec : config_.video_streams) {
    video_streams_[spec.id].source = std::make_shared<livekit::VideoSource>(spec.width, spec.height);
    publishOneTrack(spec);
    if (receiver_room_) {
      receiver_room_->setOnVideoFrameCallback(
          sender_identity_, spec.track_name,
          [state = receive_state_, id = spec.id](const livekit::VideoFrame&, std::int64_t) { state->onFrame(id); });
    }
  }

  if (config_.enable_data_track) {
    for (const auto& weak : room_->remoteParticipants()) {
      if (const auto remote = weak.lock()) {
        subscribeToDataFromParticipant(remote->identity());
      }
    }
  }

  if (config_.enable_control_packets && receiver_room_) {
    if (auto receiver = receiver_room_->localParticipant().lock()) {
      receiver->publishData(std::vector<std::uint8_t>{1}, true, {sender_identity_}, config_.control_topic);
    }
  }
}

void RoomSession::publishOneTrack(const StreamConfig& spec) {
  auto& stream = video_streams_.at(spec.id);
  const auto track = livekit::LocalVideoTrack::createLocalVideoTrack(spec.track_name, stream.source);
  livekit::TrackPublishOptions options;
  options.source = livekit::TrackSource::SOURCE_CAMERA;
  options.video_codec = livekit::VideoCodec::H264;
  options.video_encoder = livekit::VideoEncoderBackend::Nvenc;
  options.stream = spec.track_name;
  options.simulcast = false;
  options.degradation_preference = livekit::DegradationPreference::MaintainResolution;
  options.video_encoding = livekit::VideoEncodingOptions{spec.max_bitrate, spec.max_framerate};
  options.red = false;
  options.dtx = false;
  options.preconnect_buffer = false;
  auto participant = room_->localParticipant().lock();
  if (!participant) {
    throw std::runtime_error("No local participant");
  }
  participant->publishTrack(track, options);
  if (spec.initially_muted) {
    track->mute();
  }
  stream.track = track;
}

void RoomSession::onParticipantConnected(livekit::Room&, const livekit::ParticipantConnectedEvent& event) {
  if (!event.participant) {
    return;
  }
  if (!config_.enable_data_track) {
    return;
  }
  executor_->add([this, alive = alive_, identity = event.participant->identity()] {
    if (!alive->load()) {
      return;
    }
    subscribeToDataFromParticipant(identity);
  });
}

void RoomSession::subscribeToDataFromParticipant(const std::string& identity) {
  if (!data_callback_ || data_callback_id_) {
    return;
  }
  data_session_started_callback_();
  data_callback_id_ =
      room_->addOnDataFrameCallback(identity, config_.data_track_name,
                                    [consume = data_callback_](const std::vector<std::uint8_t>& payload,
                                                               std::optional<std::uint64_t> sender_timestamp_ms) {
                                      consume(payload, sender_timestamp_ms);
                                    });
  subscribed_identity_ = identity;
}

void RoomSession::onParticipantDisconnected(livekit::Room&, const livekit::ParticipantDisconnectedEvent& event) {
  if (!event.participant) {
    return;
  }
  executor_->add([this, alive = alive_, identity = event.participant->identity()] {
    if (!alive->load()) {
      return;
    }
    if (data_callback_id_ && identity == subscribed_identity_) {
      clearDataSubscriptions();
    }
  });
}

void RoomSession::onUserPacketReceived(livekit::Room&, const livekit::UserDataPacketEvent& event) {
  if (!config_.enable_control_packets || event.topic != config_.control_topic || !event.participant ||
      event.kind != livekit::DataPacketKind::Reliable) {
    return;
  }
  const std::string identity = event.participant->identity();
  const std::vector<std::uint8_t> payload = event.data;
  executor_->add([this, alive = alive_, identity, payload] { // NOLINT(bugprone-exception-escape)
    if (!alive->load() || payload.empty()) {
      return;
    }
    const bool enabled = payload.front() != 0;
    if (enabled != optional_stream_enabled_ && !config_.video_streams.empty()) {
      const auto track = video_streams_.at(config_.video_streams.front().id).track;
      if (enabled) {
        track->unmute();
      } else {
        track->mute();
      }
      optional_stream_enabled_ = enabled;
    }
    stream_state_callback_(enabled, identity);
  });
}

void RoomSession::publishStatus(
    const StatusMessage& status,
    std::optional<std::string> destination_identity) { // NOLINT(performance-unnecessary-value-param)
  const std::string destination = destination_identity.value_or(subscribed_identity_);
  if (!room_ || destination.empty()) {
    return;
  }
  auto participant = room_->localParticipant().lock();
  if (!participant) {
    throw std::runtime_error("No local participant");
  }
  participant->publishData(std::vector<std::uint8_t>{static_cast<std::uint8_t>(status.enabled)}, true, {destination},
                           config_.status_topic);
}

void RoomSession::pushFrame(StreamId id, VideoFrame frame, std::int64_t capture_time_us) {
  const livekit::VideoFrame sdk_frame(frame.width, frame.height, frame.format, std::move(frame.data));
  video_streams_.at(id).source->captureFrame(sdk_frame, capture_time_us);
}

void RoomSession::pushAudio(const AudioChunk& chunk) {
  if (chunk.sample_rate != config_.audio_sample_rate || chunk.channels != config_.audio_channels ||
      chunk.samples.size() !=
          static_cast<std::size_t>(chunk.channels) * static_cast<std::size_t>(chunk.samples_per_channel)) {
    return;
  }
  audio_source_->captureFrame(
      livekit::AudioFrame(chunk.samples, chunk.sample_rate, chunk.channels, chunk.samples_per_channel),
      config_.audio_capture_timeout_ms);
  if (config_.enable_data_track && receiver_data_track_) {
    auto result = receiver_data_track_->tryPush(std::vector<std::uint8_t>{0x5a});
    if (!result) {
      std::clog << "data-track push failed: " << result.error().message << '\n';
    }
  }
}

void RoomSession::onDisconnected(livekit::Room&, const livekit::DisconnectedEvent& event) {
  executor_->add([this, alive = alive_, reason = event.reason, notify = room_disconnected_callback_] {
    if (!alive->load()) {
      return;
    }
    close();
    if (notify) {
      notify(reason);
    }
  });
}

void RoomSession::clearDataSubscriptions() {
  if (room_ && data_callback_id_) {
    room_->removeOnDataFrameCallback(*data_callback_id_);
  }
  data_callback_id_.reset();
  subscribed_identity_.clear();
}

void RoomSession::close() {
  if (closed_) {
    return;
  }
  closed_ = true;
  alive_->store(false);
  clearDataSubscriptions();
  if (room_) {
    room_->setDelegate(nullptr);
    std::this_thread::sleep_for(config_.delegate_detach_grace_period);
  }

  auto participant = room_ ? room_->localParticipant().lock() : nullptr;
  unpublishLocalTrack(audio_track_, participant);
  for (auto& [id, stream] : video_streams_) {
    (void)id;
    unpublishLocalTrack(stream.track, participant);
    if (receiver_room_ && stream.track) {
      receiver_room_->clearOnVideoFrameCallback(sender_identity_, stream.track->name());
    }
  }
  participant.reset();
  audio_track_.reset();
  audio_source_.reset();
  video_streams_.clear();

  if (receiver_data_track_) {
    try {
      receiver_data_track_->unpublishDataTrack();
    } catch (const std::exception& error) {
      std::clog << "data-track unpublish failed: " << error.what() << '\n';
    }
  }
  receiver_data_track_.reset();
  receiver_room_.reset();
  room_.reset();

  if (sdk_initialized_) {
    livekit::shutdown();
    sdk_initialized_ = false;
  }
}

void SessionOwner::requestConnect(const ConnectionRequest& command) {
  if (command.livekit_url().empty() || command.livekit_token().empty()) {
    return;
  }
  livekit_connect_requested_.store(true);
  livekit_executor_.add([this, url = command.livekit_url(), token = command.livekit_token()] {
    if (room_connected_.load() && active_livekit_url_ == url && active_livekit_token_ == token) {
      return;
    }
    disconnectOnExecutor();
    auto consumer = std::make_shared<SynchronizedDataConsumer>();
    try {
      Configuration session_config = config_;
      session_config.livekit_url = url;
      session_config.livekit_token = token;
      room_ = std::make_unique<RoomSession>(
          std::move(session_config), livekit_executor_,
          [consumer](const std::vector<std::uint8_t>& bytes, std::optional<std::uint64_t> timestamp) {
            consumer->consume(bytes, timestamp);
          },
          [consumer] { consumer->resetForNewSession(); },
          [this](bool enabled, const std::string& destination) {
            if (room_) {
              room_->publishStatus(StatusMessage{enabled}, destination);
            }
          },
          [this](livekit::DisconnectReason) {
            livekit_connect_requested_.store(false);
            disconnectOnExecutor();
          });
    } catch (const std::exception& error) {
      const std::scoped_lock<std::mutex> lock(error_mutex_);
      last_error_ = error.what();
      return;
    }
    data_consumer_ = std::move(consumer);
    active_livekit_url_ = url;
    active_livekit_token_ = token;
    room_connected_.store(true);
  });
}

void SessionOwner::requestDisconnect() {
  livekit_connect_requested_.store(false);
  livekit_executor_.add([this] { disconnectOnExecutor(); });
}

void SessionOwner::disconnectOnExecutor() {
  room_connected_.store(false);
  active_livekit_url_.clear();
  active_livekit_token_.clear();
  if (data_consumer_) {
    data_consumer_->disable();
  }
  room_.reset();
  data_consumer_.reset();
}

void SessionOwner::pushMedia(int frame_count) {
  livekit_executor_
      .submit([this, frame_count] {
        if (!room_) {
          throw std::runtime_error("room is not connected");
        }
        for (int frame_number = 0; frame_number < frame_count; ++frame_number) {
          AudioChunk audio;
          audio.samples.resize(static_cast<std::size_t>(audio.channels) *
                               static_cast<std::size_t>(audio.samples_per_channel));
          room_->pushAudio(audio);
          for (const auto& spec : config_.video_streams) {
            VideoFrame video;
            video.width = spec.width;
            video.height = spec.height;
            video.data.resize(static_cast<std::size_t>(spec.width * spec.height * 3 / 2));
            const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count();
            room_->pushFrame(spec.id, std::move(video), timestamp);
          }
          if (config_.enable_receiver && room_->hasDecodedFrames()) {
            break;
          }
          std::this_thread::sleep_for(33ms);
        }
        if (config_.enable_receiver && !room_->waitForDecodedFrames()) {
          throw std::runtime_error("receiver did not decode every CUDA video stream");
        }
        std::this_thread::sleep_for(config_.media_drain_period);
      })
      .get();
}

std::string SessionOwner::lastError() const {
  const std::scoped_lock<std::mutex> lock(error_mutex_);
  return last_error_;
}

SessionOwner::~SessionOwner() {
  try {
    livekit_executor_.add([this] { disconnectOnExecutor(); });
  } catch (const std::exception& error) {
    std::clog << "failed to enqueue final disconnect: " << error.what() << '\n';
  }
  livekit_executor_.join();
}

const char* requireEnvironment(const char* name) {
  const char* value = std::getenv(name);
  if (!value || !*value) {
    throw std::runtime_error(std::string(name) + " is not set; source scripts/set-test-tokens.sh");
  }
  return value;
}

int parsePositiveInt(const char* value, const char* option) {
  try {
    std::size_t parsed_chars = 0;
    const int parsed = std::stoi(value, &parsed_chars);
    if (parsed_chars != std::strlen(value) || parsed <= 0) {
      throw std::runtime_error("");
    }
    return parsed;
  } catch (...) {
    throw std::runtime_error(std::string(option) + " requires a positive integer");
  }
}

ProcessSample processSample() {
  ProcessSample sample;
#if defined(__APPLE__)
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
    sample.rss_kib = static_cast<std::uint64_t>(info.resident_size) / 1024;
  }
  thread_act_array_t thread_list = nullptr;
  mach_msg_type_number_t thread_count = 0;
  if (task_threads(mach_task_self(), &thread_list, &thread_count) == KERN_SUCCESS) {
    sample.threads = thread_count;
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(thread_list),
                  static_cast<vm_size_t>(thread_count) * sizeof(thread_t));
  }
#elif defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
    sample.rss_kib = static_cast<std::uint64_t>(counters.WorkingSetSize) / 1024;
  }
#else
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    std::istringstream fields(line);
    std::string name;
    fields >> name;
    if (name == "VmRSS:") {
      fields >> sample.rss_kib;
    } else if (name == "Threads:") {
      std::uint64_t threads = 0;
      fields >> threads;
      sample.threads = threads;
    }
  }
  std::ifstream smaps_rollup("/proc/self/smaps_rollup");
  while (std::getline(smaps_rollup, line)) {
    std::istringstream fields(line);
    std::string name;
    std::uint64_t value = 0;
    fields >> name >> value;
    if (name == "Pss:") {
      sample.pss_kib = value;
    } else if (name == "Private_Dirty:") {
      sample.private_dirty_kib = value;
    }
  }
  if (DIR* directory = opendir("/proc/self/fd")) {
    std::uint64_t descriptors = 0;
    while (readdir(directory) != nullptr) {
      ++descriptors;
    }
    closedir(directory);
    // Exclude ".", "..", and the directory descriptor used for enumeration.
    sample.file_descriptors = descriptors >= 3 ? descriptors - 3 : 0;
  }
#endif
  return sample;
}

void printUsage(const char* executable) {
  std::cerr << "usage: " << executable
            << " [--iterations N] [--video-streams N] [--frames N] [--settle-ms N]\n"
               "       [--receiver] [--data-track] [--control-packets] [--log-callback] [--malloc-trim]\n";
}

} // namespace

int main(int argc, char* argv[]) {
  try {
    int iterations = 100;
    int stream_count = 3;
    int frame_count = 90;
    int settle_ms = 1000;
    bool trim_allocator = false;
    Configuration config;
    for (int argument = 1; argument < argc; ++argument) {
      const std::string option = argv[argument];
      if (option == "--receiver") {
        config.enable_receiver = true;
        continue;
      }
      if (option == "--data-track") {
        config.enable_data_track = true;
        continue;
      }
      if (option == "--control-packets") {
        config.enable_control_packets = true;
        continue;
      }
      if (option == "--log-callback") {
        config.enable_log_callback = true;
        continue;
      }
      if (option == "--malloc-trim") {
        trim_allocator = true;
        continue;
      }
      if (argument + 1 >= argc) {
        throw std::runtime_error(std::string(argv[argument]) + " requires a value");
      }
      const char* value = argv[++argument];
      if (option == "--iterations") {
        iterations = parsePositiveInt(value, "--iterations");
      } else if (option == "--video-streams") {
        stream_count = parsePositiveInt(value, "--video-streams");
      } else if (option == "--frames") {
        frame_count = parsePositiveInt(value, "--frames");
      } else if (option == "--settle-ms") {
        settle_ms = parsePositiveInt(value, "--settle-ms");
      } else {
        throw std::runtime_error("unknown option: " + option);
      }
    }

    if ((config.enable_data_track || config.enable_control_packets) && !config.enable_receiver) {
      throw std::runtime_error("--data-track and --control-packets require --receiver");
    }
    config.livekit_url = requireEnvironment("LIVEKIT_URL");
    config.livekit_token = requireEnvironment("LIVEKIT_TOKEN_A");
    config.receiver_token = requireEnvironment("LIVEKIT_TOKEN_B");
    for (int stream = 0; stream < stream_count; ++stream) {
      config.video_streams.push_back(
          StreamConfig{static_cast<StreamId>(stream), "lifecycle-video-" + std::to_string(stream)});
    }

    SessionOwner owner(config);
    std::cout << "iteration,rss_kib,pss_kib,private_dirty_kib,threads,file_descriptors\n";
    for (int iteration = 1; iteration <= iterations; ++iteration) {
      owner.requestConnect(ConnectionRequest{config.livekit_url, config.livekit_token});
      owner.flush();
      if (!owner.connected()) {
        throw std::runtime_error("connect failed: " + owner.lastError());
      }
      owner.pushMedia(frame_count);
      owner.requestDisconnect();
      owner.flush();
      std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
#if !defined(__APPLE__) && !defined(_WIN32)
      if (trim_allocator) {
        (void)malloc_trim(0);
      }
#else
      (void)trim_allocator;
#endif
      const ProcessSample sample = processSample();
      std::cout << iteration << ',' << sample.rss_kib << ',';
      if (sample.pss_kib) {
        std::cout << *sample.pss_kib;
      }
      std::cout << ',';
      if (sample.private_dirty_kib) {
        std::cout << *sample.private_dirty_kib;
      }
      std::cout << ',';
      if (sample.threads) {
        std::cout << *sample.threads;
      }
      std::cout << ',';
      if (sample.file_descriptors) {
        std::cout << *sample.file_descriptors;
      }
      std::cout << '\n';
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "lifecycle reproducer failed: " << error.what() << '\n';
    printUsage(argv[0]);
    return 1;
  }
}
