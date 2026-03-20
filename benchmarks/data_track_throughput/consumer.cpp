#include "common.h"

#include "livekit/livekit.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace livekit;
using namespace data_track_throughput;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> g_running{true};

struct ConsumerOptions {
  std::string url;
  std::string token;
  std::filesystem::path output_dir = "data_track_throughput_results";
  std::string track_name = kDefaultTrackName;
  std::chrono::milliseconds quiet_period{2000};
  std::chrono::milliseconds max_finish_wait{15000};
};

struct MessageObservation {
  std::uint64_t sequence = 0;
  std::size_t payload_bytes = 0;
  std::uint64_t send_time_us = 0;
  std::uint64_t arrival_time_us = 0;
  bool duplicate = false;
};

struct RunState {
  ScenarioRequest request;
  std::vector<MessageObservation> observations;
  std::unordered_set<std::uint64_t> seen_sequences;
  ProducerStats producer_stats;
  std::size_t duplicate_count = 0;
  std::uint64_t first_arrival_us = 0;
  std::uint64_t last_arrival_us = 0;
  bool producer_finished = false;
};

void handleSignal(int) { g_running.store(false); }

void printUsage(const char *prog) {
  std::cout << "Usage:\n"
            << "  " << prog << " <ws-url> <token>\n"
            << "  " << prog << " --url <ws-url> --token <token>\n\n"
            << "Optional flags:\n"
            << "  --output-dir <path>         CSV output directory. Default: data_track_throughput_results\n"
            << "  --track-name <name>         Data track name. Default: " << kDefaultTrackName << "\n"
            << "  --quiet-period-ms <ms>      No-new-message window before finalizing. Default: 2000\n"
            << "  --max-finish-wait-ms <ms>   Hard cap for run finalization. Default: 15000\n\n"
            << "Env fallbacks:\n"
            << "  LIVEKIT_URL, LIVEKIT_TOKEN" << std::endl;
}

