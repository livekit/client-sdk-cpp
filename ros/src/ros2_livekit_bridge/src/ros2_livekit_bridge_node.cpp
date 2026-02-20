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

#include <rclcpp/rclcpp.hpp>

#include "ros2_livekit_bridge/ros2_livekit_bridge.hpp"

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);

  auto node = std::make_shared<ros2_livekit_bridge::Ros2LiveKitBridge>();

  rclcpp::ExecutorOptions exec_options;
  const size_t num_threads =
      node->ros_threads() > 0 ? static_cast<size_t>(node->ros_threads()) : 0;

  std::cout << "Starting executor with " << num_threads << " threads"
            << std::endl;
  rclcpp::executors::MultiThreadedExecutor executor(exec_options, num_threads);
  executor.add_node(node);
  executor.spin();

  rclcpp::shutdown();

  return EXIT_SUCCESS;
}
