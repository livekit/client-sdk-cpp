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

#pragma once

#include "livekit/ffi_handle.h"
#include "livekit/participant.h"
#include "livekit/room_delegate.h"
#include "livekit/rpc_error.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace livekit {

struct ParticipantTrackPermission;

class FfiClient;
class Track;
class LocalTrackPublication;
// TODO, should consider moving Transcription to local_participant.h?
struct Transcription;

struct RpcInvocationData {
  std::string request_id;
  std::string caller_identity;
  std::string payload;
  double response_timeout_sec; // seconds
};

/**
 * Represents the local participant in a room.
 *
 * LocalParticipant, built on top of the participant.h base class.
 */
class LocalParticipant : public Participant {
public:
  using PublicationMap =
      std::unordered_map<std::string, std::shared_ptr<LocalTrackPublication>>;

  /**
   * Type of callback used to handle incoming RPC method invocations.
   *
   * The handler receives an RpcInvocationData describing the incoming call
   * and may return an optional response payload. To signal an error to the
   * remote caller, throw an RpcError; it will be serialized and forwarded.
   *
   * Returning std::nullopt means "no payload" and results in an empty
   * response body being sent back to the caller.
   */
  using RpcHandler =
      std::function<std::optional<std::string>(const RpcInvocationData &)>;

  LocalParticipant(FfiHandle handle, std::string sid, std::string name,
                   std::string identity, std::string metadata,
                   std::unordered_map<std::string, std::string> attributes,
                   ParticipantKind kind, DisconnectReason reason);

  /// Track publications associated with this participant, keyed by track SID.
  const PublicationMap &trackPublications() const noexcept {
    return track_publications_;
  }

  /**
   * Publish arbitrary data to the room.
   *
   * @param payload                Raw bytes to send.
   * @param reliable               Whether to send reliably or not.
   * @param destination_identities Optional list of participant identities.
   * @param topic                  Optional topic string.
   *
   * Throws std::runtime_error if FFI reports an error (if you wire that up).
   */
  void publishData(const std::vector<std::uint8_t> &payload,
                   bool reliable = true,
                   const std::vector<std::string> &destination_identities = {},
                   const std::string &topic = {});

  /**
   * Publish SIP DTMF message.
   */
  void publishDtmf(int code, const std::string &digit);

  /**
   * Publish transcription data to the room.
   *
   * @param transcription
   */
  void publishTranscription(const Transcription &transcription);

  // -------------------------------------------------------------------------
  // Metadata APIs (set metadata / name / attributes)
  // -------------------------------------------------------------------------

  void setMetadata(const std::string &metadata);
  void setName(const std::string &name);
  void
  setAttributes(const std::unordered_map<std::string, std::string> &attributes);

  /**
   * Set track subscription permissions for this participant.
   *
   * @param allow_all_participants If true, all participants may subscribe.
   * @param participant_permissions Optional participant-specific permissions.
   */
  void
  setTrackSubscriptionPermissions(bool allow_all_participants,
                                  const std::vector<ParticipantTrackPermission>
                                      &participant_permissions = {});

  /**
   * Publish a local track to the room.
   *
   * Throws std::runtime_error on error (e.g. publish failure).
   */
  std::shared_ptr<LocalTrackPublication>
  publishTrack(const std::shared_ptr<Track> &track,
               const TrackPublishOptions &options);

  /**
   * Unpublish a track from the room by SID.
   *
   * If the publication exists in the local map, it is removed.
   */
  void unpublishTrack(const std::string &track_sid);

  /**
   * Initiate an RPC call to a remote participant.
   *
   * @param destination_identity  Identity of the destination participant.
   * @param method                Name of the RPC method to invoke.
   * @param payload               Request payload to send to the remote handler.
   * @param response_timeout      Optional timeout in seconds for receiving
   *                              a response. If not set, the server default
   *                              timeout (15 seconds) is used.
   *
   * @return The response payload returned by the remote handler.
   *
   * @throws RpcError   If the remote side returns an RPC error, times out,
   *                    or rejects the request.
   * @throws std::runtime_error If the underlying FFI handle is invalid or
   *                             the FFI call fails unexpectedly.
   */
  std::string performRpc(const std::string &destination_identity,
                         const std::string &method, const std::string &payload,
                         std::optional<double> response_timeout = std::nullopt);

  /**
   * Register a handler for an incoming RPC method.
   *
   * Once registered, the provided handler will be invoked whenever a remote
   * participant calls the given method name on this LocalParticipant.
   *
   * @param method_name  Name of the RPC method to handle. This must match
   *                     the method name used by remote callers.
   * @param handler      Callback to execute when an invocation is received.
   *                     The handler may return an optional response payload
   *                     or throw an RpcError to signal failure.
   *
   * If a handler is already registered for the same method_name, it will be
   * replaced by the new handler.
   */

  void registerRpcMethod(const std::string &method_name, RpcHandler handler);

  /**
   * Unregister a previously registered RPC method handler.
   *
   * After this call, invocations for the given method_name will no longer
   * be dispatched to a local handler and will instead result in an
   * "unsupported method" error being returned to the caller.
   *
   * @param method_name  Name of the RPC method to unregister.
   *                     If no handler is registered for this name, the call
   *                     is a no-op.
   */
  void unregisterRpcMethod(const std::string &method_name);

protected:
  // Called by Room when an rpc_method_invocation event is received from the
  // SFU. This is internal plumbing and not intended to be called directly by
  // SDK users.
  void handleRpcMethodInvocation(std::uint64_t invocation_id,
                                 const std::string &method,
                                 const std::string &request_id,
                                 const std::string &caller_identity,
                                 const std::string &payload,
                                 double response_timeout);
  friend class Room;

private:
  PublicationMap track_publications_;
  std::unordered_map<std::string, RpcHandler> rpc_handlers_;
};

} // namespace livekit
