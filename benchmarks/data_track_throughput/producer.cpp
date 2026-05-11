// Copyright 2026 LiveKit, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common.h"
#include "livekit/livekit.h"

using namespace livekit;
using namespace data_track_throughput;

namespace {

std::atomic<bool> g_running{true};

struct ProducerOptions {
  std::string url;
  std::string token;
  std::string consumer_identity;
  std::string track_name = kDefaultTrackName;
  std::size_t messages_per_scenario = 30;
  std::vector<std::size_t> payload_sizes_bytes = defaultPayloadSizesBytes();
  std::vector<double> rates_hz = defaultRatesHz();
  std::optional<double> single_rate_hz;
  std::optional<std::size_t> single_packet_size_bytes;
  std::optional<std::size_t> single_message_count;
};

void handleSignal(int) { g_running.store(false); }

void printUsage(const char* prog) {
  std::cout << "Usage:\n"
            << "  " << prog << " <ws-url> <token> [--consumer <identity>]\n"
            << "  " << prog << " --url <ws-url> --token <token> [--consumer <identity>]\n\n"
            << "Optional flags:\n"
            << "  --track-name <name>              Data track name. Default: " << kDefaultTrackName << "\n"
            << "  --messages-per-scenario <count>  Default sweep message count. "
               "Default: 30\n"
            << "  --sizes_kb <list>                Comma-separated sweep sizes in "
               "KiB. Default: 1,4,16,64,128,256,512\n"
            << "  --freq_hz <list>                 Comma-separated sweep rates in "
               "Hz. Default: 1,5,10,25,50,100,200,500,1000\n"
            << "  --rate-hz <hz>                   Run a single scenario instead of "
               "the full sweep\n"
            << "  --packet-size <size>             Single-scenario packet size, e.g. "
               "1mb\n"
            << "  --num-msgs <count>               Single-scenario message count\n\n"
            << "Env fallbacks:\n"
            << "  LIVEKIT_URL, LIVEKIT_TOKEN" << std::endl;
}

std::vector<std::string> splitCommaSeparated(const std::string& text) {
  std::vector<std::string> values;
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t end = text.find(',', start);
    const std::size_t count = end == std::string::npos ? std::string::npos : end - start;
    values.push_back(trim(text.substr(start, count)));
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return values;
}

std::vector<double> parseRateListHz(const std::string& text) {
  std::vector<double> rates;
  for (const auto& value : splitCommaSeparated(text)) {
    if (value.empty()) {
      throw std::runtime_error("--freq_hz contains an empty value");
    }
    rates.push_back(std::stod(value));
  }
  if (rates.empty()) {
    throw std::runtime_error("--freq_hz must contain at least one value");
  }
  return rates;
}

std::vector<std::size_t> parseSizeListKb(const std::string& text) {
  std::vector<std::size_t> sizes;
  for (const auto& value : splitCommaSeparated(text)) {
    if (value.empty()) {
      throw std::runtime_error("--sizes_kb contains an empty value");
    }
    sizes.push_back(parseByteSize(value + "kb"));
  }
  if (sizes.empty()) {
    throw std::runtime_error("--sizes_kb must contain at least one value");
  }
  return sizes;
}

bool parseArgs(int argc, char* argv[], ProducerOptions& options) {
  auto readFlagValue = [&](const std::string& flag, int& index) -> std::string {
    const std::string arg = argv[index];
    const std::string prefix = flag + "=";
    if (arg == flag && index + 1 < argc) {
      return argv[++index];
    }
    if (arg.rfind(prefix, 0) == 0) {
      return arg.substr(prefix.size());
    }
    return {};
  };

  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "-h" || arg == "--help") {
      return false;
    }

