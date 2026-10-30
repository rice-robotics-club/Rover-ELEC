import rclpy
from rclpy.node import Node

from sensor_msgs.msg import JointState
import struct
import serial

import pygame


class JointStatePublisher(Node):

    def __init__(self):
        super().__init__('joint_state_publisher')
        self.publisher = self.create_publisher(
            JointState,
            '/joint_states',
            10
        )

        self.timer = self.create_timer(0.01, self.publish_joint_states)

    def listener_callback(self,msg):
         self.get_logger().info(f'I heard: "{msg.data}"')
    def publish_joint_states(self):
        msg = JointState()

        # Timestamp
        msg.header.stamp = self.get_clock().now().to_msg()

        # Joint names (must match your URDF)
        msg.name = [
            'dof_shoulder',
            'dof_elbow',
            'dof_claw'
        ]

        # Joint positions in radians (or meters for prismatic joints)
        msg.position = [
            0.0,
            1.2,
            -0.3
        ]
        self.publisher.publish(msg)


def main(args=None):

    pygame.init()
    pygame.joystick.init()

    # Find controller
    if pygame.joystick.get_count() == 0:
        print("No controller detected")
        exit()
    global controller;
    controller = pygame.joystick.Joystick(0)
    controller.init()

    print("Connected:", controller.get_name())

    

    rclpy.init(args=args)

    node = JointStatePublisher()

    enable = 0b10000011
    forward = 0b00000000
    backward = 0b00000000
    checksum = 0b00000000

    #send to the esp

    enable = 0b00000000

    while rclpy.ok():
        pygame.event.pump()

        # Analog sticks
        if round(controller.get_axis(1)) == -1:
            forward |= (1 << 0)

        if round(controller.get_axis(1)) == 1:
            backward |= (1 << 0)

        if round(controller.get_axis(1)) == 0:
             forward &= ~(1 << 0)
             backward &= ~(1 << 0)

        if round(controller.get_axis(4)) == -1:
            forward |= (1 << 1)
        
        if round(controller.get_axis(4)) == 1:
            backward |= (1 << 1)
        
        if round(controller.get_axis(4)) == 0:
            forward &= ~(1 << 1)
            backward &= ~(1 << 1)

        checksum = backward ^ forward ^ enable

        print(f"Forward: {forward:08b}",end=" ",flush=False)
        print(f"Backward {backward:08b}",end=" ",flush=False)
        print(f"The full message to be sent to the esp: {0xAA:08b}{0x55:08b}{enable:08b}{forward:08b}{backward:08b}{checksum:08b}",end="\r",flush=True)
        right_y = round(controller.get_axis(3))
    
        # Buttons
        a_button = controller.get_button(0)
        b_button = controller.get_button(1)

        
        

        rclpy.spin_once (node)

    node.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()