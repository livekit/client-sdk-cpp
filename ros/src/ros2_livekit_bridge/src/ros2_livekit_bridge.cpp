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

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <geometry_msgs/msg/polygon_stamped.hpp>
#include <geometry_msgs/msg/pose2_d.hpp>
#include <geometry_msgs/msg/pose_with_covariance_stamped.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <nav_msgs/msg/path.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/joy.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <std_msgs/msg/string.hpp>

#include <ros2_foxglove_adapters/to_foxglove.hpp>

#include "livekit/track.h"

namespace ros2_livekit_bridge {

namespace {

constexpr size_t DEFAULT_MIN_QOS_DEPTH = 1;
constexpr size_t DEFAULT_MAX_QOS_DEPTH = 25;
constexpr const char *kImageMsgType = "sensor_msgs/msg/Image";

/**
 * @brief Convert a sensor_msgs::msg::Image to packed RGBA8.
 * @param img The input image
 * @param out The output buffer
 * @return True if the conversion was successful, false otherwise
 * @note this should be gutted. super inneficient.
 */
bool convertToRgba(const sensor_msgs::msg::Image &img,
                   std::vector<std::uint8_t> &out) {
  const std::size_t num_pixels =
      static_cast<std::size_t>(img.width) * img.height;
  out.resize(num_pixels * 4);

  const auto &enc = img.encoding;

  if (enc == "rgba8") {
    if (img.step == img.width * 4) {
      std::memcpy(out.data(), img.data.data(), num_pixels * 4);
    } else {
      for (std::uint32_t y = 0; y < img.height; ++y) {
        std::memcpy(out.data() + y * img.width * 4,
                    img.data.data() + y * img.step, img.width * 4);
      }
    }
    return true;
  }

  if (enc == "rgb8") {
    for (std::uint32_t y = 0; y < img.height; ++y) {
      const auto *row = img.data.data() + y * img.step;
      for (std::uint32_t x = 0; x < img.width; ++x) {
        const auto *px = row + x * 3;
        auto *dst = out.data() + (y * img.width + x) * 4;
        dst[0] = px[0];
        dst[1] = px[1];
        dst[2] = px[2];
        dst[3] = 255;
      }
    }
    return true;
  }

  if (enc == "bgr8") {
    for (std::uint32_t y = 0; y < img.height; ++y) {
      const auto *row = img.data.data() + y * img.step;
      for (std::uint32_t x = 0; x < img.width; ++x) {
        const auto *px = row + x * 3;
        auto *dst = out.data() + (y * img.width + x) * 4;
        dst[0] = px[2];
        dst[1] = px[1];
        dst[2] = px[0];
        dst[3] = 255;
      }
    }
    return true;
  }

  if (enc == "bgra8") {
    for (std::uint32_t y = 0; y < img.height; ++y) {
      const auto *row = img.data.data() + y * img.step;
      for (std::uint32_t x = 0; x < img.width; ++x) {
        const auto *px = row + x * 4;
        auto *dst = out.data() + (y * img.width + x) * 4;
        dst[0] = px[2];
        dst[1] = px[1];
        dst[2] = px[0];
        dst[3] = px[3];
      }
    }
    return true;
  }

  if (enc == "mono8") {
    for (std::uint32_t y = 0; y < img.height; ++y) {
      const auto *row = img.data.data() + y * img.step;
      for (std::uint32_t x = 0; x < img.width; ++x) {
        auto *dst = out.data() + (y * img.width + x) * 4;
        dst[0] = row[x];
        dst[1] = row[x];
        dst[2] = row[x];
        dst[3] = 255;
      }
    }
    return true;
  }

  return false;
}

/// Try a ROS parameter first; if empty, fall back to an environment variable.
/// Returns the resolved value and sets @p source to a human-readable label.
/**
 * @brief Try a ROS parameter first; if empty, fall back to an environment
 * variable.
 * @param node The node to resolve the credential from
 * @param param_name The name of the parameter
 * @param env_var_name The name of the environment variable
 * @param source The source of the credential. This is set to a human-readable
 * label of the source.
 * @return The resolved credential
 */
std::string resolveCredential(rclcpp::Node *node, const std::string &param_name,
                              const std::string &env_var_name,
                              std::string &source) {
  const std::string param_val = node->get_parameter(param_name).as_string();
  if (!param_val.empty()) {
    source = "ROS parameter '" + param_name + "'";
    return param_val;
  }
  const char *env_val = std::getenv(env_var_name.c_str());
  if (env_val && env_val[0] != '\0') {
    source = "environment variable " + env_var_name;
    return std::string(env_val);
  }
  source = "none";
  return {};
}

/**
 * @brief Compile a vector of strings into a vector of regex patterns.
 * @param strings The vector of strings to compile
 * @param out The output vector of regex patterns
 * @param logger The logger to use
 */
void compilePatterns(const std::vector<std::string> &strings,
                     std::vector<std::regex> &out, rclcpp::Logger logger) {
  for (const auto &pattern : strings) {
    try {
      out.emplace_back(pattern, std::regex::ECMAScript);
    } catch (const std::regex_error &e) {
      RCLCPP_ERROR(logger, "Invalid regex pattern '%s': %s", pattern.c_str(),
                   e.what());
    }
  }
}

/**
 * @brief Check if a string matches any of the regex patterns.
 * @param str The string to check
 * @param patterns The vector of regex patterns to check against
 * @return True if the string matches any of the patterns, false otherwise
 */
bool matchesAnyPattern(const std::string &str,
                       const std::vector<std::regex> &patterns) {
  for (const auto &pattern : patterns) {
    if (std::regex_match(str, pattern)) {
      return true;
    }
  }
  return false;
}

} // namespace

Ros2LiveKitBridge::Ros2LiveKitBridge(const rclcpp::NodeOptions &options)
    : rclcpp::Node("ros2_livekit_bridge", options) {
  this->declare_parameter<std::string>("room_name", "");
  this->declare_parameter<std::string>("livekit_url", "");
  this->declare_parameter<std::string>("livekit_token", "");
  this->declare_parameter<int>("topic_polling_period_ms", 500);
  this->declare_parameter<int>("ros_threads", 0);
  const std::vector<std::string> kEmptyStringVec{};
  this->declare_parameter("ros_topics",
                          rclcpp::ParameterValue(kEmptyStringVec));
  this->declare_parameter<int>("min_qos_depth",
                               static_cast<int>(DEFAULT_MIN_QOS_DEPTH));
  this->declare_parameter<int>("max_qos_depth",
                               static_cast<int>(DEFAULT_MAX_QOS_DEPTH));
  this->declare_parameter("best_effort_qos_topics",
                          rclcpp::ParameterValue(kEmptyStringVec));

  room_name_ = this->get_parameter("room_name").as_string();
  topic_polling_period_ms_ =
      this->get_parameter("topic_polling_period_ms").as_int();
  ros_threads_ = this->get_parameter("ros_threads").as_int();

  reentrant_callback_group_ =
      this->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  min_qos_depth_ =
      static_cast<size_t>(this->get_parameter("min_qos_depth").as_int());
  max_qos_depth_ =
      static_cast<size_t>(this->get_parameter("max_qos_depth").as_int());
  ros_topic_patterns_ = this->get_parameter("ros_topics").as_string_array();
  compilePatterns(ros_topic_patterns_, compiled_patterns_, this->get_logger());

  auto best_effort_topics =
      this->get_parameter("best_effort_qos_topics").as_string_array();
  compilePatterns(best_effort_topics, best_effort_qos_topic_patterns_,
                  this->get_logger());

  RCLCPP_INFO(this->get_logger(),
              "Room: '%s', polling period: %d ms, watching %zu topic patterns, "
              "QoS depth range: [%zu, %zu]",
              room_name_.c_str(), topic_polling_period_ms_,
              compiled_patterns_.size(), min_qos_depth_, max_qos_depth_);

  RCLCPP_INFO(this->get_logger(), "Attempting to resolve LiveKit credentials");

  // ----- Resolve LiveKit credentials (param -> env var fallback) -----
  std::string url_source, token_source;
  const std::string livekit_url =
      resolveCredential(this, "livekit_url", "LIVEKIT_URL", url_source);
  const std::string livekit_token =
      resolveCredential(this, "livekit_token", "LIVEKIT_TOKEN", token_source);

  RCLCPP_INFO(this->get_logger(), "LiveKit URL resolved from %s",
              url_source.c_str());
  RCLCPP_INFO(this->get_logger(), "LiveKit token resolved from %s",
              token_source.c_str());

  RCLCPP_INFO(this->get_logger(), "Creating default room options");
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  room_options.dynacast = true;

  if (livekit_url.empty() || livekit_token.empty()) {
    RCLCPP_WARN(
        this->get_logger(),
        "LiveKit credentials not fully provided — bridge will not connect.\n"
        "  livekit_url   : %s\n"
        "  livekit_token : %s\n"
        "Set them via ROS parameters (-p livekit_url:=... -p "
        "livekit_token:=...)\n"
        "or environment variables LIVEKIT_URL / LIVEKIT_TOKEN.",
        livekit_url.empty() ? "(missing)" : url_source.c_str(),
        livekit_token.empty() ? "(missing)" : token_source.c_str());
  } else {
    RCLCPP_INFO(this->get_logger(), "livekit_url   resolved from %s",
                url_source.c_str());
    RCLCPP_INFO(this->get_logger(), "livekit_token resolved from %s",
                token_source.c_str());
    RCLCPP_INFO(this->get_logger(), "Connecting to %s ...",
                livekit_url.c_str());
    if (livekit_bridge_.connect(livekit_url, livekit_token, room_options)) {
      RCLCPP_INFO(this->get_logger(), "Connected to LiveKit room.");
    } else {
      RCLCPP_ERROR(this->get_logger(), "Failed to connect to LiveKit room.");
    }
  }

  RCLCPP_INFO(this->get_logger(), "Creating timer for polling topics");

  poll_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(topic_polling_period_ms_),
      std::bind(&Ros2LiveKitBridge::pollTopics, this),
      reentrant_callback_group_);
}

