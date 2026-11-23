#!/usr/bin/env python3
"""Keyboard control for the command station: press 1 = motor ON, 0 = OFF, q = quit.

Publishes /station/motor_cmd (std_msgs/Bool) at 10 Hz with the current state.
The steady stream doubles as a dead-man signal: if this node stops, the PC
gateway stops sending ON after its cmd_timeout. Run it in its own terminal
(it needs a real keyboard), not from a launch file.
"""
import select
import sys
import termios
import tty

import rclpy
from std_msgs.msg import Bool

HELP = "Motor control:  1 = ON   0 = OFF   q = quit"


def main(args=None):
    if not sys.stdin.isatty(): #tty means terminal, we need that
        sys.exit('keyboard_motor needs an interactive terminal')

    rclpy.init(args=args) #start ROS
    node = rclpy.create_node('keyboard_motor') #make this node
    pub = node.create_publisher(Bool, '/station/motor_cmd', 10) #make a publisher motor command
    settings = termios.tcgetattr(sys.stdin) #save terminal settings 
    motor_on = False #default off motor
    print(HELP)
    try:
        tty.setcbreak(sys.stdin.fileno())  # keeps Ctrl-C working
        while rclpy.ok(): #if ROS is working
            if select.select([sys.stdin], [], [], 0.1)[0]: #wait for the user input for 0.1 secs
                key = sys.stdin.read(1)
                if key == '1':
                    motor_on = True
                    print('motor ON  ')
                elif key == '0':
                    motor_on = False
                    print('motor OFF ')
                elif key in ('q', 'Q'):
                    break
            pub.publish(Bool(data=motor_on)) #now publish that boolean
    except KeyboardInterrupt:
        pass
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, settings) #restores the terminal after q or crtl c
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
