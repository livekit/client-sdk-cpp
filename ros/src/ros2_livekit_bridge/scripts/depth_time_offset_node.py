#!/usr/bin/env python3

import rclpy
from rclpy.node import Node

from sensor_msgs.msg import CompressedImage
from std_msgs.msg import Float64

IMAGE_TOPIC="/camera/realsense2_camera/depth/image_rect_raw/compressed"

class DepthTimeOffsetNode(Node):

    def __init__(self):
        super().__init__('depth_time_offset_node')

        self.subscription = self.create_subscription(
            CompressedImage,
	    IMAGE_TOPIC,
            self.depth_callback,
            10
        )
        print("Subbing to topic: {}", IMAGE_TOPIC)

        self.publisher = self.create_publisher(
            Float64,
            '/depth_time_offset_ms',
            10
        )

        self.get_logger().info('Depth Time Offset Node Started')

    def depth_callback(self, msg: CompressedImage):
        # Current machine time (ROS clock)
        now = self.get_clock().now()

        # Convert message header stamp to rclpy Time
        msg_time = rclpy.time.Time.from_msg(msg.header.stamp)

        # Compute difference in nanoseconds
        delta = now - msg_time
        delta_ms = delta.nanoseconds / 1e-6

        # Publish as Float64
        out_msg = Float64()
        out_msg.data = float(delta_ms)

        self.publisher.publish(out_msg)
        print("diff ms: {}", out_msg.data)

def main(args=None):
    rclpy.init(args=args)
    node = DepthTimeOffsetNode()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()
