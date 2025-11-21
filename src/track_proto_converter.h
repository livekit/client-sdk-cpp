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
 * distributed under the License is distributed on an “AS IS” BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "livekit/participant.h"
#include "livekit/track.h"
#include "livekit/track_publication.h"
#include "participant.pb.h"
#include "track.pb.h"

namespace livekit {

TrackKind fromProto(proto::TrackKind in);
StreamState fromProto(proto::StreamState in);
TrackSource fromProto(proto::TrackSource in);
AudioTrackFeature fromProto(proto::AudioTrackFeature in);
std::vector<AudioTrackFeature>
convertAudioFeatures(const google::protobuf::RepeatedField<int> &features);

// Participant Utils
ParticipantKind fromProto(proto::ParticipantKind kind);
proto::ParticipantTrackPermission toProto(const ParticipantTrackPermission &in);
ParticipantTrackPermission
fromProto(const proto::ParticipantTrackPermission &in);

// Track Publication Utils.
EncryptionType fromProto(proto::EncryptionType in);

} // namespace livekit