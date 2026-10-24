#!/usr/bin/env python3
"""Jetson demo: echo whatever arrives on in_topic back out on out_topic."""
import rclpy
from rclpy.node import Node
from std_msgs.msg import String


class DemoEcho(Node):
    def __init__(self):
        super().__init__('demo_echo')
        self.declare_parameter('in_topic', '/rover/hello')
        self.declare_parameter('out_topic', '/rover/pong')
        self.pub = self.create_publisher(String, self.get_parameter('out_topic').value, 10)
        self.create_subscription(
            String, self.get_parameter('in_topic').value, self.on_msg, 10)

    def on_msg(self, msg: String):
        self.get_logger().info(f'Got "{msg.data}", echoing back')
        self.pub.publish(String(data=msg.data))


def main(args=None):
    rclpy.init(args=args)
    node = DemoEcho()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