    if (arg.rfind("--url", 0) == 0) {
      options.url = readFlagValue("--url", index);
    } else if (arg.rfind("--token", 0) == 0) {
      options.token = readFlagValue("--token", index);
    } else if (arg.rfind("--consumer", 0) == 0) {
      options.consumer_identity = readFlagValue("--consumer", index);
    } else if (arg.rfind("--track-name", 0) == 0) {
      options.track_name = readFlagValue("--track-name", index);
    } else if (arg.rfind("--messages-per-scenario", 0) == 0) {
      options.messages_per_scenario =
          static_cast<std::size_t>(std::stoull(readFlagValue("--messages-per-scenario", index)));
    } else if (arg.rfind("--sizes_kb", 0) == 0) {
      options.payload_sizes_bytes = parseSizeListKb(readFlagValue("--sizes_kb", index));
    } else if (arg.rfind("--freq_hz", 0) == 0) {
      options.rates_hz = parseRateListHz(readFlagValue("--freq_hz", index));
    } else if (arg.rfind("--rate-hz", 0) == 0) {
      options.single_rate_hz = std::stod(readFlagValue("--rate-hz", index));
    } else if (arg.rfind("--packet-size", 0) == 0) {
      options.single_packet_size_bytes = parseByteSize(readFlagValue("--packet-size", index));
    } else if (arg.rfind("--num-msgs", 0) == 0) {
      options.single_message_count = static_cast<std::size_t>(std::stoull(readFlagValue("--num-msgs", index)));
    }
  }

  std::vector<std::string> positional;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg.rfind("--", 0) == 0) {
      if (arg.find('=') == std::string::npos && index + 1 < argc) {
        ++index;
      }
      continue;
    }
    positional.push_back(arg);
  }

  if (options.url.empty() && positional.size() >= 1) {
    options.url = positional[0];
  }
  if (options.token.empty() && positional.size() >= 2) {
    options.token = positional[1];
  }

  if (options.url.empty()) {
    options.url = getenvOrEmpty("LIVEKIT_URL");
  }
  if (options.token.empty()) {
    options.token = getenvOrEmpty("LIVEKIT_TOKEN");
  }

  const bool single_mode_requested = options.single_rate_hz.has_value() ||
                                     options.single_packet_size_bytes.has_value() ||
                                     options.single_message_count.has_value();
  const bool single_mode_complete = options.single_rate_hz.has_value() &&
                                    options.single_packet_size_bytes.has_value() &&
                                    options.single_message_count.has_value();
  if (single_mode_requested && !single_mode_complete) {
    throw std::runtime_error(
        "Single-scenario mode requires --rate-hz, "
        "--packet-size, and --num-msgs");
  }

  return !(options.url.empty() || options.token.empty());
}

