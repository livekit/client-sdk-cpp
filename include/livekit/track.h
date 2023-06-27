/*
 * Copyright 2023 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the “License”);
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

#ifndef LIVEKIT_TRACK_H
#define LIVEKIT_TRACK_H

#include "livekit/ffi_client.h"
#include "track.pb.h"
#include "video_source.h"

namespace livekit {
class Track {
public:
    // Use a move constructor to avoid copying the parameters
    Track(FfiHandle&& ffiHandle, TrackInfo&& info) : ffiHandle_(std::move(ffiHandle)), info_(std::move(info)) {}
    Track(const FfiHandle& ffiHandle, const TrackInfo& info) : ffiHandle_(ffiHandle), info_(info) {}

    // For dynamic_cast to work, we need a virtual function
    virtual ~Track() {}

    std::string GetSid() const {
        return info_.sid();
    }

    std::string GetName() const {
        return info_.name();
    }

    TrackKind Getkind() const {
        return info_.kind();
    }

    StreamState GetStreamState() const {
        return info_.stream_state();
    }

    bool IsMuted() const {
        return info_.muted();
    }

    void SetUpdateInfo(const TrackInfo& info) {
        info_ = info;
    }

    FfiHandle GetHandle() const {
        return ffiHandle_;
    }

private:
    TrackInfo info_;
    FfiHandle ffiHandle_;
};


class LocalVideoTrack : public Track {
public:
    // Use a move constructor to avoid copying the parameters
    LocalVideoTrack(FfiHandle&& ffiHandle, TrackInfo&& info) : Track(std::move(ffiHandle), std::move(info)) {}
    LocalVideoTrack(const FfiHandle& ffiHandle, const TrackInfo& info) : Track(ffiHandle, info) {}
    

    static std::unique_ptr<LocalVideoTrack> CreateVideoTrack(const std::string& name, const VideoSource& source);
};

class RemoteVideoTrack : public Track {
    RemoteVideoTrack(const FfiHandle& ffiHandle, const TrackInfo& info): Track(ffiHandle, info) {}
};
}

#endif /* LIVEKIT_TRACK_H */