Ros2LiveKitBridge::~Ros2LiveKitBridge() {
  data_topic_states_.clear();
  image_topic_states_.clear();
  if (livekit_bridge_.isConnected()) {
    RCLCPP_INFO(this->get_logger(), "Disconnecting LiveKit bridge...");
    livekit_bridge_.disconnect();
  }
}

void Ros2LiveKitBridge::pollTopics() {
  auto topic_names_and_types = this->get_topic_names_and_types();

  for (const auto &[topic_name, topic_types] : topic_names_and_types) {
    if (subscriptions_.count(topic_name) > 0) {
      continue;
    }

    if (!matchesTopic(topic_name)) {
      continue;
    }

    if (topic_types.empty()) {
      continue;
    }

    const auto &topic_type = topic_types.front();
    RCLCPP_INFO(this->get_logger(), "Discovered matching topic: '%s' [%s]",
                topic_name.c_str(), topic_type.c_str());
    createSubscriber(topic_name, topic_type);
  }
}

void Ros2LiveKitBridge::createSubscriber(const std::string &topic_name,
                                         const std::string &topic_type) {
  if (topic_type == kImageMsgType) {
    createImageSubscriber(topic_name);
    // TODO(sderosa): audio track support
    // } else if (topic_type == kAudioMsgType) {
    //   createAudioSubscriber(topic_name);
  } else if (topic_type == "nav_msgs/msg/Odometry") {
    createDataSubscriber<nav_msgs::msg::Odometry>(topic_name);
  } else if (topic_type == "nav_msgs/msg/Path") {
    createDataSubscriber<nav_msgs::msg::Path>(topic_name);
  } else if (topic_type == "nav_msgs/msg/OccupancyGrid") {
    createDataSubscriber<nav_msgs::msg::OccupancyGrid>(topic_name);
  } else if (topic_type == "geometry_msgs/msg/TransformStamped") {
    createDataSubscriber<geometry_msgs::msg::TransformStamped>(topic_name);
  } else if (topic_type == "geometry_msgs/msg/Pose2D") {
    createDataSubscriber<geometry_msgs::msg::Pose2D>(topic_name);
  } else if (topic_type == "geometry_msgs/msg/PolygonStamped") {
    createDataSubscriber<geometry_msgs::msg::PolygonStamped>(topic_name);
  } else if (topic_type == "geometry_msgs/msg/PoseWithCovarianceStamped") {
    createDataSubscriber<geometry_msgs::msg::PoseWithCovarianceStamped>(
        topic_name);
  } else if (topic_type == "sensor_msgs/msg/PointCloud2") {
    createDataSubscriber<sensor_msgs::msg::PointCloud2>(topic_name);
  } else if (topic_type == "sensor_msgs/msg/Imu") {
    createDataSubscriber<sensor_msgs::msg::Imu>(topic_name);
  } else if (topic_type == "sensor_msgs/msg/Joy") {
    createDataSubscriber<sensor_msgs::msg::Joy>(topic_name);
  } else if (topic_type == "sensor_msgs/msg/BatteryState") {
    createDataSubscriber<sensor_msgs::msg::BatteryState>(topic_name);
  } else if (topic_type == "std_msgs/msg/String") {
    createDataSubscriber<std_msgs::msg::String>(topic_name);
  } else {
    RCLCPP_WARN(this->get_logger(),
                "Unsupported message type '%s' on topic '%s' -- skipping",
                topic_type.c_str(), topic_name.c_str());
  }
}