bool parseArgs(int argc, char *argv[], ConsumerOptions &options) {
  auto readFlagValue = [&](const std::string &flag, int &index) -> std::string {
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
    } else if (arg.rfind("--output-dir", 0) == 0) {
      options.output_dir = readFlagValue("--output-dir", index);
    } else if (arg.rfind("--track-name", 0) == 0) {
      options.track_name = readFlagValue("--track-name", index);
    } else if (arg.rfind("--quiet-period-ms", 0) == 0) {
      options.quiet_period = std::chrono::milliseconds(
          std::stoll(readFlagValue("--quiet-period-ms", index)));
    } else if (arg.rfind("--max-finish-wait-ms", 0) == 0) {
      options.max_finish_wait = std::chrono::milliseconds(
          std::stoll(readFlagValue("--max-finish-wait-ms", index)));
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

  return !(options.url.empty() || options.token.empty());
}

class ThroughputConsumer {
public:
  explicit ThroughputConsumer(const ConsumerOptions &options)
      : options_(options),
        summary_csv_path_(options.output_dir / "throughput_summary.csv"),
        message_csv_path_(options.output_dir / "throughput_messages.csv") {
    std::filesystem::create_directories(options_.output_dir);
    ensureCsvHeader(
        summary_csv_path_,
        "run_id,scenario_name,desired_rate_hz,packet_size_bytes,"
        "messages_requested,messages_attempted,messages_enqueued,"
        "messages_enqueue_failed,messages_received,messages_missed,"
        "duplicate_messages,attempted_bytes,enqueued_bytes,received_bytes,"
        "first_send_time_us,last_send_time_us,"
        "first_arrival_time_us,last_arrival_time_us");
    ensureCsvHeader(message_csv_path_,
                    "run_id,sequence,payload_bytes,send_time_us,"
                    "arrival_time_us,is_duplicate");
  }

  ~ThroughputConsumer() {
    if (room_ != nullptr &&
        callback_id_ != std::numeric_limits<DataFrameCallbackId>::max()) {
      room_->removeOnDataFrameCallback(callback_id_);
    }
  }

  void setRoom(Room &room) { room_ = &room; }

  void ensureDataCallbackRegistered(const ScenarioRequest &request) {
    if (room_ == nullptr) {
      throw std::runtime_error("Room not attached to throughput consumer");
    }

    if (callback_id_ != std::numeric_limits<DataFrameCallbackId>::max() &&
        callback_producer_identity_ == request.producer_identity &&
        callback_track_name_ == request.track_name) {
      return;
    }

    if (callback_id_ != std::numeric_limits<DataFrameCallbackId>::max()) {
      room_->removeOnDataFrameCallback(callback_id_);
      callback_id_ = std::numeric_limits<DataFrameCallbackId>::max();
    }

    callback_id_ = room_->addOnDataFrameCallback(
        request.producer_identity, request.track_name,
        [this](const std::vector<std::uint8_t> &payload,
               std::optional<std::uint64_t> user_timestamp) {
          this->onDataFrame(payload, user_timestamp);
        });
    if (callback_id_ == std::numeric_limits<DataFrameCallbackId>::max()) {
      throw std::runtime_error("Failed to register data frame callback");
    }

    callback_producer_identity_ = request.producer_identity;
    callback_track_name_ = request.track_name;

    std::cout << "Listening for throughput data track '" << request.track_name
              << "' from " << request.producer_identity << std::endl;
  }

  std::optional<std::string>
  handlePrepareRpc(const RpcInvocationData &invocation) {
    const ScenarioRequest request =
        scenarioRequestFromJson(json::parse(invocation.payload));
    validateScenario(request);
    ensureDataCallbackRegistered(request);

    std::unique_lock<std::mutex> lock(mutex_);
    RunState state;
    state.request = request;
    runs_[request.run_id] = std::move(state);

    std::cout << "Prepared " << request.scenario_name
              << " rate=" << request.desired_rate_hz << "Hz"
              << " packet_size=" << request.packet_size_bytes
              << " messages=" << request.message_count << std::endl;

    return json{{"ready", true},
                {"scenario_name", request.scenario_name},
                {"track_name", options_.track_name}}
        .dump();
  }

  std::optional<std::string>
  handleFinishRpc(const RpcInvocationData &invocation) {
    const auto payload = json::parse(invocation.payload);
    const ProducerStats stats = producerStatsFromJson(payload.at("stats"));

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = runs_.find(stats.run_id);
      if (it == runs_.end()) {
        return json{{"ready", false},
                    {"message", "Unknown run_id in finish RPC"},
                    {"run_id", stats.run_id}}
            .dump();
      }

      it->second.producer_stats = stats;
      it->second.producer_finished = true;
      cv_.notify_all();
    }

    return finalizeRun(stats.run_id);
  }

private:
  void onDataFrame(const std::vector<std::uint8_t> &payload,
                   std::optional<std::uint64_t> user_timestamp) {
    const std::uint64_t arrival_time_us = nowSystemUs();
    const auto header = parseHeader(payload);
    if (!header) {
      std::cerr << "Ignored frame with invalid throughput header" << std::endl;
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto run_it = runs_.find(header->run_id);
    if (run_it == runs_.end()) {
      std::cerr << "Ignored frame for unknown run_id=" << header->run_id
                << std::endl;
      return;
    }

    RunState &run = run_it->second;
    const bool duplicate =
        !run.seen_sequences.insert(header->sequence).second;

    if (duplicate) {
      ++run.duplicate_count;
    }

    if (payload.size() != run.request.packet_size_bytes) {
      std::cerr << "WARN: payload size mismatch run_id=" << header->run_id
                << " seq=" << header->sequence
                << " expected=" << run.request.packet_size_bytes
                << " got=" << payload.size() << std::endl;
    }
    if (header->payload_size_bytes != payload.size() ||
        header->message_count != run.request.message_count ||
        header->packet_size_bytes != run.request.packet_size_bytes) {
      std::cerr << "WARN: header field mismatch run_id=" << header->run_id
                << " seq=" << header->sequence << std::endl;
    }
    if (user_timestamp.has_value() &&
        *user_timestamp != header->send_time_us) {
      std::cerr << "WARN: timestamp mismatch run_id=" << header->run_id
                << " seq=" << header->sequence << std::endl;
    }
    if (header->sequence >= run.request.message_count) {
      std::cerr << "WARN: unexpected sequence run_id=" << header->run_id
                << " seq=" << header->sequence
                << " max=" << run.request.message_count << std::endl;
    }

    if (run.first_arrival_us == 0) {
      run.first_arrival_us = arrival_time_us;
    }
    run.last_arrival_us = arrival_time_us;

    MessageObservation observation;
    observation.sequence = header->sequence;
    observation.payload_bytes = payload.size();
    observation.send_time_us = user_timestamp.value_or(header->send_time_us);
    observation.arrival_time_us = arrival_time_us;
    observation.duplicate = duplicate;
    run.observations.push_back(std::move(observation));
  }

  std::string finalizeRun(std::uint64_t run_id) {
    RunState completed;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      auto it = runs_.find(run_id);
      if (it == runs_.end()) {
        throw std::runtime_error("Run missing during finalization");
      }

      const auto deadline =
          std::chrono::steady_clock::now() + options_.max_finish_wait;
      while (g_running.load()) {
        auto now = std::chrono::steady_clock::now();
        if (it->second.seen_sequences.size() >=
            it->second.request.message_count) {
          break;
        }
        if (now >= deadline) {
          break;
        }
        if (it->second.producer_finished) {
          const std::uint64_t reference_time_us =
              it->second.last_arrival_us != 0
                  ? it->second.last_arrival_us
                  : it->second.producer_stats.last_send_time_us;
          if (reference_time_us != 0 &&
              nowSystemUs() >
                  reference_time_us + static_cast<std::uint64_t>(
                                          options_.quiet_period.count()) *
                                          1000ull) {
            break;
          }
        }

        cv_.wait_for(lock, 100ms);
        it = runs_.find(run_id);
        if (it == runs_.end()) {
          throw std::runtime_error("Run disappeared during finalization");
        }
      }

      completed = std::move(it->second);
      runs_.erase(it);
    }

    auto summary = buildSummaryJson(completed);
    appendMessageRows(completed);
    appendSummaryRow(summary);

    std::cout << "Wrote scenario " << completed.request.scenario_name
              << " to " << summary_csv_path_.string()
              << " and " << message_csv_path_.string() << std::endl;
    return summary.dump();
  }

  json buildSummaryJson(const RunState &run) const {
    std::size_t unique_count = 0;
    std::uint64_t received_bytes = 0;
    for (const auto &obs : run.observations) {
      if (!obs.duplicate) {
        ++unique_count;
        received_bytes += obs.payload_bytes;
      }
    }

    const std::size_t messages_missed =
        run.request.message_count > unique_count
            ? run.request.message_count - unique_count
            : 0;

    json summary;
    summary["run_id"] = run.request.run_id;
    summary["scenario_name"] = run.request.scenario_name;
    summary["desired_rate_hz"] = run.request.desired_rate_hz;
    summary["packet_size_bytes"] = run.request.packet_size_bytes;
    summary["messages_requested"] = run.request.message_count;
    summary["messages_attempted"] = run.producer_stats.attempted_count;
    summary["messages_enqueued"] = run.producer_stats.enqueue_success_count;
    summary["messages_enqueue_failed"] =
        run.producer_stats.enqueue_failure_count;
    summary["messages_received"] = unique_count;
    summary["messages_missed"] = messages_missed;
    summary["duplicate_messages"] = run.duplicate_count;
    summary["attempted_bytes"] = run.producer_stats.attempted_bytes;
    summary["enqueued_bytes"] = run.producer_stats.enqueued_bytes;
    summary["received_bytes"] = received_bytes;
    summary["first_send_time_us"] = run.producer_stats.first_send_time_us;
    summary["last_send_time_us"] = run.producer_stats.last_send_time_us;
    summary["first_arrival_time_us"] = run.first_arrival_us;
    summary["last_arrival_time_us"] = run.last_arrival_us;
    return summary;
  }

  void appendSummaryRow(const json &summary) const {
    std::ofstream out(summary_csv_path_, std::ios::app);
    if (!out) {
      throw std::runtime_error("Failed to open summary CSV for append");
    }

    out << summary.at("run_id").get<std::uint64_t>() << ','
        << csvEscape(summary.at("scenario_name").get<std::string>()) << ','
        << summary.at("desired_rate_hz").get<double>() << ','
        << summary.at("packet_size_bytes").get<std::size_t>() << ','
        << summary.at("messages_requested").get<std::size_t>() << ','
        << summary.at("messages_attempted").get<std::size_t>() << ','
        << summary.at("messages_enqueued").get<std::size_t>() << ','
        << summary.at("messages_enqueue_failed").get<std::size_t>() << ','
        << summary.at("messages_received").get<std::size_t>() << ','
        << summary.at("messages_missed").get<std::size_t>() << ','
        << summary.at("duplicate_messages").get<std::size_t>() << ','
        << summary.at("attempted_bytes").get<std::uint64_t>() << ','
        << summary.at("enqueued_bytes").get<std::uint64_t>() << ','
        << summary.at("received_bytes").get<std::uint64_t>() << ','
        << summary.at("first_send_time_us").get<std::uint64_t>() << ','
        << summary.at("last_send_time_us").get<std::uint64_t>() << ','
        << summary.at("first_arrival_time_us").get<std::uint64_t>() << ','
        << summary.at("last_arrival_time_us").get<std::uint64_t>() << '\n';
  }

  void appendMessageRows(const RunState &run) const {
    std::vector<MessageObservation> observations = run.observations;
    std::sort(observations.begin(), observations.end(),
              [](const MessageObservation &lhs, const MessageObservation &rhs) {
                return lhs.arrival_time_us < rhs.arrival_time_us;
              });

    std::ofstream out(message_csv_path_, std::ios::app);
    if (!out) {
      throw std::runtime_error("Failed to open message CSV for append");
    }

    for (const auto &message : observations) {
      out << run.request.run_id << ','
          << message.sequence << ',' << message.payload_bytes << ','
          << message.send_time_us << ',' << message.arrival_time_us << ','
          << (message.duplicate ? 1 : 0) << '\n';
    }
  }

  ConsumerOptions options_;
  std::filesystem::path summary_csv_path_;
  std::filesystem::path message_csv_path_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  Room *room_ = nullptr;
  DataFrameCallbackId callback_id_ =
      std::numeric_limits<DataFrameCallbackId>::max();
  std::string callback_producer_identity_;
  std::string callback_track_name_;
  std::unordered_map<std::uint64_t, RunState> runs_;
};

} // namespace

