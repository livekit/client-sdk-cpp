#include "livekit/data_track_frame.h"

#include "data_track.pb.h"

namespace livekit {

DataTrackFrame
DataTrackFrame::fromOwnedInfo(const proto::DataTrackFrame &owned) {
  DataTrackFrame frame;
  const auto &payload_str = owned.payload();
  frame.payload.assign(
      reinterpret_cast<const std::uint8_t *>(payload_str.data()),
      reinterpret_cast<const std::uint8_t *>(payload_str.data()) +
          payload_str.size());
  if (owned.has_user_timestamp()) {
    frame.user_timestamp = owned.user_timestamp();
  }
  return frame;
}

} // namespace livekit
