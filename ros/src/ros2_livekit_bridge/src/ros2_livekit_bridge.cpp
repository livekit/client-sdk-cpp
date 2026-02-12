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

Ros2LiveKitBridge::Ros2LiveKitBridge(const rclcpp::NodeOptions& options)
  : rclcpp::Node("ros2_livekit_bridge", options) {
  this->declare_parameter<std::string>("room_name", "");
  this->declare_parameter<int>("topic_polling_period_ms", 500);
  this->declare_parameter<std::vector<std::string>>("ros_topics", std::vector<std::string>{});

  room_name_ = this->get_parameter("room_name").as_string();
  topic_polling_period_ms_ = this->get_parameter("topic_polling_period_ms").as_int();
  ros_topic_patterns_ = this->get_parameter("ros_topics").as_string_array();

  for (const auto& pattern : ros_topic_patterns_) {
    try {
      compiled_patterns_.emplace_back(pattern, std::regex::ECMAScript);
    } catch (const std::regex_error& e) {
      RCLCPP_ERROR(this->get_logger(), "Invalid regex pattern '%s': %s", pattern.c_str(),
                   e.what());
    }
  }

  RCLCPP_INFO(this->get_logger(), "Room: '%s', polling period: %d ms, watching %zu topic patterns",
              room_name_.c_str(), topic_polling_period_ms_, compiled_patterns_.size());

  for (const auto& pattern : ros_topic_patterns_) {
    RCLCPP_INFO(this->get_logger(), "  pattern: %s", pattern.c_str());
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
  auto qos = getPublisherQos(topic_name);

  auto callback = [this, topic_name](std::shared_ptr<rclcpp::SerializedMessage> msg) {
    RCLCPP_INFO(this->get_logger(), "Received message on '%s' (%zu bytes)", topic_name.c_str(),
                msg->size());
  };

  auto subscription = this->create_generic_subscription(topic_name, topic_type, qos, callback);
  subscriptions_[topic_name] = subscription;

  RCLCPP_INFO(this->get_logger(), "Subscribed to '%s' [%s]", topic_name.c_str(),
              topic_type.c_str());
}

bool Ros2LiveKitBridge::matchesTopic(const std::string& topic_name) const {
  for (const auto& pattern : compiled_patterns_) {
    if (std::regex_match(topic_name, pattern)) {
      return true;
    }
  }
  return false;
}

rclcpp::QoS Ros2LiveKitBridge::getPublisherQos(const std::string& topic_name) const {
  auto publishers = this->get_publishers_info_by_topic(topic_name);
  if (publishers.empty()) {
    RCLCPP_WARN(this->get_logger(),
                "No publishers found for '%s', using SensorDataQoS as fallback",
                topic_name.c_str());
    return rclcpp::SensorDataQoS();
  }

  const auto& endpoint_qos = publishers.front().qos_profile();

  rclcpp::QoS qos(10);
  qos.reliability(endpoint_qos.reliability());
  qos.durability(endpoint_qos.durability());

  RCLCPP_INFO(this->get_logger(), "Matched QoS for '%s': reliability=%s, durability=%s",
              topic_name.c_str(),
              endpoint_qos.reliability() == rclcpp::ReliabilityPolicy::Reliable ? "RELIABLE"
                                                                                : "BEST_EFFORT",
              endpoint_qos.durability() == rclcpp::DurabilityPolicy::TransientLocal
                ? "TRANSIENT_LOCAL"
                : "VOLATILE");

  return qos;
}

}  // namespace ros2_livekit_bridge
