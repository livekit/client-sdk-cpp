#include "livekit/video_stream.h"

#include <utility>

#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/track.h"
#include "video_frame.pb.h"
#include "video_utils.h"

namespace livekit {

using proto::FfiEvent;
using proto::FfiRequest;
using proto::VideoStreamEvent;

std::shared_ptr<VideoStream>
VideoStream::fromTrack(const std::shared_ptr<Track> &track,
                       const Options &options) {
  auto stream = std::shared_ptr<VideoStream>(new VideoStream());
  stream->initFromTrack(track, options);
  return stream;
}

std::shared_ptr<VideoStream>
VideoStream::fromParticipant(Participant &participant, TrackSource track_source,
                             const Options &options) {
  auto stream = std::shared_ptr<VideoStream>(new VideoStream());
  stream->initFromParticipant(participant, track_source, options);
  return stream;
}

VideoStream::~VideoStream() { close(); }

VideoStream::VideoStream(VideoStream &&other) noexcept {
  std::lock_guard<std::mutex> lock(other.mutex_);
  queue_ = std::move(other.queue_);
  capacity_ = other.capacity_;
  eof_ = other.eof_;
  closed_ = other.closed_;
  stream_handle_ = std::move(other.stream_handle_);
  listener_id_ = other.listener_id_;

  other.listener_id_ = 0;
  other.closed_ = true;
}

VideoStream &VideoStream::operator=(VideoStream &&other) noexcept {
  if (this == &other)
    return *this;

  close();

  {
    std::lock_guard<std::mutex> lock_this(mutex_);
    std::lock_guard<std::mutex> lock_other(other.mutex_);

    queue_ = std::move(other.queue_);
    capacity_ = other.capacity_;
    eof_ = other.eof_;
    closed_ = other.closed_;
    stream_handle_ = std::move(other.stream_handle_);
    listener_id_ = other.listener_id_;

    other.listener_id_ = 0;
    other.closed_ = true;
  }

  return *this;
}

// --------------------- Public API ---------------------

bool VideoStream::read(VideoFrameEvent &out) {
  std::unique_lock<std::mutex> lock(mutex_);

  cv_.wait(lock, [this] { return !queue_.empty() || eof_ || closed_; });

  if (closed_ || (queue_.empty() && eof_)) {
    return false; // EOS / closed
  }

  out = std::move(queue_.front());
  queue_.pop_front();
  return true;
}

void VideoStream::close() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return;
    }
    closed_ = true;
  }

  // Dispose FFI handle
  if (stream_handle_.get() != 0) {
    stream_handle_.reset();
  }

  // Remove listener
  if (listener_id_ != 0) {
    FfiClient::instance().RemoveListener(listener_id_);
    listener_id_ = 0;
  }

  // Wake any waiting readers
  cv_.notify_all();
}

// --------------------- Internal helpers ---------------------

void VideoStream::initFromTrack(const std::shared_ptr<Track> &track,
                                const Options &options) {
  capacity_ = options.capacity;

  // Subscribe to FFI events, this is essential to get video frames from FFI.
  listener_id_ = FfiClient::instance().AddListener(
      [this](const proto::FfiEvent &e) { this->onFfiEvent(e); });

  // Send FFI request to create a new video stream bound to this track
  FfiRequest req;
  auto *new_video_stream = req.mutable_new_video_stream();
  new_video_stream->set_track_handle(
      static_cast<uint64_t>(track->ffi_handle_id()));
  new_video_stream->set_type(proto::VideoStreamType::VIDEO_STREAM_NATIVE);
  new_video_stream->set_normalize_stride(true);
  new_video_stream->set_format(toProto(options.format));

  auto resp = FfiClient::instance().sendRequest(req);
  if (!resp.has_new_video_stream()) {
    std::cerr << "VideoStream::initFromTrack: FFI response missing "
                 "new_video_stream()\n";
    throw std::runtime_error("new_video_stream FFI request failed");
  }
  // Adjust field names to match your proto exactly:
  const auto &stream = resp.new_video_stream().stream();
  stream_handle_ = FfiHandle(static_cast<uintptr_t>(stream.handle().id()));
  // TODO, do we need to cache the metadata from stream.info ?
}

void VideoStream::initFromParticipant(Participant &participant,
                                      TrackSource track_source,
                                      const Options &options) {
  capacity_ = options.capacity;

  // 1) Subscribe to FFI events
  listener_id_ = FfiClient::instance().AddListener(
      [this](const FfiEvent &e) { this->onFfiEvent(e); });

  // 2) Send FFI request to create a video stream from participant + track
  // source
  FfiRequest req;
  auto *vs = req.mutable_video_stream_from_participant();
  vs->set_participant_handle(participant.ffiHandleId());
  vs->set_type(proto::VideoStreamType::VIDEO_STREAM_NATIVE);
  vs->set_track_source(static_cast<proto::TrackSource>(track_source));
  vs->set_normalize_stride(true);
  vs->set_format(toProto(options.format));

  auto resp = FfiClient::instance().sendRequest(req);
  // Adjust field names to match your proto exactly:
  const auto &stream = resp.video_stream_from_participant().stream();
  stream_handle_ = FfiHandle(static_cast<uintptr_t>(stream.handle().id()));
}

void VideoStream::onFfiEvent(const proto::FfiEvent &event) {
  // Filter for video_stream_event first.
  if (event.message_case() != FfiEvent::kVideoStreamEvent) {
    return;
  }
  const auto &vse = event.video_stream_event();
  // Check if this event is for our stream handle.
  if (vse.stream_handle() != static_cast<std::uint64_t>(stream_handle_.get())) {
    return;
  }
  // Handle frame_received or eos.
  if (vse.has_frame_received()) {
    const auto &fr = vse.frame_received();

    // Convert owned buffer->VideoFrame via a helper.
    // You should implement this static function in your VideoFrame class.
    LKVideoFrame frame = LKVideoFrame::fromOwnedInfo(fr.buffer());

    VideoFrameEvent ev{std::move(frame), fr.timestamp_us(),
                       static_cast<VideoRotation>(fr.rotation())};
    pushFrame(std::move(ev));
  } else if (vse.has_eos()) {
    pushEos();
  }
}

void VideoStream::pushFrame(VideoFrameEvent &&ev) {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_ || eof_) {
      return;
    }

    if (capacity_ > 0 && queue_.size() >= capacity_) {
      // Ring behavior: drop oldest frame.
      queue_.pop_front();
    }

    queue_.push_back(std::move(ev));
  }
  cv_.notify_one();
}

void VideoStream::pushEos() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (eof_) {
      return;
    }
    eof_ = true;
  }
  cv_.notify_all();
}

} // namespace livekit
