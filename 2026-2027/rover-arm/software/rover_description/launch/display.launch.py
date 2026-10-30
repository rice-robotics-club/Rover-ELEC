from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    package_dir = get_package_share_directory('rover_description')

    urdf_path = os.path.join(
        package_dir,
        'final_assembly',
        'final_assembly.urdf'
    )

    with open(urdf_path, 'r') as f:
        robot_description = f.read()

    print(f'Loading URDF from: {urdf_path}')

    return LaunchDescription([
        Node(
            package='robot_state_publisher',
            executable='robot_state_publisher',
            name='robot_state_publisher',
            parameters=[{
                'robot_description': robot_description
            }]
        ),
        #Node(
        #   package='joint_state_publisher_gui',
        #    executable='joint_state_publisher_gui'
        #),
        Node(
            package='rviz2',
            executable='rviz2'
        )
    ])