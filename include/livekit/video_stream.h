#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

#include "ffi_handle.h"
#include "participant.h"
#include "track.h"
#include "video_frame.h"

namespace livekit {

// C++ equivalent of Python VideoFrameEvent
struct VideoFrameEvent {
  VideoFrame frame;
  std::int64_t timestamp_us;
  VideoRotation rotation;
};

class VideoStream {
public:
  struct Options {
    std::size_t capacity{0};               // 0 = unbounded
    std::optional<VideoBufferType> format; // optional pixel format
  };

  // Factory: create a VideoStream bound to a specific Track
  static std::unique_ptr<VideoStream>
  fromTrack(const std::shared_ptr<Track> &track,
            const Options &options = Options{});

  // Factory: create a VideoStream from a Participant + TrackSource
  static std::unique_ptr<VideoStream>
  fromParticipant(Participant &participant, TrackSource track_source,
                  const Options &options = Options{});

  ~VideoStream();

  VideoStream(const VideoStream &) = delete;
  VideoStream &operator=(const VideoStream &) = delete;
  VideoStream(VideoStream &&) noexcept;
  VideoStream &operator=(VideoStream &&) noexcept;

  /// Blocking read: returns true if a frame was delivered,
  /// false if the stream has ended (EOS or closed).
  bool read(VideoFrameEvent &out);

  /// Signal that we are no longer interested in frames.
  /// Disposes the underlying FFI stream and drains internal listener.
  void close();

private:
  VideoStream() = default;

  // Internal init helpers, used by the factories
  void initFromTrack(const std::shared_ptr<Track> &track,
                     const Options &options);
  void initFromParticipant(Participant &participant, TrackSource source,
                           const Options &options);

  // FFI event handler (registered with FfiClient)
  void onFfiEvent(const FfiEvent &event);

  // Queue helpers
  void pushFrame(VideoFrameEvent &&ev);
  void pushEos();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<VideoFrameEvent> queue_;
  std::size_t capacity_{0};
  bool eof_{false};
  bool closed_{false};

  // Underlying FFI handle for the video stream
  FfiHandle stream_handle_;

  // Listener id registered on FfiClient
  std::int64_t listener_id_{0};
};

} // namespace livekit
