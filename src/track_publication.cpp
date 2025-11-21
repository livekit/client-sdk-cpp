#include "livekit/track_publication.h"

namespace livekit {

TrackPublication::TrackPublication(
    FfiHandle handle, std::string sid, std::string name, TrackKind kind,
    TrackSource source, bool simulcasted, std::uint32_t width,
    std::uint32_t height, std::string mime_type, bool muted,
    EncryptionType encryption_type,
    std::vector<AudioTrackFeature> audio_features)
    : handle_(std::move(handle)), sid_(std::move(sid)), name_(std::move(name)),
      kind_(kind), source_(source), simulcasted_(simulcasted), width_(width),
      height_(height), mime_type_(std::move(mime_type)), muted_(muted),
      encryption_type_(encryption_type),
      audio_features_(std::move(audio_features)) {}

} // namespace livekit
