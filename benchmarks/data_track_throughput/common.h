#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace data_track_throughput {

using json = nlohmann::json;

constexpr const char *kDefaultTrackName = "data-track-throughput";
constexpr const char *kPrepareRpcMethod = "throughput.prepare";
constexpr const char *kFinishRpcMethod = "throughput.finish";
constexpr std::size_t kMinimumPayloadBytes = 1024;
constexpr std::size_t kMaximumPayloadBytes = 256ull * 1024ull * 1024ull;
constexpr double kMinimumRateHz = 1.0;
constexpr double kMaximumRateHz = 50000.0;
constexpr double kMaximumDataRateBytesPerSec = 10'000'000'000.0; // 10 GBps
constexpr std::uint32_t kFrameMagic = 0x31545444u;               // "DTT1"
constexpr std::uint32_t kFrameVersion = 1;
constexpr std::size_t kFrameHeaderBytes = 56;

inline std::string getenvOrEmpty(const char *name) {
  const char *value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string(value);
}

inline std::uint64_t nowSystemUs() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<microseconds>(system_clock::now().time_since_epoch())
          .count());
}

inline std::string toLower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

inline std::string trim(std::string value) {
  auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(),
                           [&](unsigned char c) { return !is_space(c); }));
  value.erase(std::find_if(value.rbegin(), value.rend(),
                           [&](unsigned char c) { return !is_space(c); })
                  .base(),
              value.end());
  return value;
}

inline std::size_t parseByteSize(const std::string &text) {
  const std::string lowered = toLower(trim(text));
  if (lowered.empty()) {
    throw std::runtime_error("Empty byte size");
  }

  std::size_t split = 0;
  while (split < lowered.size() &&
         (std::isdigit(static_cast<unsigned char>(lowered[split])) != 0 ||
          lowered[split] == '.')) {
    ++split;
  }

  const double number = std::stod(lowered.substr(0, split));
  const std::string unit = trim(lowered.substr(split));

  double multiplier = 1.0;
  if (unit.empty() || unit == "b") {
    multiplier = 1.0;
  } else if (unit == "kb" || unit == "kib" || unit == "k") {
    multiplier = 1024.0;
  } else if (unit == "mb" || unit == "mib" || unit == "m") {
    multiplier = 1024.0 * 1024.0;
  } else if (unit == "gb" || unit == "gib" || unit == "g") {
    multiplier = 1024.0 * 1024.0 * 1024.0;
  } else {
    throw std::runtime_error("Unsupported size unit: " + text);
  }

  return static_cast<std::size_t>(std::llround(number * multiplier));
}

inline std::string humanBytes(std::size_t bytes) {
  static constexpr const char *kUnits[] = {"B", "KiB", "MiB", "GiB"};
  double value = static_cast<double>(bytes);
  std::size_t unit_index = 0;
  while (value >= 1024.0 && unit_index + 1 < std::size(kUnits)) {
    value /= 1024.0;
    ++unit_index;
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(unit_index == 0 ? 0 : 2) << value
      << kUnits[unit_index];
  return oss.str();
}

inline std::string csvEscape(const std::string &value) {
  if (value.find_first_of(",\"\n") == std::string::npos) {
    return value;
  }

  std::string escaped = "\"";
  for (char ch : value) {
    if (ch == '"') {
      escaped += "\"\"";
    } else {
      escaped.push_back(ch);
    }
  }
  escaped.push_back('"');
  return escaped;
}

inline void ensureCsvHeader(const std::filesystem::path &path,
                            const std::string &header) {
  if (std::filesystem::exists(path)) {
    return;
  }

  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Failed to create CSV: " + path.string());
  }
  out << header << '\n';
}

struct ScenarioRequest {
  std::uint64_t run_id = 0;
  std::string scenario_name;
  std::string producer_identity;
  std::string track_name = kDefaultTrackName;
  double desired_rate_hz = 0.0;
  std::size_t packet_size_bytes = 0;
  std::size_t message_count = 0;
};

struct ProducerStats {
  std::uint64_t run_id = 0;
  std::string scenario_name;
  std::size_t attempted_count = 0;
  std::size_t enqueue_success_count = 0;
  std::size_t enqueue_failure_count = 0;
  std::uint64_t attempted_bytes = 0;
  std::uint64_t enqueued_bytes = 0;
  std::uint64_t first_send_time_us = 0;
  std::uint64_t last_send_time_us = 0;
};

