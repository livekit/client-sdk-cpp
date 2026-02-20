#!/usr/bin/env python3
#
# Copyright 2025 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Publish USB camera frames as sensor_msgs/msg/Image on /test/image1.

Usage:
  python3 usb_camera_publisher.py
  # or with ROS args:
  python3 usb_camera_publisher.py --ros-args -p camera_index:=0 -p fps:=30
"""

import cv2
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image


class UsbCameraPublisher(Node):
    def __init__(self):
        super().__init__("usb_camera_publisher")

        self.declare_parameter("camera_index", 0)
        self.declare_parameter("fps", 30)

        camera_index = self.get_parameter("camera_index").value
        fps = self.get_parameter("fps").value

        self.publisher_ = self.create_publisher(Image, "/test/image1", 10)

        self.cap_ = cv2.VideoCapture(camera_index)
        if not self.cap_.isOpened():
            self.get_logger().fatal(f"Cannot open camera {camera_index}")
            raise RuntimeError(f"Cannot open camera {camera_index}")

        width = int(self.cap_.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(self.cap_.get(cv2.CAP_PROP_FRAME_HEIGHT))
        self.get_logger().info(
            f"Opened camera {camera_index} ({width}x{height}), publishing at {fps} fps"
        )

        period = 1.0 / fps
        self.timer_ = self.create_timer(period, self.timer_callback)
        self.frame_count_ = 0

    def timer_callback(self):
        ret, frame = self.cap_.read()
        if not ret:
            self.get_logger().warn("Failed to read frame from camera")
            return

        msg = Image()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = "camera"
        msg.height = frame.shape[0]
        msg.width = frame.shape[1]
        msg.encoding = "bgr8"
        msg.is_bigendian = 0
        msg.step = frame.shape[1] * 3
        msg.data = frame.tobytes()

        self.publisher_.publish(msg)
        self.frame_count_ += 1

        if self.frame_count_ % 100 == 0:
            self.get_logger().info(f"Published {self.frame_count_} frames")

    def destroy_node(self):
        if self.cap_ is not None:
            self.cap_.release()
            self.get_logger().info("Camera released")
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = UsbCameraPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
