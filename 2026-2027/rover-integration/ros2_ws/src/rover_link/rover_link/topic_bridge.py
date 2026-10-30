#!/usr/bin/env python3
"""Generic radio bridge for one std_msgs/String topic in each direction.

Run the same node on both machines, each with its own topic names:
    tx_topic : local topic whose messages are sent OUT over the radio
    rx_topic : local topic that messages arriving FROM the radio are published on

Params: port, baud, tx_topic, rx_topic, max_hz, stats_period

Only the newest pending message is sent each 1/max_hz seconds, so a fast
publisher cannot flood the radio (skipped messages are counted as "limited").
Messages whose packet would exceed MAX_PACKET bytes are dropped with a warning.
"""
import rclpy
import serial
from rclpy.node import Node
from std_msgs.msg import String

from rover_link.protocol import LineDecoder, encode
from rover_link.steady import steady_timer

MAX_PACKET = 200  # bytes on the wire, including the newline


class TopicBridge(Node):
    def __init__(self):
        super().__init__('topic_bridge')
        self.declare_parameter('port', '/dev/ttyUSB0')
        self.declare_parameter('baud', 57600)
        self.declare_parameter('tx_topic', '')
        self.declare_parameter('rx_topic', '')
        self.declare_parameter('max_hz', 5.0)
        self.declare_parameter('stats_period', 5.0)

        port = self.get_parameter('port').value
        baud = int(self.get_parameter('baud').value)
        tx_topic = self.get_parameter('tx_topic').value
        rx_topic = self.get_parameter('rx_topic').value
        if not tx_topic and not rx_topic:
            raise ValueError('set at least one of tx_topic / rx_topic')

        self.ser = serial.Serial(port, baud, timeout=0)
        self.decoder = LineDecoder()
        self.pending = None
        self.tx_count = self.rx_count = self.limited = self.too_big = 0

        if tx_topic:
            self.create_subscription(String, tx_topic, self.on_tx, 10)
        self.rx_pub = self.create_publisher(String, rx_topic, 10) if rx_topic else None

        steady_timer(self, 1.0 / float(self.get_parameter('max_hz').value), self.send)
        steady_timer(self, 0.02, self.read_radio)
        steady_timer(self, float(self.get_parameter('stats_period').value), self.log_stats)
        self.get_logger().info(
            f'Radio on {port} @ {baud}: sending "{tx_topic}", publishing "{rx_topic}"')

    def on_tx(self, msg: String):
        if self.pending is not None:
            self.limited += 1  # replaced before it could be sent
        self.pending = msg.data

    def send(self):
        if self.pending is None:
            return
        packet = encode(t='str', d=self.pending)
        self.pending = None
        if len(packet) > MAX_PACKET:
            self.too_big += 1
            self.get_logger().warn(
                f'Message too long for the radio link ({len(packet)} > {MAX_PACKET} bytes), dropped',
                throttle_duration_sec=2.0)
            return
        try:
            self.ser.write(packet)
            self.tx_count += 1
        except (serial.SerialException, OSError) as e:
            self.get_logger().error(f'Radio write failed: {e}', throttle_duration_sec=2.0)

    def read_radio(self):
        try:
            data = self.ser.read(256)
        except (serial.SerialException, OSError) as e:
            self.get_logger().error(f'Radio read failed: {e}', throttle_duration_sec=2.0)
            return
        for msg in self.decoder.feed(data):
            if msg.get('t') == 'str' and isinstance(msg.get('d'), str) and self.rx_pub:
                self.rx_pub.publish(String(data=msg['d']))
                self.rx_count += 1

    def log_stats(self):
        self.get_logger().info(
            f'radio sent={self.tx_count} received={self.rx_count} '
            f'rate-limited={self.limited} too-long={self.too_big}')


def main(args=None):
    rclpy.init(args=args)
    node = TopicBridge()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.ser.close()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
