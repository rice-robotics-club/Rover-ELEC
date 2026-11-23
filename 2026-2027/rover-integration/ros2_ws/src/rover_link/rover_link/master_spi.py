#!/usr/bin/env python3
"""SPI master (Jetson Orin Nano): ROS 2 drive command  ->  SPI frames to the first ESP32.

Subscribes: /rover/drive_out (std_msgs/Float32MultiArray)  data = [right, left], each -1..1
            (the topic name is the 'topic' parameter)
Params:     topic, bus, device, speed_hz, mode, spi_hz, cmd_timeout

SPI frame (fixed 10 bytes, see the protocols PDF):
    0xAA 0x55 | message code | sequence number | payload (4 bytes) | CRC-16 high | CRC-16 low
  message code 0x01 = drive; payload = right byte, left byte, 0x00, 0x00
  each value is one signed byte: value x 127 (-127..127), twos complement
  CRC-16/SPI-FUJITSU over message code, sequence number and payload (not the header)

A frame is sent every 1/spi_hz seconds whether or not a new command arrived, so the
ESP32 always has a fresh heartbeat. If no command arrives for cmd_timeout seconds
(or none has arrived yet), the frames carry 0, 0.
"""
import math
import time

import rclpy
import spidev
from rclpy.clock import Clock, ClockType
from rclpy.node import Node
from std_msgs.msg import Float32MultiArray

HEADER = bytes([0xAA, 0x55])
CODE_DRIVE = 0x01       # message code 1: set drive, payload = right byte, left byte
PAYLOAD_LEN = 4         # every frame has this many payload bytes, padded with zeros


# ---- protocol helpers -------------------------------------------------------
def crc16_spi_fujitsu(data: bytes) -> int:
    """CRC-16/SPI-FUJITSU: poly 0x1021, init 0x1D0F, no reflection, no final XOR."""
    crc = 0x1D0F
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def to_byte(v: float) -> int:
    """-1.0 .. 1.0 -> signed 8-bit (value x 127, -127..127) as an unsigned byte."""
    return round(max(-1.0, min(1.0, v)) * 127) & 0xFF


def build_frame(code: int, seq: int, payload: bytes) -> bytes:
    """0xAA 0x55 | code | seq | payload (padded to PAYLOAD_LEN) | CRC high | CRC low."""
    if len(payload) > PAYLOAD_LEN:
        raise ValueError(f'payload is {len(payload)} bytes, the frame holds {PAYLOAD_LEN}')
    body = bytes([code, seq]) + payload.ljust(PAYLOAD_LEN, b'\x00')
    return HEADER + body + crc16_spi_fujitsu(body).to_bytes(2, 'big')


def steady_timer(node, period_s, callback):
    """A timer on the steady (monotonic) clock, so wall-clock changes can't pause it."""
    try:
        return node.create_timer(
            period_s, callback, clock=Clock(clock_type=ClockType.STEADY_TIME))
    except TypeError:  # an rclpy without the clock argument
        node.get_logger().warn(
            'create_timer has no clock argument here; using ROS time instead')
        return node.create_timer(period_s, callback)


class MasterSpi(Node):
    def __init__(self):
        super().__init__('master_spi')
        self.declare_parameter('topic', '/rover/drive_out')
        self.declare_parameter('bus', 0)             # /dev/spidev<bus>.<device>
        self.declare_parameter('device', 0)          # chip select 0 = the first ESP32
        self.declare_parameter('speed_hz', 1_000_000)
        self.declare_parameter('mode', 0)            # SPI mode 0-3
        self.declare_parameter('spi_hz', 50.0)       # frames per second
        self.declare_parameter('cmd_timeout', 0.5)

        topic = self.get_parameter('topic').value
        bus = int(self.get_parameter('bus').value)
        dev = int(self.get_parameter('device').value)
        self.topic = topic
        self.cmd_timeout = float(self.get_parameter('cmd_timeout').value)

        # Raises if /dev/spidev<bus>.<dev> is missing: better than running with no SPI.
        self.spi = spidev.SpiDev()
        self.spi.open(bus, dev)
        self.spi.max_speed_hz = int(self.get_parameter('speed_hz').value)
        self.spi.mode = int(self.get_parameter('mode').value)
        self.spi.bits_per_word = 8

        self.drive = (0.0, 0.0)      # latest (right, left) from the topic
        self.last_cmd_time = None    # monotonic time of the last command
        self.stale = True            # True while we are sending 0, 0 because of no command
        self.seq = 0                 # sequence number of the next frame, wraps 255 -> 0
        self.last_reply = b''        # the ESP32's last reply bytes (same layout); not used yet

        self.create_subscription(Float32MultiArray, topic, self.on_drive, 10)
        steady_timer(self, 1.0 / float(self.get_parameter('spi_hz').value), self.send_frame)
        self.send_frame()  # start with a 0, 0 frame
        self.get_logger().info(
            f'Opened /dev/spidev{bus}.{dev} @ {self.spi.max_speed_hz} Hz, mode {self.spi.mode}; '
            f'sending "{topic}" as SPI drive frames')

    def on_drive(self, msg: Float32MultiArray):
        if len(msg.data) != 2 or not all(math.isfinite(v) for v in msg.data):
            self.get_logger().warn(f'{self.topic} needs exactly two finite values [right, left]: ignored',
                                   throttle_duration_sec=2.0)
            return
        # clamp to -1..1 and store; send_frame sends it at a steady rate
        self.drive = tuple(max(-1.0, min(1.0, float(v))) for v in msg.data)
        self.last_cmd_time = time.monotonic()

    def send_frame(self):
        stale = (self.last_cmd_time is None
                 or time.monotonic() - self.last_cmd_time > self.cmd_timeout)
        if stale != self.stale:
            self.stale = stale
            if stale:
                self.get_logger().warn(f'No {self.topic} for {self.cmd_timeout} s: sending 0, 0')
            else:
                self.get_logger().info(f'{self.topic} received: sending drive commands')
        right, left = (0.0, 0.0) if stale else self.drive
        frame = build_frame(CODE_DRIVE, self.seq, bytes([to_byte(right), to_byte(left)]))
        self.seq = (self.seq + 1) & 0xFF
        try:
            # SPI sends and receives at the same time; the reply bytes are kept for later
            self.last_reply = bytes(self.spi.xfer2(list(frame)))
        except OSError as e:
            self.get_logger().error(f'SPI transfer failed: {e}', throttle_duration_sec=2.0)

    def destroy_node(self):
        try:
            self.drive = (0.0, 0.0)
            self.last_cmd_time = None
            for _ in range(3):  # best effort: leave the motors stopped
                self.send_frame()
            self.spi.close()
        finally:
            super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = MasterSpi()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()