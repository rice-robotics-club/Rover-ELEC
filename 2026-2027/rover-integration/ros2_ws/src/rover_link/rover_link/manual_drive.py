#!/usr/bin/env python3
"""Manual drive (Jetson): the rover gateway's drive command  ->  the drive command for the SPI master.

Subscribes: /rover/drive_cmd (std_msgs/Float32MultiArray)  data = [right, left], from rover_gateway
            /rover/manual    (std_msgs/Bool)               True = manual driving: pass drive_cmd on
            /rover/estop     (std_msgs/Bool)               True = e-stop, from rover_gateway
Publishes:  /rover/drive_out (std_msgs/Float32MultiArray)  data = [right, left], at publish_hz
Params:     publish_hz, cmd_timeout, estop_hold, neutral

While /manual is True and drive_cmd keeps arriving, drive_cmd is passed on unchanged.
In every other case the output is 0, 0:
  - /manual is False, or no /manual message has been received yet
  - no drive_cmd for cmd_timeout seconds (rover_gateway publishes it at 10 Hz, and
    publishes 0, 0 itself when the radio goes silent)
  - an e-stop is active (see below)
The output is published at a steady rate (zeros included), so the node after this
one (master_spi) can tell this node is still alive.
Added k variable to multiply drive and limit speed
E-stop: every /rover/estop True message sets the output to 0, 0 at once and restarts
a hold of estop_hold seconds (the station sends five in a row). The e-stop ends
only when BOTH are true: the hold has run out, and the drive command is back at
neutral (both values within +-neutral of 0). So a stick that is still held over
cannot start the rover again by itself. It stays active across /manual changes.
"""
import math
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import Bool, Float32MultiArray

from rover_link.steady import steady_timer


class ManualDrive(Node):
    def __init__(self):
        super().__init__('manual_drive')
        self.declare_parameter('publish_hz', 20.0)
        self.declare_parameter('cmd_timeout', 0.5)
        self.declare_parameter('estop_hold', 1.0)    # s the output stays 0, 0 after the last e-stop message
        self.declare_parameter('neutral', 0.05)      # |value| at or below this counts as stick neutral

        self.cmd_timeout = float(self.get_parameter('cmd_timeout').value)
        self.estop_hold = float(self.get_parameter('estop_hold').value)
        self.neutral = float(self.get_parameter('neutral').value)

        self.manual = False          # latest /manual value, False until one arrives
        self.drive = (0.0, 0.0)      # latest (right, left) from /rover/drive_cmd
        self.k = 0.5                 # variable less than one to restrict speed
        self.last_cmd_time = None    # monotonic time of the last /rover/drive_cmd
        self.quiet = False           # manual is on but drive_cmd is stale (only for logging)
        self.estopped = False        # an e-stop is active: output is held at 0, 0
        self.estop_time = None       # monotonic time of the last e-stop message
        self.waiting_neutral = False # hold is over, waiting for the stick (only for logging)

        self.create_subscription(Float32MultiArray, '/rover/drive_cmd', self.on_drive, 10)
        self.create_subscription(Bool, '/rover/manual', self.on_manual, 10)
        self.create_subscription(Bool, '/rover/estop', self.on_estop, 10)
        self.out_pub = self.create_publisher(Float32MultiArray, '/rover/drive_out', 10)
        steady_timer(self, 1.0 / float(self.get_parameter('publish_hz').value),
                     self.publish_out)
        self.get_logger().info('Manual drive ready: output stays 0, 0 until /manual is True')

    def on_manual(self, msg: Bool):
        if msg.data != self.manual:
            self.manual = msg.data
            self.get_logger().info(f'Manual driving {"ON" if msg.data else "OFF"}')
            self.publish_out()  # react now, don't wait for the next tick

    def on_estop(self, msg: Bool):
        if not msg.data:
            return
        if not self.estopped:
            self.get_logger().warn('E-STOP: output held at 0, 0')
        self.estopped = True
        self.waiting_neutral = False
        self.estop_time = time.monotonic()  # every e-stop message restarts the hold
        self.publish_out()  # stop now, don't wait for the next tick

    def on_drive(self, msg: Float32MultiArray):
        if len(msg.data) != 2 or not all(math.isfinite(v) for v in msg.data):
            self.get_logger().warn('drive_cmd needs exactly two finite values [right, left]: ignored',
                                   throttle_duration_sec=2.0)
            return
        # clamp to -1..1, multiply by k to limit speed, and store; publish_out sends it at a steady rate
        self.drive = tuple((max(-1.0, min(1.0, float(v))) * self.k) for v in msg.data)
        self.last_cmd_time = time.monotonic()

    def publish_out(self):
        now = time.monotonic()
        fresh = self.last_cmd_time is not None and now - self.last_cmd_time <= self.cmd_timeout

        if self.estopped and now - self.estop_time >= self.estop_hold and fresh:
            if max(abs(self.drive[0]), abs(self.drive[1])) <= self.neutral:
                self.estopped = False
                self.waiting_neutral = False
                self.get_logger().info('E-stop cleared (stick is neutral): driving allowed again')
            elif not self.waiting_neutral:
                self.waiting_neutral = True
                self.get_logger().info('E-stop hold is over: return the stick to neutral to drive again')

        quiet = self.manual and not fresh
        if quiet != self.quiet:
            self.quiet = quiet
            if quiet:
                self.get_logger().warn('No recent /rover/drive_cmd: output 0, 0')
            else:
                self.get_logger().info('/rover/drive_cmd is back')

        right, left = self.drive if (self.manual and fresh and not self.estopped) else (0.0, 0.0)
        self.out_pub.publish(Float32MultiArray(data=[right, left]))


def main(args=None):
    rclpy.init(args=args)
    node = ManualDrive()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.out_pub.publish(Float32MultiArray(data=[0.0, 0.0]))  # leave the output stopped
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()