struct FrameHeader {
  std::uint64_t run_id = 0;
  std::uint64_t sequence = 0;
  std::uint64_t message_count = 0;
  std::uint64_t payload_size_bytes = 0;
  std::uint64_t packet_size_bytes = 0;
  std::uint64_t send_time_us = 0;
};

inline void writeLe32(std::vector<std::uint8_t> &buffer, std::size_t offset,
                      std::uint32_t value) {
  for (int idx = 0; idx < 4; ++idx) {
    buffer[offset + idx] =
        static_cast<std::uint8_t>((value >> (idx * 8)) & 0xFF);
  }
}

inline void writeLe64(std::vector<std::uint8_t> &buffer, std::size_t offset,
                      std::uint64_t value) {
  for (int idx = 0; idx < 8; ++idx) {
    buffer[offset + idx] =
        static_cast<std::uint8_t>((value >> (idx * 8)) & 0xFF);
  }
}

inline std::uint32_t readLe32(const std::vector<std::uint8_t> &buffer,
                              std::size_t offset) {
  std::uint32_t value = 0;
  for (int idx = 0; idx < 4; ++idx) {
    value |= static_cast<std::uint32_t>(buffer[offset + idx]) << (idx * 8);
  }
  return value;
}

inline std::uint64_t readLe64(const std::vector<std::uint8_t> &buffer,
                              std::size_t offset) {
  std::uint64_t value = 0;
  for (int idx = 0; idx < 8; ++idx) {
    value |= static_cast<std::uint64_t>(buffer[offset + idx]) << (idx * 8);
  }
  return value;
}

inline std::vector<std::uint8_t> makePayload(const ScenarioRequest &request,
                                             std::uint64_t sequence,
                                             std::uint64_t send_time_us) {
  if (request.packet_size_bytes < kFrameHeaderBytes) {
    throw std::runtime_error("Payload too small for frame header");
  }

  std::vector<std::uint8_t> payload(request.packet_size_bytes,
                                    static_cast<std::uint8_t>(sequence & 0xFF));
  writeLe32(payload, 0, kFrameMagic);
  writeLe32(payload, 4, kFrameVersion);
  writeLe64(payload, 8, request.run_id);
  writeLe64(payload, 16, sequence);
  writeLe64(payload, 24, static_cast<std::uint64_t>(request.message_count));
  writeLe64(payload, 32, static_cast<std::uint64_t>(request.packet_size_bytes));
  writeLe64(payload, 40, static_cast<std::uint64_t>(request.packet_size_bytes));
  writeLe64(payload, 48, send_time_us);
  return payload;
}

inline std::optional<FrameHeader>
parseHeader(const std::vector<std::uint8_t> &payload) {
  if (payload.size() < kFrameHeaderBytes) {
    return std::nullopt;
  }
  if (readLe32(payload, 0) != kFrameMagic ||
      readLe32(payload, 4) != kFrameVersion) {
    return std::nullopt;
  }

  FrameHeader header;
  header.run_id = readLe64(payload, 8);
  header.sequence = readLe64(payload, 16);
  header.message_count = readLe64(payload, 24);
  header.payload_size_bytes = readLe64(payload, 32);
  header.packet_size_bytes = readLe64(payload, 40);
  header.send_time_us = readLe64(payload, 48);
  return header;
}

inline std::string defaultScenarioName(std::size_t packet_size_bytes,
                                       double rate_hz) {
  std::ostringstream oss;
  oss << humanBytes(packet_size_bytes);
  oss << "_" << std::fixed << std::setprecision(0) << rate_hz << "Hz";
  std::string name = oss.str();
  std::replace(name.begin(), name.end(), '.', '_');
  return name;
}

inline json toJson(const ScenarioRequest &request) {
  return json{
      {"run_id", request.run_id},
      {"scenario_name", request.scenario_name},
      {"producer_identity", request.producer_identity},
      {"track_name", request.track_name},
      {"desired_rate_hz", request.desired_rate_hz},
      {"packet_size_bytes", request.packet_size_bytes},
      {"message_count", request.message_count},
  };
}

inline ScenarioRequest scenarioRequestFromJson(const json &value) {
  ScenarioRequest request;
  request.run_id = value.at("run_id").get<std::uint64_t>();
  request.scenario_name = value.at("scenario_name").get<std::string>();
  request.producer_identity = value.at("producer_identity").get<std::string>();
  request.track_name = value.at("track_name").get<std::string>();
  request.desired_rate_hz = value.at("desired_rate_hz").get<double>();
  request.packet_size_bytes = value.at("packet_size_bytes").get<std::size_t>();
  request.message_count = value.at("message_count").get<std::size_t>();
  return request;
}

