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

#pragma once

#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rclcpp/rclcpp.hpp>

namespace ros2_livekit_bridge {

class Ros2LiveKitBridge : public rclcpp::Node {
public:
  explicit Ros2LiveKitBridge(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

private:
  /**
  * @brief Poll the topics and create subscribers for the allowed topics
  */
  void pollTopics();
  
  /**
  * @brief Create a subscriber for the topic
  * @param topic_name The name of the topic
  * @param topic_type The type of the topic
  */
  void createSubscriber(const std::string& topic_name, const std::string& topic_type);

  /**
  * @brief Check if the topic matches the allowed topics
  * @param topic_name The name of the topic
  * @return True if the topic matches the allowed topics, false otherwise
  */
  bool matchesTopic(const std::string& topic_name) const;

  /**
  * @brief Get the QoS for the publisher
  * @param topic_name The name of the topic
  * @return The QoS for the publisher
  */
  rclcpp::QoS getPublisherQos(const std::string& topic_name) const;

  //! @brief The name of the room
  std::string room_name_;
  //! @brief The polling period for the topics
  int topic_polling_period_ms_;
  //! @brief The patterns for the topics
  std::vector<std::string> ros_topic_patterns_;
  //! @brief The compiled patterns for the topics
  std::vector<std::regex> compiled_patterns_;

  //! @brief The timer for the polling for new topics
  rclcpp::TimerBase::SharedPtr poll_timer_;
  //! @brief The subscriptions for the topics
  std::unordered_map<std::string, rclcpp::GenericSubscription::SharedPtr> subscriptions_;
};

}  // namespace ros2_livekit_bridge
