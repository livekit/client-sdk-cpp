#include "livekit/remote_track_publication.h"

#include "ffi.pb.h"
#include "livekit/ffi_client.h"
#include "livekit/track.h"
#include "track_proto_converter.h"

namespace livekit {

RemoteTrackPublication::RemoteTrackPublication(
    const proto::OwnedTrackPublication &owned)
    : TrackPublication(
          FfiHandle(owned.handle().id()), owned.info().sid(),
          owned.info().name(), fromProto(owned.info().kind()),
          fromProto(owned.info().source()), owned.info().simulcasted(),
          owned.info().width(), owned.info().height(), owned.info().mime_type(),
          owned.info().muted(), fromProto(owned.info().encryption_type()),
          convertAudioFeatures(owned.info().audio_features())) {}

std::shared_ptr<Track> RemoteTrackPublication::track() const noexcept {
  auto base = TrackPublication::track();
  return std::static_pointer_cast<Track>(base);
}

void RemoteTrackPublication::setSubscribed(bool subscribed) {
  if (ffiHandleId() == 0) {
    throw std::runtime_error(
        "RemoteTrackPublication::setSubscribed: invalid FFI handle");
  }

  proto::FfiRequest req;
  auto *msg = req.mutable_set_subscribed();
  msg->set_subscribe(subscribed);
  msg->set_publication_handle(static_cast<std::uint64_t>(ffiHandleId()));

  // Synchronous request; if you add an async version in FfiClient, you can
  // wire that up instead.
  auto resp = FfiClient::instance().sendRequest(req);
  (void)resp; // currently unused, but you can inspect error fields here

  subscribed_ = subscribed;
}

} // namespace livekit
