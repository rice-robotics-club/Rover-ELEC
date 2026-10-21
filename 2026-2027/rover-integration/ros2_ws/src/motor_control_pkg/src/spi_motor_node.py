#!/usr/bin/env python3
import spidev
import rclpy
from rclpy.node import Node


class SPIMotorNode(Node):

    def __init__(self):
        super().__init__('spi_motor_node')

        self.spi = spidev.SpiDev()
        try:
            self.spi.open(0, 0)            # /dev/spidev0.0
            self.spi.max_speed_hz = 1000000
            self.spi.mode = 0b00
            self.get_logger().info('SPI opened on /dev/spidev0.0 at 1 MHz')
        except FileNotFoundError:
            self.get_logger().error('Could not open /dev/spidev0.0. Check jetson-io and permissions.')
            raise

        self.timer = self.create_timer(1.0, self.send_one)

    def send_one(self):
        self.spi.xfer2([1])
        self.get_logger().info('Sent 0x01 over SPI')

    def destroy_node(self):
        self.spi.close()
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = SPIMotorNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()