inline json toJson(const ProducerStats &stats) {
  return json{
      {"run_id", stats.run_id},
      {"scenario_name", stats.scenario_name},
      {"attempted_count", stats.attempted_count},
      {"enqueue_success_count", stats.enqueue_success_count},
      {"enqueue_failure_count", stats.enqueue_failure_count},
      {"attempted_bytes", stats.attempted_bytes},
      {"enqueued_bytes", stats.enqueued_bytes},
      {"first_send_time_us", stats.first_send_time_us},
      {"last_send_time_us", stats.last_send_time_us},
  };
}

inline ProducerStats producerStatsFromJson(const json &value) {
  ProducerStats stats;
  stats.run_id = value.at("run_id").get<std::uint64_t>();
  stats.scenario_name = value.at("scenario_name").get<std::string>();
  stats.attempted_count = value.at("attempted_count").get<std::size_t>();
  stats.enqueue_success_count =
      value.at("enqueue_success_count").get<std::size_t>();
  stats.enqueue_failure_count =
      value.at("enqueue_failure_count").get<std::size_t>();
  stats.attempted_bytes = value.at("attempted_bytes").get<std::uint64_t>();
  stats.enqueued_bytes = value.at("enqueued_bytes").get<std::uint64_t>();
  stats.first_send_time_us =
      value.at("first_send_time_us").get<std::uint64_t>();
  stats.last_send_time_us = value.at("last_send_time_us").get<std::uint64_t>();
  return stats;
}

inline bool exceedsDataRateBudget(const ScenarioRequest &request) {
  return static_cast<double>(request.packet_size_bytes) *
             request.desired_rate_hz >
         kMaximumDataRateBytesPerSec;
}

inline void validateScenario(const ScenarioRequest &request) {
  if (request.message_count == 0) {
    throw std::runtime_error("message_count must be greater than zero");
  }
  if (request.desired_rate_hz < kMinimumRateHz ||
      request.desired_rate_hz > kMaximumRateHz) {
    throw std::runtime_error("desired_rate_hz must be between 1 and 50000");
  }
  if (request.packet_size_bytes < kMinimumPayloadBytes ||
      request.packet_size_bytes > kMaximumPayloadBytes) {
    throw std::runtime_error(
        "packet_size_bytes must be between 1 KiB and 256 MiB");
  }
  if (exceedsDataRateBudget(request)) {
    throw std::runtime_error("scenario exceeds data-rate budget of 10 Gbps");
  }
}

inline std::vector<ScenarioRequest>
makeDefaultScenarioPlan(const std::string &producer_identity,
                        const std::string &track_name,
                        std::size_t messages_per_scenario) {
  const std::vector<std::size_t> payload_sizes = {1ull * 1024ull,
                                                  4ull * 1024ull,
                                                  16ull * 1024ull,
                                                  64ull * 1024ull,
                                                  128ull * 1024ull,
                                                  256ull * 1024ull,
                                                  512ull * 1024ull,
                                                  1ull * 1024ull * 1024ull,
                                                  2ull * 1024ull * 1024ull,
                                                  4ull * 1024ull * 1024ull,
                                                  16ull * 1024ull * 1024ull,
                                                  64ull * 1024ull * 1024ull,
                                                  256ull * 1024ull * 1024ull};
  const std::vector<double> rates = {1.0,     5.0,     10.0,   25.0,   50.0,
                                     100.0,   200.0,   500.0,  1000.0, 5000.0,
                                     10000.0, 20000.0, 50000.0};

  std::vector<ScenarioRequest> scenarios;
  std::uint64_t run_id = 1;

  for (std::size_t payload_size : payload_sizes) {
    for (double rate_hz : rates) {
      ScenarioRequest request;
      request.run_id = run_id++;
      request.scenario_name = defaultScenarioName(payload_size, rate_hz);
      request.producer_identity = producer_identity;
      request.track_name = track_name;
      request.desired_rate_hz = rate_hz;
      request.packet_size_bytes = payload_size;
      request.message_count = messages_per_scenario;
      if (exceedsDataRateBudget(request)) {
        continue;
      }
      scenarios.push_back(std::move(request));
    }
  }

  return scenarios;
}

} // namespace data_track_throughput
