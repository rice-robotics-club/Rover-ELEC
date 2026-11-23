from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        Node(
            package='arm_control',
            executable='controller_node',
            output='screen'
        ),
        Node(
            package='arm_control',
            executable='com_node',
            output='screen'
        ),
    ])