std::string waitForConsumerIdentity(Room& room, const std::string& requested_identity, std::chrono::seconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (g_running.load() && std::chrono::steady_clock::now() < deadline) {
    if (!requested_identity.empty()) {
      if (room.remoteParticipant(requested_identity) != nullptr) {
        return requested_identity;
      }
    } else {
      const auto participants = room.remoteParticipants();
      if (participants.size() == 1 && participants.front() != nullptr) {
        return participants.front()->identity();
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  throw std::runtime_error(requested_identity.empty()
                               ? "Timed out waiting for exactly one remote consumer participant"
                               : "Timed out waiting for consumer identity: " + requested_identity);
}

std::vector<ScenarioRequest> buildScenarioPlan(const ProducerOptions& options, const std::string& producer_identity) {
  if (options.single_rate_hz.has_value()) {
    ScenarioRequest request;
    request.run_id = 1;
    request.scenario_name = defaultScenarioName(*options.single_packet_size_bytes, *options.single_rate_hz);
    request.producer_identity = producer_identity;
    request.track_name = options.track_name;
    request.desired_rate_hz = *options.single_rate_hz;
    request.packet_size_bytes = *options.single_packet_size_bytes;
    request.message_count = *options.single_message_count;
    validateScenario(request);
    return {request};
  }

  auto scenarios = makeScenarioPlan(producer_identity, options.track_name, options.messages_per_scenario,
                                    options.payload_sizes_bytes, options.rates_hz);
  for (const auto& scenario : scenarios) {
    validateScenario(scenario);
  }
  return scenarios;
}

ProducerStats runScenario(LocalDataTrack& track, const ScenarioRequest& request) {
  ProducerStats stats;
  stats.run_id = request.run_id;
  stats.scenario_name = request.scenario_name;

  const auto frame_interval = std::chrono::duration<double>(1.0 / request.desired_rate_hz);
  auto next_send = std::chrono::steady_clock::now();

  for (std::size_t index = 0; index < request.message_count && g_running.load(); ++index) {
    const std::uint64_t send_time_us = nowSystemUs();

    if (stats.first_send_time_us == 0) {
      stats.first_send_time_us = send_time_us;
    }
    stats.last_send_time_us = send_time_us;
    ++stats.attempted_count;
    stats.attempted_bytes += request.packet_size_bytes;

    auto payload = makePayload(request, static_cast<std::uint64_t>(index), send_time_us);
    auto push_result = track.tryPush(std::move(payload), send_time_us);
    if (push_result) {
      ++stats.enqueue_success_count;
      stats.enqueued_bytes += request.packet_size_bytes;
    } else {
      ++stats.enqueue_failure_count;
      std::cerr << "tryPush failed for scenario " << request.scenario_name << " seq=" << index
                << " reason=" << push_result.error().message << std::endl;
    }

    next_send += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frame_interval);
    std::this_thread::sleep_until(next_send);
  }

  return stats;
}

void printScenarioSummary(const json& summary) {
  std::cout << "Summary " << summary.value("scenario_name", "")
            << ": received=" << summary.value("messages_received", 0) << "/" << summary.value("messages_requested", 0)
            << " missed=" << summary.value("messages_missed", 0)
            << " received_bytes=" << summary.value("received_bytes", 0) << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
  ProducerOptions options;
  try {
    if (!parseArgs(argc, argv, options)) {
      printUsage(argv[0]);
      return 1;
    }
  } catch (const std::exception& e) {
    std::cerr << "Argument error: " << e.what() << std::endl;
    printUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

  try {
    Room room;
    RoomOptions room_options;
    room_options.auto_subscribe = true;
    room_options.dynacast = false;

    std::cout << "Connecting to " << options.url << std::endl;
    if (!room.Connect(options.url, options.token, room_options)) {
      throw std::runtime_error("Failed to connect to LiveKit room");
    }

    auto* local_participant = room.localParticipant();
    if (local_participant == nullptr) {
      throw std::runtime_error("Local participant unavailable after connect");
    }

    const auto publish_result = local_participant->publishDataTrack(options.track_name);
    if (!publish_result) {
      throw std::runtime_error("Failed to publish data track: " + publish_result.error().message);
    }
    auto track = publish_result.value();

    const std::string consumer_identity =
        waitForConsumerIdentity(room, options.consumer_identity, std::chrono::seconds(30));
    const auto scenarios = buildScenarioPlan(options, local_participant->identity());

    std::cout << "Target consumer: " << consumer_identity << std::endl;
    std::cout << "Running " << scenarios.size() << " scenario(s)" << std::endl;

    for (const auto& scenario : scenarios) {
      if (!g_running.load()) {
        break;
      }

      std::cout << "Preparing " << scenario.scenario_name << " rate=" << scenario.desired_rate_hz << "Hz"
                << " packet_size=" << humanBytes(scenario.packet_size_bytes) << " messages=" << scenario.message_count
                << std::endl;

      const auto prepare_payload = toJson(scenario).dump();
      const std::string prepare_response =
          local_participant->performRpc(consumer_identity, kPrepareRpcMethod, prepare_payload, 30.0);
      const json prepare_json = json::parse(prepare_response);
      if (!prepare_json.value("ready", false)) {
        throw std::runtime_error("Consumer rejected scenario " + scenario.scenario_name + ": " +
                                 prepare_json.value("message", "unknown error"));
      }

      const ProducerStats stats = runScenario(*track, scenario);
      json finish_payload;
      finish_payload["stats"] = toJson(stats);
      const std::string finish_response =
          local_participant->performRpc(consumer_identity, kFinishRpcMethod, finish_payload.dump(), 60.0);
      const json summary = json::parse(finish_response);
      printScenarioSummary(summary);
    }

    track->unpublishDataTrack();
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    livekit::shutdown();
    return 1;
  }

  livekit::shutdown();
  return 0;
}
