import rclpy
from rclpy.node import Node

from sensor_msgs.msg import JointState
import struct
import serial

import pygame


global controller;
global node;

class JointStatePublisher(Node):

    positions = [0.0,1.2,-0.2]

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

        # Joint positions in radians 
        msg.position = self.positions
        self.publisher.publish(msg)




def main(args=None):

    rclpy.init(args=args)

    
    pygame.init()
    pygame.joystick.init()

    # Find controller
    if pygame.joystick.get_count() == 0:
        print("No controller detected")
        exit()


    controller = pygame.joystick.Joystick(0)

    controller.init()

    print("Connected:", controller.get_name())

    node = JointStatePublisher()

    enable = 0b10000011
    forward = 0b00000000
    backward = 0b00000000
    checksum = 0b00000000

    #initialize all the motors here

    enable = 0b00000000

    while rclpy.ok():
        pygame.event.pump()

        # Use bitmasks to set the forward and backwards bits based on the positions of the left and right sticks
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

        full_message = struct.pack('!6B', 0xAA, 0x55, enable, forward, backward, checksum)
        
        print(f"Forward: {forward:08b}",end=" ",flush=False)
        print(f"Backward {backward:08b}",end=" ",flush=False)
        print(f"The full message to be sent to the esp: {int.from_bytes(full_message):48b}",end="\r",flush=True)


        #TODO transmit the full message to the esp, and read the values it sends. Both will be done via serial.

        

        #changing the position of the arm in rviz is as simple as this. 
        node.positions[0] += 0.01


        #this updtates the position
        rclpy.spin_once (node)

    node.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()