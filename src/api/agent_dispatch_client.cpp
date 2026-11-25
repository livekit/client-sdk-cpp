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

#include "livekit/api/agent_dispatch_client.h"

#include <exception>
#include <utility>

#include "livekit_ffi.hpp"

namespace livekit::api {

namespace ffi = ::livekit_ffi;
namespace ffi_err = ::livekit_ffi::agent_dispatch_error;

struct AgentDispatchClient::Impl {
  std::shared_ptr<ffi::AgentDispatchClient> client;
};

namespace {

using Kind = AgentDispatchError::Kind;

std::optional<ffi::AgentDispatchClientOptions> toFfi(const AgentDispatchClientOptions& options) {
  ffi::AgentDispatchClientOptions out;
  // A nonpositive timeout keeps the Rust default instead of failing every request at once.
  if (options.request_timeout.count() > 0) {
    out.request_timeout_ms = static_cast<std::uint64_t>(options.request_timeout.count());
  }
  out.failover = options.failover;
  return out;
}

ffi::JobRestartPolicy toFfi(JobRestartPolicy policy) {
  return policy == JobRestartPolicy::Never ? ffi::JobRestartPolicy::kNever : ffi::JobRestartPolicy::kOnFailure;
}

JobRestartPolicy fromFfi(ffi::JobRestartPolicy policy) {
  return policy == ffi::JobRestartPolicy::kNever ? JobRestartPolicy::Never : JobRestartPolicy::OnFailure;
}

JobType fromFfi(ffi::JobType type) {
  switch (type) {
    case ffi::JobType::kPublisher:
      return JobType::Publisher;
    case ffi::JobType::kParticipant:
      return JobType::Participant;
    case ffi::JobType::kRoom:
      break;
  }
  return JobType::Room;
}

JobStatus fromFfi(ffi::JobStatus status) {
  switch (status) {
    case ffi::JobStatus::kRunning:
      return JobStatus::Running;
    case ffi::JobStatus::kSuccess:
      return JobStatus::Success;
    case ffi::JobStatus::kFailed:
      return JobStatus::Failed;
    case ffi::JobStatus::kCancelled:
      return JobStatus::Cancelled;
    case ffi::JobStatus::kPending:
      break;
  }
  return JobStatus::Pending;
}

std::map<std::string, std::string> fromFfi(const std::unordered_map<std::string, std::string>& map) {
  return {map.begin(), map.end()};
}

JobState fromFfi(const ffi::JobState& s) {
  JobState out;
  out.status = fromFfi(s.status);
  out.error = s.error;
  out.started_at = s.started_at;
  out.ended_at = s.ended_at;
  out.updated_at = s.updated_at;
  out.participant_identity = s.participant_identity;
  out.worker_id = s.worker_id;
  out.agent_id = s.agent_id;
  return out;
}

Job fromFfi(const ffi::Job& j) {
  Job out;
  out.id = j.id;
  out.dispatch_id = j.dispatch_id;
  out.type = fromFfi(j.job_type);
  out.room_name = j.room_name;
  out.room_sid = j.room_sid;
  out.participant_identity = j.participant_identity;
  out.metadata = j.metadata;
  out.agent_name = j.agent_name;
  if (j.state) {
    out.state = fromFfi(*j.state);
  }
  out.enable_recording = j.enable_recording;
  out.deployment = j.deployment;
  out.attributes = fromFfi(j.attributes);
  return out;
}

AgentDispatch fromFfi(const ffi::AgentDispatch& d) {
  AgentDispatch out;
  out.id = d.id;
  out.agent_name = d.agent_name;
  out.room = d.room;
  out.metadata = d.metadata;
  if (d.state) {
    AgentDispatchState state;
    state.jobs.reserve(d.state->jobs.size());
    for (const auto& job : d.state->jobs) {
      state.jobs.push_back(fromFfi(job));
    }
    state.created_at = d.state->created_at;
    state.deleted_at = d.state->deleted_at;
    out.state = std::move(state);
  }
  out.restart_policy = fromFfi(d.restart_policy);
  out.deployment = d.deployment;
  out.attributes = fromFfi(d.attributes);
  return out;
}

// Generated bindings report Rust errors as exceptions; the public API returns them in Result.
AgentDispatchError toError(const std::exception_ptr& error) {
  try {
    std::rethrow_exception(error);
  } catch (const ffi_err::InvalidEnvironment& e) {
    return {Kind::InvalidEnvironment, {}, e.message};
  } catch (const ffi_err::InvalidAccessToken& e) {
    return {Kind::InvalidAccessToken, {}, e.message};
  } catch (const ffi_err::Server& e) {
    return {Kind::Server, e.code, e.message};
  } catch (const ffi_err::Request& e) {
    return {Kind::Request, {}, e.message};
  } catch (const std::exception& e) {
    return {Kind::Request, {}, e.what()};
  } catch (...) {
    return {Kind::Request, {}, "unknown error"};
  }
}

// Completes a std::future from a generated future without blocking a C++ thread. The
// continuation runs on the UniFFI executor thread, so it only converts and stores the result.
template <typename Out, typename Start, typename Convert>
std::future<Result<Out, AgentDispatchError>> bridge(Start start, Convert convert) {
  using R = Result<Out, AgentDispatchError>;
  auto promise = std::make_shared<std::promise<R>>();
  auto future = promise->get_future();
  try {
    auto call = start();
    using Completed = typename decltype(call)::Result;
    // Detach: the request keeps running when the caller drops the std::future.
    std::move(call)
        .then([promise, convert](Completed completed) {
          try {
            promise->set_value(R::success(convert(std::move(completed).get())));
          } catch (...) {
            promise->set_value(R::failure(toError(std::current_exception())));
          }
        })
        .detach();
  } catch (...) {
    promise->set_value(R::failure(toError(std::current_exception())));
  }
  return future;
}

} // namespace

AgentDispatchClient::AgentDispatchClient(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

AgentDispatchClient::AgentDispatchClient(const std::string& host, const std::string& api_key,
                                         const std::string& api_secret, const AgentDispatchClientOptions& options)
    : impl_(std::make_shared<Impl>(
          Impl{ffi::AgentDispatchClient::with_api_key(host, api_key, api_secret, toFfi(options))})) {}

AgentDispatchClient AgentDispatchClient::withToken(const std::string& host, const std::string& token,
                                                   const AgentDispatchClientOptions& options) {
  return AgentDispatchClient(
      std::make_shared<Impl>(Impl{ffi::AgentDispatchClient::with_token(host, token, toFfi(options))}));
}

Result<AgentDispatchClient, AgentDispatchError> AgentDispatchClient::fromEnv(
    const std::string& host, const AgentDispatchClientOptions& options) {
  try {
    return Result<AgentDispatchClient, AgentDispatchError>::success(
        AgentDispatchClient(std::make_shared<Impl>(Impl{ffi::AgentDispatchClient::from_env(host, toFfi(options))})));
  } catch (...) {
    return Result<AgentDispatchClient, AgentDispatchError>::failure(toError(std::current_exception()));
  }
}

std::future<Result<AgentDispatch, AgentDispatchError>> AgentDispatchClient::createDispatch(
    const CreateAgentDispatchRequest& req) const {
  ffi::CreateAgentDispatchRequest ffi_req;
  ffi_req.agent_name = req.agent_name;
  ffi_req.room = req.room;
  ffi_req.metadata = req.metadata;
  ffi_req.restart_policy = toFfi(req.restart_policy);
  ffi_req.deployment = req.deployment;
  ffi_req.attributes = {req.attributes.begin(), req.attributes.end()};
  return bridge<AgentDispatch>([&] { return impl_->client->create_dispatch(ffi_req); },
                               [](const ffi::AgentDispatch& d) { return fromFfi(d); });
}

std::future<Result<AgentDispatch, AgentDispatchError>> AgentDispatchClient::deleteDispatch(
    const std::string& dispatch_id, const std::string& room_name) const {
  return bridge<AgentDispatch>([&] { return impl_->client->delete_dispatch(dispatch_id, room_name); },
                               [](const ffi::AgentDispatch& d) { return fromFfi(d); });
}

std::future<Result<std::vector<AgentDispatch>, AgentDispatchError>> AgentDispatchClient::listDispatch(
    const std::string& room_name) const {
  return bridge<std::vector<AgentDispatch>>([&] { return impl_->client->list_dispatch(room_name); },
                                            [](const std::vector<ffi::AgentDispatch>& list) {
                                              std::vector<AgentDispatch> out;
                                              out.reserve(list.size());
                                              for (const auto& d : list) {
                                                out.push_back(fromFfi(d));
                                              }
                                              return out;
                                            });
}

std::future<Result<std::optional<AgentDispatch>, AgentDispatchError>> AgentDispatchClient::getDispatch(
    const std::string& dispatch_id, const std::string& room_name) const {
  return bridge<std::optional<AgentDispatch>>(
      [&] { return impl_->client->get_dispatch(dispatch_id, room_name); },
      [](const std::optional<ffi::AgentDispatch>& d) -> std::optional<AgentDispatch> {
        if (!d) {
          return std::nullopt;
        }
        return fromFfi(*d);
      });
}

} // namespace livekit::api
