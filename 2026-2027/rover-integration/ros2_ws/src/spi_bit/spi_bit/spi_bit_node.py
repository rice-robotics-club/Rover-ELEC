#!/usr/bin/env python3
"""ROS 2 node that sends a single 0 or 1 byte over SPI (Jetson Orin Nano).

Topic:  spi_bit/set  (std_msgs/Bool)  -> True sends 0x01, False sends 0x00
Params: bus, device, speed_hz, mode, initial_value, resend_period (s, 0 = only on change)
"""
import rclpy
from rclpy.node import Node
from std_msgs.msg import Bool
import spidev


class SpiBitNode(Node):
    def __init__(self):
        super().__init__('spi_bit_node')
        self.declare_parameter('bus', 0)            # /dev/spidev<bus>.<device>
        self.declare_parameter('device', 0)
        self.declare_parameter('speed_hz', 1_000_000)
        self.declare_parameter('mode', 0)           # SPI mode 0-3
        self.declare_parameter('initial_value', False)
        self.declare_parameter('resend_period', 0.0)

        bus = self.get_parameter('bus').value
        dev = self.get_parameter('device').value

        self.spi = spidev.SpiDev()
        self.spi.open(bus, dev)
        self.spi.max_speed_hz = int(self.get_parameter('speed_hz').value)
        self.spi.mode = int(self.get_parameter('mode').value)
        self.spi.bits_per_word = 8

        self.value = bool(self.get_parameter('initial_value').value)
        self.send()

        self.create_subscription(Bool, 'spi_bit/set', self.on_msg, 10)

        period = float(self.get_parameter('resend_period').value)
        if period > 0.0:
            self.create_timer(period, self.send)

        self.get_logger().info(
            f'Opened /dev/spidev{bus}.{dev} @ {self.spi.max_speed_hz} Hz, mode {self.spi.mode}')

    def send(self):
        self.spi.xfer2([1 if self.value else 0])

    def on_msg(self, msg: Bool):
        changed = msg.data != self.value
        self.value = msg.data
        self.send()
        if changed:  # the radio gateway republishes at 10 Hz; log changes only
            self.get_logger().info(f'Sent {int(self.value)}')

    def destroy_node(self):
        try:
            self.spi.close()
        finally:
            super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = SpiBitNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
