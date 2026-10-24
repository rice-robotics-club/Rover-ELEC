#!/usr/bin/env python3
"""Command-PC demo: publish "<n>,<send time>" and report the round trip.

Publishes: topic        (default /station/hello)  once per 1/rate_hz seconds
Listens:   reply_topic  (default /station/pong)   the echo coming back over the radios
"""
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import String


class DemoTalker(Node):
    def __init__(self):
        super().__init__('demo_talker')
        self.declare_parameter('topic', '/station/hello')
        self.declare_parameter('reply_topic', '/station/pong')
        self.declare_parameter('rate_hz', 1.0)

        self.pub = self.create_publisher(String, self.get_parameter('topic').value, 10)
        self.create_subscription(
            String, self.get_parameter('reply_topic').value, self.on_reply, 10)
        self.n = 0
        self.create_timer(1.0 / float(self.get_parameter('rate_hz').value), self.tick)

    def tick(self):
        self.pub.publish(String(data=f'{self.n},{time.time():.3f}'))
        self.get_logger().info(f'Sent hello #{self.n}')
        self.n += 1

    def on_reply(self, msg: String):
        try:
            n, sent = msg.data.split(',')
            rtt_ms = (time.time() - float(sent)) * 1000.0
        except ValueError:
            self.get_logger().warn(f'Unexpected reply: {msg.data!r}')
            return
        self.get_logger().info(f'Reply to #{n}: round trip {rtt_ms:.0f} ms')


def main(args=None):
    rclpy.init(args=args)
    node = DemoTalker()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