template <typename RosMsgT>
void Ros2LiveKitBridge::createDataSubscriber(const std::string &topic_name) {
  auto qos = determineQoS(topic_name);

  rclcpp::SubscriptionOptions sub_options;
  sub_options.callback_group = reentrant_callback_group_;

  data_topic_states_[topic_name] = DataTopicState{};

  auto callback = [this, topic_name](typename RosMsgT::ConstSharedPtr msg) {
    auto &state = data_topic_states_[topic_name];

    if (!state.track) {
      if (!livekit_bridge_.isConnected()) {
        return;
      }
      try {
        state.track = livekit_bridge_.createDataTrack(topic_name);
        RCLCPP_INFO(this->get_logger(), "Created data track '%s'",
                    topic_name.c_str());
      } catch (const std::exception &e) {
        RCLCPP_ERROR(this->get_logger(),
                     "Failed to create data track for '%s': %s",
                     topic_name.c_str(), e.what());
        return;
      }
    }

    auto fg_msg = ros2_foxglove_adapters::toFoxglove(*msg);
    std::string serialized;
    if (!fg_msg.SerializeToString(&serialized)) {
      RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                           "Failed to serialize Foxglove message for '%s'",
                           topic_name.c_str());
      return;
    }

    state.track->pushFrame(
        reinterpret_cast<const std::uint8_t *>(serialized.data()),
        serialized.size());
  };

  auto subscription = this->create_subscription<RosMsgT>(topic_name, qos,
                                                         callback, sub_options);
  subscriptions_[topic_name] = subscription;

  RCLCPP_INFO(this->get_logger(),
              "Subscribed to data topic '%s' (Foxglove protobuf)",
              topic_name.c_str());
}

