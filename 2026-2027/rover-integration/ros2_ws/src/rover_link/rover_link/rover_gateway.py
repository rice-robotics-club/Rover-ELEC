#!/usr/bin/env python3
"""Rover gateway (Jetson): RFD900x radio B  ->  ROS 2 motor command.

Publishes:  /rover/motor_cmd (std_msgs/Bool)   feed this to the SPI node
Params:     port, baud, watchdog_timeout, ack_hz

Watchdog: if no valid packet arrives for watchdog_timeout seconds (radio lost,
PC gateway crashed, ...) the node publishes motor OFF until packets resume.
It also starts in the failsafe state, so the motor is OFF until the first
valid packet is received.
"""
import time

import rclpy
import serial
from rclpy.node import Node
from std_msgs.msg import Bool

from rover_link.protocol import LineDecoder, encode, motor_value
from rover_link.steady import steady_timer


class RoverGateway(Node):
    def __init__(self):
        super().__init__('rover_gateway')
        self.declare_parameter('port', '/dev/ttyUSB0')
        self.declare_parameter('baud', 57600)
        self.declare_parameter('watchdog_timeout', 0.5)
        self.declare_parameter('ack_hz', 2.0)

        port = self.get_parameter('port').value
        baud = int(self.get_parameter('baud').value)
        self.watchdog_timeout = float(self.get_parameter('watchdog_timeout').value)

        self.ser = serial.Serial(port, baud, timeout=0)
        self.decoder = LineDecoder()

        self.motor_on = False
        self.last_rx_time = None
        self.failsafe = True

        self.cmd_pub = self.create_publisher(Bool, '/rover/motor_cmd', 10)
        steady_timer(self, 0.02, self.read_radio)
        steady_timer(self, 0.1, self.check_watchdog)
        steady_timer(self, 1.0 / float(self.get_parameter('ack_hz').value),
                     self.send_ack)
        self.get_logger().info(f'Radio B open on {port} @ {baud} baud')

    def read_radio(self):
        try:
            data = self.ser.read(256)
        except (serial.SerialException, OSError) as e:
            self.get_logger().error(f'Radio read failed: {e}',
                                    throttle_duration_sec=2.0)
            return
        for msg in self.decoder.feed(data):
            value = motor_value(msg)
            if value is None:
                continue
            self.last_rx_time = time.monotonic()
            if self.failsafe:
                self.failsafe = False
                self.get_logger().info('Radio link established')
            if value != self.motor_on:
                self.get_logger().info(f'Motor {"ON" if value else "OFF"}')
            self.motor_on = value
            self.cmd_pub.publish(Bool(data=value))

    def check_watchdog(self):
        stale = (self.last_rx_time is None
                 or time.monotonic() - self.last_rx_time > self.watchdog_timeout)
        if stale:
            if not self.failsafe:
                self.failsafe = True
                self.get_logger().warn('Radio silent: failsafe, motor OFF')
            self.motor_on = False
            self.cmd_pub.publish(Bool(data=False))

    def send_ack(self):
        try:
            self.ser.write(encode(t='ack', v=int(self.motor_on),
                                  ok=int(not self.failsafe)))
        except (serial.SerialException, OSError) as e:
            self.get_logger().error(f'Radio write failed: {e}',
                                    throttle_duration_sec=2.0)


def main(args=None):
    rclpy.init(args=args)
    node = RoverGateway()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.cmd_pub.publish(Bool(data=False))
        node.ser.close()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
