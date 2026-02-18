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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ros2_livekit_bridge/ros2_livekit_bridge.hpp"

namespace ros2_livekit_bridge {

namespace {

constexpr size_t DEFAULT_MIN_QOS_DEPTH = 1;
constexpr size_t DEFAULT_MAX_QOS_DEPTH = 25;

void compilePatterns(const std::vector<std::string>& strings, std::vector<std::regex>& out,
                     rclcpp::Logger logger) {
  for (const auto& pattern : strings) {
    try {
      out.emplace_back(pattern, std::regex::ECMAScript);
    } catch (const std::regex_error& e) {
      RCLCPP_ERROR(logger, "Invalid regex pattern '%s': %s", pattern.c_str(), e.what());
    }
  }
}

bool matchesAnyPattern(const std::string& str, const std::vector<std::regex>& patterns) {
  for (const auto& pattern : patterns) {
    if (std::regex_match(str, pattern)) {
      return true;
    }
  }
  return false;
}

}  // namespace

Ros2LiveKitBridge::Ros2LiveKitBridge(const rclcpp::NodeOptions& options)
  : rclcpp::Node("ros2_livekit_bridge", options) {
  this->declare_parameter<std::string>("room_name", "");
  this->declare_parameter<int>("topic_polling_period_ms", 500);
  const std::vector<std::string> kEmptyStringVec{};
  this->declare_parameter("ros_topics", rclcpp::ParameterValue(kEmptyStringVec));
  this->declare_parameter<int>("min_qos_depth", static_cast<int>(DEFAULT_MIN_QOS_DEPTH));
  this->declare_parameter<int>("max_qos_depth", static_cast<int>(DEFAULT_MAX_QOS_DEPTH));
  this->declare_parameter("best_effort_qos_topics", rclcpp::ParameterValue(kEmptyStringVec));

  room_name_ = this->get_parameter("room_name").as_string();
  topic_polling_period_ms_ = this->get_parameter("topic_polling_period_ms").as_int();
  min_qos_depth_ = static_cast<size_t>(this->get_parameter("min_qos_depth").as_int());
  max_qos_depth_ = static_cast<size_t>(this->get_parameter("max_qos_depth").as_int());
  ros_topic_patterns_ = this->get_parameter("ros_topics").as_string_array();
  compilePatterns(ros_topic_patterns_, compiled_patterns_, this->get_logger());

  auto best_effort_topics = this->get_parameter("best_effort_qos_topics").as_string_array();
  compilePatterns(best_effort_topics, best_effort_qos_topic_patterns_, this->get_logger());

  RCLCPP_INFO(this->get_logger(),
              "Room: '%s', polling period: %d ms, watching %zu topic patterns, "
              "QoS depth range: [%zu, %zu]",
              room_name_.c_str(), topic_polling_period_ms_, compiled_patterns_.size(),
              min_qos_depth_, max_qos_depth_);

  for (const auto& pattern : ros_topic_patterns_) {
    RCLCPP_INFO(this->get_logger(), "  topic pattern: %s", pattern.c_str());
  }
  for (const auto& pattern : best_effort_topics) {
    RCLCPP_INFO(this->get_logger(), "  best-effort override pattern: %s", pattern.c_str());
  }

  poll_timer_ = this->create_wall_timer(std::chrono::milliseconds(topic_polling_period_ms_),
                                         std::bind(&Ros2LiveKitBridge::pollTopics, this));
}

void Ros2LiveKitBridge::pollTopics() {
  auto topic_names_and_types = this->get_topic_names_and_types();

  for (const auto& [topic_name, topic_types] : topic_names_and_types) {
    if (subscriptions_.count(topic_name) > 0) {
      continue;
    }

    if (!matchesTopic(topic_name)) {
      continue;
    }

    if (topic_types.empty()) {
      continue;
    }

    const auto& topic_type = topic_types.front();
    RCLCPP_INFO(this->get_logger(), "Discovered matching topic: '%s' [%s]", topic_name.c_str(),
                topic_type.c_str());
    createSubscriber(topic_name, topic_type);
  }
}

void Ros2LiveKitBridge::createSubscriber(const std::string& topic_name,
                                          const std::string& topic_type) {
  auto qos = determineQoS(topic_name);

  rclcpp::SubscriptionEventCallbacks event_callbacks;
  event_callbacks.incompatible_qos_callback = [this,
                                                topic_name](const rclcpp::QOSRequestedIncompatibleQoSInfo&) {
    RCLCPP_ERROR(this->get_logger(), "Incompatible subscriber QoS settings for topic '%s'",
                 topic_name.c_str());
  };

  rclcpp::SubscriptionOptions sub_options;
  sub_options.event_callbacks = event_callbacks;

  auto callback = [this, topic_name](std::shared_ptr<rclcpp::SerializedMessage> msg) {
    RCLCPP_INFO(this->get_logger(), "Received message on '%s' (%zu bytes)", topic_name.c_str(),
                msg->size());
  };

  auto subscription =
    this->create_generic_subscription(topic_name, topic_type, qos, callback, sub_options);
  subscriptions_[topic_name] = subscription;

  RCLCPP_INFO(this->get_logger(), "Subscribed to '%s' [%s]", topic_name.c_str(),
              topic_type.c_str());
}

bool Ros2LiveKitBridge::matchesTopic(const std::string& topic_name) const {
  return matchesAnyPattern(topic_name, compiled_patterns_);
}

rclcpp::QoS Ros2LiveKitBridge::determineQoS(const std::string& topic_name) const {
  // Follows the approach used by ros2 topic echo and the Foxglove bridge:
  // https://github.com/foxglove/foxglove-sdk/blob/main/ros/src/foxglove_bridge/src/ros2_foxglove_bridge.cpp
  size_t depth = 0;
  size_t reliable_count = 0;
  size_t transient_local_count = 0;

  const auto publisher_info = this->get_publishers_info_by_topic(topic_name);

  for (const auto& publisher : publisher_info) {
    const auto& qos = publisher.qos_profile();

    if (qos.reliability() == rclcpp::ReliabilityPolicy::Reliable) {
      ++reliable_count;
    }
    if (qos.durability() == rclcpp::DurabilityPolicy::TransientLocal) {
      ++transient_local_count;
    }

    // Some RMW implementations report history depth as 0; use a floor of 1 per publisher so
    // the total depth is at least equal to the publisher count (important for multiple
    // transient_local publishers, e.g. several tf_static broadcasters).
    const size_t pub_depth = std::max(static_cast<size_t>(1), qos.depth());
    depth += pub_depth;
  }

  depth = std::max(depth, min_qos_depth_);
  if (depth > max_qos_depth_) {
    RCLCPP_WARN(this->get_logger(),
                "Clamping history depth for topic '%s' to %zu (was %zu). "
                "Increase max_qos_depth if needed.",
                topic_name.c_str(), max_qos_depth_, depth);
    depth = max_qos_depth_;
  }

  rclcpp::QoS qos{rclcpp::KeepLast(depth)};

  // Reliability: force best-effort if topic matches the override list, otherwise use RELIABLE
  // only when every publisher offers it (mixed policies fall back to best-effort so we can
  // connect to all publishers).
  if (matchesAnyPattern(topic_name, best_effort_qos_topic_patterns_)) {
    qos.best_effort();
  } else if (!publisher_info.empty() && reliable_count == publisher_info.size()) {
    qos.reliable();
  } else {
    if (reliable_count > 0) {
      RCLCPP_WARN(this->get_logger(),
                  "Mixed reliability on topic '%s' (%zu/%zu reliable). "
                  "Falling back to BEST_EFFORT to connect to all publishers.",
                  topic_name.c_str(), reliable_count, publisher_info.size());
    }
    qos.best_effort();
  }

  // Durability: TRANSIENT_LOCAL only when every publisher offers it.
  if (!publisher_info.empty() && transient_local_count == publisher_info.size()) {
    qos.transient_local();
  } else {
    if (transient_local_count > 0) {
      RCLCPP_WARN(this->get_logger(),
                  "Mixed durability on topic '%s' (%zu/%zu transient_local). "
                  "Falling back to VOLATILE to connect to all publishers.",
                  topic_name.c_str(), transient_local_count, publisher_info.size());
    }
    qos.durability_volatile();
  }

  RCLCPP_INFO(this->get_logger(),
              "QoS for '%s': depth=%zu, reliability=%s, durability=%s (%zu publishers)",
              topic_name.c_str(), depth,
              qos.reliability() == rclcpp::ReliabilityPolicy::Reliable ? "RELIABLE" : "BEST_EFFORT",
              qos.durability() == rclcpp::DurabilityPolicy::TransientLocal ? "TRANSIENT_LOCAL"
                                                                           : "VOLATILE",
              publisher_info.size());

  return qos;
}

}  // namespace ros2_livekit_bridge