// Explicit template instantiations for all supported Foxglove-adapted types.
template void Ros2LiveKitBridge::createDataSubscriber<nav_msgs::msg::Odometry>(
    const std::string &);
template void Ros2LiveKitBridge::createDataSubscriber<nav_msgs::msg::Path>(
    const std::string &);
template void
Ros2LiveKitBridge::createDataSubscriber<nav_msgs::msg::OccupancyGrid>(
    const std::string &);
template void
Ros2LiveKitBridge::createDataSubscriber<geometry_msgs::msg::TransformStamped>(
    const std::string &);
template void
Ros2LiveKitBridge::createDataSubscriber<geometry_msgs::msg::Pose2D>(
    const std::string &);
template void
Ros2LiveKitBridge::createDataSubscriber<geometry_msgs::msg::PolygonStamped>(
    const std::string &);
template void Ros2LiveKitBridge::createDataSubscriber<
    geometry_msgs::msg::PoseWithCovarianceStamped>(const std::string &);
template void
Ros2LiveKitBridge::createDataSubscriber<sensor_msgs::msg::PointCloud2>(
    const std::string &);
template void Ros2LiveKitBridge::createDataSubscriber<sensor_msgs::msg::Imu>(
    const std::string &);
template void Ros2LiveKitBridge::createDataSubscriber<sensor_msgs::msg::Joy>(
    const std::string &);
template void
Ros2LiveKitBridge::createDataSubscriber<sensor_msgs::msg::BatteryState>(
    const std::string &);
template void Ros2LiveKitBridge::createDataSubscriber<std_msgs::msg::String>(
    const std::string &);

