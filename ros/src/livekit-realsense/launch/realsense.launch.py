from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(
            package='realsense2_camera',
            executable='realsense2_camera_node',
            name='realsense2_camera',
            output='screen',
            parameters=[{
                'enable_depth': True,
                'enable_infra1': True,
                'enable_infra2': True,
                'enable_color': True,
            }],
        ),
    ])

