
#include "livekit/track.h"

#include <future>
#include <optional>
#include "livekit/ffi_client.h"


 namespace livekit {

Track::Track(std::weak_ptr<FfiHandle> handle,
             std::string sid,
             std::string name,
             TrackKind kind,
             StreamState state,
             bool muted,
             bool remote)
    : handle_(std::move(handle)), sid_(std::move(sid)), name_(std::move(name)),
      kind_(kind), state_(state), muted_(muted), remote_(remote) {
}

void Track::setPublicationFields(std::optional<TrackSource> source,
                                 std::optional<bool> simulcasted,
                                 std::optional<uint32_t> width,
                                 std::optional<uint32_t> height,
                                 std::optional<std::string> mime_type) {
    source_ = source;
    simulcasted_ = simulcasted;
    width_ = width;
    height_ = height;
    mime_type_ = std::move(mime_type);
}

std::future<std::vector<RtcStats>> Track::getStats() const {
    auto id = ffi_handle_id();
    if (!id) {
        // make a ready future with an empty vector
        std::promise<std::vector<RtcStats>> pr;
        pr.set_value({});
        return pr.get_future();
    }

    // just forward the future from FfiClient
    return FfiClient::instance().getTrackStatsAsync(id);
}


} // namespace livekit