void Ros2LiveKitBridge::createImageSubscriber(const std::string &topic_name) {
  auto qos = determineQoS(topic_name);

  rclcpp::SubscriptionOptions sub_options;
  sub_options.callback_group = reentrant_callback_group_;

  image_topic_states_[topic_name] = ImageTopicState{};

  auto callback = [this,
                   topic_name](sensor_msgs::msg::Image::ConstSharedPtr msg) {
    auto &state = image_topic_states_[topic_name];

    if (!state.track) {
      if (!livekit_bridge_.isConnected()) {
        return;
      }
      try {
        state.track = livekit_bridge_.createVideoTrack(
            topic_name, static_cast<int>(msg->width),
            static_cast<int>(msg->height), livekit::TrackSource::SOURCE_CAMERA);
        RCLCPP_INFO(this->get_logger(), "Created video track '%s' (%ux%u, %s)",
                    topic_name.c_str(), msg->width, msg->height,
                    msg->encoding.c_str());
      } catch (const std::exception &e) {
        RCLCPP_ERROR(this->get_logger(),
                     "Failed to create video track for '%s': %s",
                     topic_name.c_str(), e.what());
        return;
      }
    }

    const auto &stamp = msg->header.stamp;
    const std::int64_t timestamp_us =
        static_cast<std::int64_t>(stamp.sec) * 1'000'000 +
        static_cast<std::int64_t>(stamp.nanosec) / 1'000;

    if (msg->encoding == "rgba8" && msg->step == msg->width * 4) {
      state.track->pushFrame(msg->data.data(), msg->data.size(), timestamp_us);
    } else {
      if (!convertToRgba(*msg, state.rgba_buf)) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "Unsupported image encoding '%s' on topic '%s'",
                             msg->encoding.c_str(), topic_name.c_str());
        return;
      }
      state.track->pushFrame(state.rgba_buf, timestamp_us);
    }
  };

  auto subscription = this->create_subscription<sensor_msgs::msg::Image>(
      topic_name, qos, callback, sub_options);
  subscriptions_[topic_name] = subscription;

  RCLCPP_INFO(this->get_logger(), "Subscribed to image topic '%s' [%s]",
              topic_name.c_str(), kImageMsgType);
}

/** Helpers **/

bool Ros2LiveKitBridge::matchesTopic(const std::string &topic_name) const {
  return matchesAnyPattern(topic_name, compiled_patterns_);
}

rclcpp::QoS
Ros2LiveKitBridge::determineQoS(const std::string &topic_name) const {
  // Follows the approach used by ros2 topic echo and the Foxglove bridge:
  // https://github.com/foxglove/foxglove-sdk/blob/main/ros/src/foxglove_bridge/src/ros2_foxglove_bridge.cpp
  size_t depth = 0;
  size_t reliable_count = 0;
  size_t transient_local_count = 0;

  const auto publisher_info = this->get_publishers_info_by_topic(topic_name);

  for (const auto &publisher : publisher_info) {
    const auto &qos = publisher.qos_profile();

    if (qos.reliability() == rclcpp::ReliabilityPolicy::Reliable) {
      ++reliable_count;
    }
    if (qos.durability() == rclcpp::DurabilityPolicy::TransientLocal) {
      ++transient_local_count;
    }

    // Some RMW implementations report history depth as 0; use a floor of 1 per
    // publisher so the total depth is at least equal to the publisher count
    // (important for multiple transient_local publishers, e.g. several
    // tf_static broadcasters).
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

  // Reliability: force best-effort if topic matches the override list,
  // otherwise use RELIABLE only when every publisher offers it (mixed policies
  // fall back to best-effort so we can connect to all publishers).
  if (matchesAnyPattern(topic_name, best_effort_qos_topic_patterns_)) {
    qos.best_effort();
  } else if (!publisher_info.empty() &&
             reliable_count == publisher_info.size()) {
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
  if (!publisher_info.empty() &&
      transient_local_count == publisher_info.size()) {
    qos.transient_local();
  } else {
    if (transient_local_count > 0) {
      RCLCPP_WARN(this->get_logger(),
                  "Mixed durability on topic '%s' (%zu/%zu transient_local). "
                  "Falling back to VOLATILE to connect to all publishers.",
                  topic_name.c_str(), transient_local_count,
                  publisher_info.size());
    }
    qos.durability_volatile();
  }

  RCLCPP_INFO(
      this->get_logger(),
      "QoS for '%s': depth=%zu, reliability=%s, durability=%s (%zu publishers)",
      topic_name.c_str(), depth,
      qos.reliability() == rclcpp::ReliabilityPolicy::Reliable ? "RELIABLE"
                                                               : "BEST_EFFORT",
      qos.durability() == rclcpp::DurabilityPolicy::TransientLocal
          ? "TRANSIENT_LOCAL"
          : "VOLATILE",
      publisher_info.size());

  return qos;
}

} // namespace ros2_livekit_bridge
