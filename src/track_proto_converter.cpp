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

#include "track_proto_converter.h"

#include <vector>

namespace livekit {

proto::ParticipantTrackPermission
toProto(const ParticipantTrackPermission &in) {
  proto::ParticipantTrackPermission out;
  out.set_participant_identity(in.participant_identity);
  if (in.allow_all.has_value()) {
    out.set_allow_all(*in.allow_all);
  }
  for (const auto &sid : in.allowed_track_sids) {
    out.add_allowed_track_sids(sid);
  }
  return out;
}

ParticipantTrackPermission
fromProto(const proto::ParticipantTrackPermission &in) {
  ParticipantTrackPermission out;
  out.participant_identity = in.participant_identity();
  if (in.has_allow_all()) {
    out.allow_all = in.allow_all();
  } else {
    out.allow_all = std::nullopt;
  }
  out.allowed_track_sids.reserve(in.allowed_track_sids_size());
  for (const auto &sid : in.allowed_track_sids()) {
    out.allowed_track_sids.push_back(sid);
  }
  return out;
}

TrackKind fromProto(proto::TrackKind in) {
  switch (in) {
  case proto::TrackKind::KIND_AUDIO:
    return TrackKind::KIND_AUDIO;
  case proto::TrackKind::KIND_VIDEO:
    return TrackKind::KIND_VIDEO;
  case proto::TrackKind::KIND_UNKNOWN:
    return TrackKind::KIND_UNKNOWN;
  default:
    return TrackKind::KIND_UNKNOWN;
  }
}

StreamState fromProto(proto::StreamState in) {
  switch (in) {
  case proto::StreamState::STATE_ACTIVE:
    return StreamState::STATE_ACTIVE;
  case proto::StreamState::STATE_PAUSED:
    return StreamState::STATE_PAUSED;
  case proto::StreamState::STATE_UNKNOWN:
    return StreamState::STATE_UNKNOWN;
  default:
    return StreamState::STATE_UNKNOWN;
  }
}

TrackSource fromProto(proto::TrackSource in) {
  switch (in) {
  case proto::TrackSource::SOURCE_CAMERA:
    return TrackSource::SOURCE_CAMERA;
  case proto::TrackSource::SOURCE_MICROPHONE:
    return TrackSource::SOURCE_MICROPHONE;
  case proto::TrackSource::SOURCE_SCREENSHARE:
    return TrackSource::SOURCE_SCREENSHARE;
  case proto::TrackSource::SOURCE_SCREENSHARE_AUDIO:
    return TrackSource::SOURCE_SCREENSHARE_AUDIO;
  case proto::TrackSource::SOURCE_UNKNOWN:
    return TrackSource::SOURCE_UNKNOWN;
  default:
    return TrackSource::SOURCE_UNKNOWN;
  }
}

AudioTrackFeature fromProto(proto::AudioTrackFeature in) {
  switch (in) {
  case proto::TF_STEREO:
    return AudioTrackFeature::TF_STEREO;
  case proto::TF_NO_DTX:
    return AudioTrackFeature::TF_NO_DTX;
  case proto::TF_AUTO_GAIN_CONTROL:
    return AudioTrackFeature::TF_AUTO_GAIN_CONTROL;
  case proto::TF_ECHO_CANCELLATION:
    return AudioTrackFeature::TF_ECHO_CANCELLATION;
  case proto::TF_NOISE_SUPPRESSION:
    return AudioTrackFeature::TF_NOISE_SUPPRESSION;
  case proto::TF_ENHANCED_NOISE_CANCELLATION:
    return AudioTrackFeature::TF_ENHANCED_NOISE_CANCELLATION;
  case proto::TF_PRECONNECT_BUFFER:
    return AudioTrackFeature::TF_PRECONNECT_BUFFER;
  default:
    // Defensive fallback – pick something valid instead of UB.
    return AudioTrackFeature::TF_STEREO;
  }
}

std::vector<AudioTrackFeature>
convertAudioFeatures(const google::protobuf::RepeatedField<int> &features) {
  std::vector<AudioTrackFeature> out;
  out.reserve(features.size());
  for (int v : features) {
    out.push_back(fromProto(static_cast<proto::AudioTrackFeature>(v)));
  }
  return out;
}

ParticipantKind fromProto(proto::ParticipantKind in) {
  switch (in) {
  case proto::ParticipantKind::PARTICIPANT_KIND_STANDARD:
    return ParticipantKind::Standard;
  case proto::ParticipantKind::PARTICIPANT_KIND_INGRESS:
    return ParticipantKind::Ingress;
  case proto::ParticipantKind::PARTICIPANT_KIND_EGRESS:
    return ParticipantKind::Egress;
  case proto::ParticipantKind::PARTICIPANT_KIND_SIP:
    return ParticipantKind::Sip;
  case proto::ParticipantKind::PARTICIPANT_KIND_AGENT:
    return ParticipantKind::Agent;
  default:
    return ParticipantKind::Standard;
  }
}

EncryptionType fromProto(proto::EncryptionType in) {
  switch (in) {
  case proto::NONE:
    return EncryptionType::NONE;
  case proto::GCM:
    return EncryptionType::GCM;
  case proto::CUSTOM:
    return EncryptionType::CUSTOM;
  default:
    // Defensive fallback
    return EncryptionType::NONE;
  }
}

} // namespace livekit