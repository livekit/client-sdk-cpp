#include "livekit/local_track_publication.h"

#include "livekit/track.h"
#include "track_proto_converter.h"

namespace livekit {

LocalTrackPublication::LocalTrackPublication(
    const proto::OwnedTrackPublication &owned)
    : TrackPublication(
          FfiHandle(owned.handle().id()), owned.info().sid(),
          owned.info().name(), fromProto(owned.info().kind()),
          fromProto(owned.info().source()), owned.info().simulcasted(),
          owned.info().width(), owned.info().height(), owned.info().mime_type(),
          owned.info().muted(), fromProto(owned.info().encryption_type()),
          convertAudioFeatures(owned.info().audio_features())) {}

std::shared_ptr<Track> LocalTrackPublication::track() const noexcept {
  auto base = TrackPublication::track();
  return std::static_pointer_cast<Track>(base);
}

} // namespace livekit