int main(int argc, char *argv[]) {
  ConsumerOptions options;
  if (!parseArgs(argc, argv, options)) {
    printUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);

  try {
    ThroughputConsumer consumer(options);
    Room room;
    consumer.setRoom(room);

    RoomOptions room_options;
    room_options.auto_subscribe = true;
    room_options.dynacast = false;

    std::cout << "Connecting to " << options.url << std::endl;
    if (!room.Connect(options.url, options.token, room_options)) {
      throw std::runtime_error("Failed to connect to LiveKit room");
    }

    auto *local_participant = room.localParticipant();
    if (local_participant == nullptr) {
      throw std::runtime_error("Local participant unavailable after connect");
    }

    local_participant->registerRpcMethod(
        kPrepareRpcMethod, [&consumer](const RpcInvocationData &invocation) {
          return consumer.handlePrepareRpc(invocation);
        });
    local_participant->registerRpcMethod(
        kFinishRpcMethod, [&consumer](const RpcInvocationData &invocation) {
          return consumer.handleFinishRpc(invocation);
        });

    std::cout << "Ready. CSV output directory: "
              << std::filesystem::absolute(options.output_dir).string()
              << std::endl;

    while (g_running.load()) {
      std::this_thread::sleep_for(250ms);
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    livekit::shutdown();
    return 1;
  }

  livekit::shutdown();
  return 0;
}
