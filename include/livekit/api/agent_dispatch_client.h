/*
 * Copyright 2026 LiveKit, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "livekit/result.h"
#include "livekit/visibility.h"

/// @brief LiveKit server API clients.
///
/// These clients call LiveKit server APIs over HTTPS. They are not part of
/// `livekit/livekit.h`; include the header of each client you use.
namespace livekit::api {

/// @brief Whether a failed job restarts. Cloud only.
enum class JobRestartPolicy {
  OnFailure = 0, ///< Restart the job when it fails (default).
  Never = 1,     ///< Never restart the job.
};

/// @brief How a dispatch assigns jobs to agents.
enum class JobType {
  Room = 0,        ///< One job for the room.
  Publisher = 1,   ///< One job for each publisher.
  Participant = 2, ///< One job for each participant.
};

/// @brief Run state of a job.
enum class JobStatus {
  Pending = 0,   ///< Created, not started.
  Running = 1,   ///< Running.
  Success = 2,   ///< Completed.
  Failed = 3,    ///< Failed.
  Cancelled = 4, ///< Ended before an agent took it.
};

/// @brief Runtime status of one job.
struct JobState {
  JobStatus status = JobStatus::Pending;
  std::string error;
  std::int64_t started_at = 0; ///< Unix seconds.
  std::int64_t ended_at = 0;   ///< Unix seconds. Zero while the job runs.
  std::int64_t updated_at = 0; ///< Unix seconds.
  std::string participant_identity;
  std::string worker_id;
  std::string agent_id;
};

/// @brief One agent job created by a dispatch.
struct Job {
  std::string id;
  std::string dispatch_id;
  JobType type = JobType::Room;
  std::string room_name;
  std::string room_sid;
  std::optional<std::string> participant_identity; ///< Set for publisher and participant jobs.
  std::string metadata;
  std::string agent_name;
  std::optional<JobState> state;
  bool enable_recording = false;
  std::string deployment;
  std::map<std::string, std::string> attributes;
};

/// @brief Jobs and timestamps of a dispatch.
struct AgentDispatchState {
  std::vector<Job> jobs;
  std::int64_t created_at = 0; ///< Unix seconds.
  std::int64_t deleted_at = 0; ///< Unix seconds. Zero while the dispatch exists.
};

/// @brief An explicit dispatch of a named agent into a room.
struct AgentDispatch {
  std::string id;
  std::string agent_name;
  std::string room;
  std::string metadata;
  std::optional<AgentDispatchState> state;
  JobRestartPolicy restart_policy = JobRestartPolicy::OnFailure;
  std::string deployment;
  std::map<std::string, std::string> attributes;
};

/// @brief Parameters of @ref AgentDispatchClient::createDispatch.
struct CreateAgentDispatchRequest {
  std::string agent_name; ///< Must match the agent name the agent server registers.
  std::string room;       ///< The server creates the room if it does not exist.
  std::string metadata;   ///< Opaque to LiveKit; passed to the agent job.
  JobRestartPolicy restart_policy = JobRestartPolicy::OnFailure;
  std::string deployment; ///< Empty targets the production deployment.
  std::map<std::string, std::string> attributes;
};

/// @brief Transport configuration of @ref AgentDispatchClient.
struct AgentDispatchClientOptions {
  std::chrono::milliseconds request_timeout{10000}; ///< Timeout of each HTTP request.
  bool failover = true;                             ///< Retry other LiveKit Cloud regions on 5xx or network errors.
};

/// @brief Error returned by @ref AgentDispatchClient calls.
struct AgentDispatchError {
  /// @brief Category of the failure.
  enum class Kind {
    InvalidEnvironment, ///< `LIVEKIT_API_KEY` or `LIVEKIT_API_SECRET` is missing.
    InvalidAccessToken, ///< The request token could not be signed.
    Server,             ///< The server returned a Twirp error; see @ref code.
    Request,            ///< Connection, TLS, timeout, or decode failure.
  };
  Kind kind = Kind::Request;
  std::string code; ///< Twirp error code for @ref Kind::Server, for example `not_found`.
  std::string message;
};

/// @brief Client for the AgentDispatchService server API.
///
/// Every call needs a token with the `roomAdmin` grant for the target room.
/// With an API key and secret, the client signs that token for each request.
/// Calls return a `std::future` and do not block the caller. The client is
/// safe to share between threads. Do not wait on the future inside a
/// @ref livekit::RoomDelegate callback.
class LIVEKIT_API AgentDispatchClient {
public:
  /// @brief Create a client that signs a token per request.
  ///
  /// @param host       LiveKit server URL, for example `https://project.livekit.cloud`.
  /// @param api_key    API key.
  /// @param api_secret API secret.
  /// @param options    Transport configuration.
  AgentDispatchClient(const std::string& host, const std::string& api_key, const std::string& api_secret,
                      const AgentDispatchClientOptions& options = {});

  /// @brief Create a client that sends a pre-signed token on every request.
  ///
  /// @param host    LiveKit server URL.
  /// @param token   Access token with the `roomAdmin` grant.
  /// @param options Transport configuration.
  /// @return The client.
  static AgentDispatchClient withToken(const std::string& host, const std::string& token,
                                       const AgentDispatchClientOptions& options = {});

  /// @brief Create a client from `LIVEKIT_API_KEY` and `LIVEKIT_API_SECRET`.
  ///
  /// @param host    LiveKit server URL.
  /// @param options Transport configuration.
  /// @return The client, or @ref AgentDispatchError::Kind::InvalidEnvironment
  ///         when a variable is not set.
  static Result<AgentDispatchClient, AgentDispatchError> fromEnv(const std::string& host,
                                                                 const AgentDispatchClientOptions& options = {});

  /// @brief Dispatch a named agent into a room.
  ///
  /// @param req Dispatch parameters.
  /// @return The created dispatch, or the error.
  std::future<Result<AgentDispatch, AgentDispatchError>> createDispatch(const CreateAgentDispatchRequest& req) const;

  /// @brief Delete a dispatch.
  ///
  /// @param dispatch_id ID of the dispatch.
  /// @param room_name   Room of the dispatch.
  /// @return The deleted dispatch, or the error.
  std::future<Result<AgentDispatch, AgentDispatchError>> deleteDispatch(const std::string& dispatch_id,
                                                                        const std::string& room_name) const;

  /// @brief List every dispatch in a room.
  ///
  /// @param room_name Room to list.
  /// @return The dispatches, or the error.
  std::future<Result<std::vector<AgentDispatch>, AgentDispatchError>> listDispatch(const std::string& room_name) const;

  /// @brief Get one dispatch.
  ///
  /// @param dispatch_id ID of the dispatch.
  /// @param room_name   Room of the dispatch.
  /// @return The dispatch, `std::nullopt` when the room has no dispatch with
  ///         that ID, or the error.
  std::future<Result<std::optional<AgentDispatch>, AgentDispatchError>> getDispatch(const std::string& dispatch_id,
                                                                                    const std::string& room_name) const;

private:
  struct Impl;
  explicit AgentDispatchClient(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;
};

} // namespace livekit::api
