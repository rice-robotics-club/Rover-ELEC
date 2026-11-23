#!/usr/bin/env python3
"""Rover gateway (Jetson): RFD900x radio B  ->  local ROS 2 topics.

A gateway only: it decodes what arrives over the radio and publishes it on local
topics. It does not talk SPI; a separate node listens to these topics for that.

Publishes:  /rover/drive_cmd (std_msgs/Float32MultiArray)  data = [right, left], each -1..1
            /rover/estop     (std_msgs/Bool)               True each time an e-stop message arrives
            /rover/link_ok   (std_msgs/Bool)               False while the radio is silent (failsafe)
            /rover/radio_rx  (std_msgs/UInt8MultiArray)    every other valid message: [type byte, payload ...]
            /rover/manual (std_msgs/Bool) True each time a drive_cmd arrives

Params:     port, baud, watchdog_timeout, ack_hz

Radio protocol (see the protocols PDF). One message, before COBS:
    [type][sequence number][payload ...][CRC-16 high byte][CRC-16 low byte]
    type = bit 7: 0 = command / 1 = data, bits 6-0: the code
Then the whole thing is COBS encoded and a 0x00 byte is added to mark the end.

Watchdog: if no valid message arrives for watchdog_timeout seconds (radio lost,
PC gateway crashed, ...) the node publishes drive 0, 0 and link_ok False until
messages resume. It also starts in the failsafe state, so it publishes drive 0, 0
until the first valid message is received.
"""
import time

import rclpy
import serial
from rclpy.node import Node
from std_msgs.msg import Bool, Float32MultiArray, UInt8MultiArray

from rover_link.steady import steady_timer

# ---- protocol constants (codes from the protocols PDF) ---------------------
COMMAND, DATA = 0, 1        # the top bit of the type byte
CMD_DRIVE = 1               # command 1: drive motors, payload = right byte, left byte
CMD_ESTOP = 3               # command 3: turn off drive motors (e-stop), no payload
DATA_ACK = 2                # data 2: acknowledgement, payload = one status byte
MAX_FRAME = 256             # bytes; more than this with no 0x00 in sight is garbage


# ---- protocol helpers (same as in command_pc_gateway.py; they can move into
# ---- protocol.py later so both gateways import them) -------------------------
def crc16_spi_fujitsu(data: bytes) -> int:
    """CRC-16/SPI-FUJITSU: poly 0x1021, init 0x1D0F, no reflection, no final XOR."""
    crc = 0x1D0F
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    """Rewrite data so it contains no 0x00 (standard COBS)."""
    out, block = bytearray(), bytearray()
    for b in data:
        if b == 0:
            out.append(len(block) + 1)
            out += block
            block = bytearray()
        else:
            block.append(b)
            if len(block) == 254:      # longest block a code byte can describe
                out.append(0xFF)
                out += block
                block = bytearray()
    out.append(len(block) + 1)
    out += block
    return bytes(out)


def cobs_decode(data: bytes):
    """Undo cobs_encode. Returns None if the data is not valid COBS."""
    out, i = bytearray(), 0
    while i < len(data):
        code = data[i]
        end = i + code
        if code == 0 or end > len(data):
            return None
        out += data[i + 1:end]
        i = end
        if code != 0xFF and i < len(data):
            out.append(0)
    return bytes(out)


def from_byte(b: int) -> float:
    """One signed byte (value x 127, twos complement) -> -1.0 .. 1.0. 0x80 (-128) counts as -1.0."""
    return max(-1.0, (b - 256 if b > 127 else b) / 127)


def build_message(kind: int, code: int, seq: int, payload: bytes = b'') -> bytes:
    """One complete radio message: COBS( type, seq, payload, CRC ) + 0x00."""
    body = bytes([(kind << 7) | code, seq]) + payload
    body += crc16_spi_fujitsu(body).to_bytes(2, 'big')
    return cobs_encode(body) + b'\x00'


class MessageDecoder:
    """Collects raw radio bytes and returns complete, CRC-checked messages.

    feed() returns a list of (kind, code, seq, payload). Messages that are not
    valid COBS, are too short, or fail the CRC are dropped.
    """

    def __init__(self):
        self._buf = bytearray()

    def feed(self, data: bytes):
        self._buf.extend(data)
        messages = []
        while True:
            end = self._buf.find(b'\x00')
            if end < 0:
                break
            frame = bytes(self._buf[:end])
            del self._buf[:end + 1]
            if not frame:
                continue
            body = cobs_decode(frame)
            if body is None or len(body) < 4:          # type, seq, 2 CRC bytes
                continue
            if crc16_spi_fujitsu(body[:-2]) != int.from_bytes(body[-2:], 'big'):
                continue
            messages.append((body[0] >> 7, body[0] & 0x7F, body[1], body[2:-2]))
        if len(self._buf) > MAX_FRAME:  # no 0x00 in sight: junk, start over
            self._buf.clear()
        return messages


class RoverGateway(Node):
    def __init__(self):
        super().__init__('rover_gateway')
        self.declare_parameter('port', '/dev/ttyUSB0')
        self.declare_parameter('baud', 57600)
        self.declare_parameter('watchdog_timeout', 0.5)
        self.declare_parameter('ack_hz', 2.0) #how often the rover sends acknowledgements

        port = self.get_parameter('port').value
        baud = int(self.get_parameter('baud').value)
        self.watchdog_timeout = float(self.get_parameter('watchdog_timeout').value)

        self.ser = serial.Serial(port, baud, timeout=0)
        self.decoder = MessageDecoder()

        self.seq = 0                 # sequence number of the next message we send, wraps 255 -> 0
        self.moving = False          # last drive command was not 0, 0 (only used for logging)
        self.last_rx_time = None
        self.failsafe = True

        self.drive_pub = self.create_publisher(Float32MultiArray, '/rover/drive_cmd', 10) #make a local publisher for the drive cmd [right, left]
        self.estop_pub = self.create_publisher(Bool, '/rover/estop', 10) #local publisher for e-stop
        self.manual_pub = self.create_publisher(Bool, '/rover/manual', 10) #local publisher for manual
        self.link_pub = self.create_publisher(Bool, '/rover/link_ok', 10) #local publisher for if the radio link is alive
        self.raw_pub = self.create_publisher(UInt8MultiArray, '/rover/radio_rx', 10) #everything else received, as raw bytes
        steady_timer(self, 0.02, self.read_radio) #these functions are now executed forever after rclpy.spin goes through
        steady_timer(self, 0.1, self.check_watchdog)
        steady_timer(self, 1.0 / float(self.get_parameter('ack_hz').value),
                     self.send_ack)
        self.get_logger().info(f'Radio B open on {port} @ {baud} baud')

    def read_radio(self):
        try:
            data = self.ser.read(256) #read up to 256 bytes
        except (serial.SerialException, OSError) as e:
            self.get_logger().error(f'Radio read failed: {e}',
                                    throttle_duration_sec=2.0)
            return
        for kind, code, seq, payload in self.decoder.feed(data): #for each message (already un-COBSed and CRC checked)
            self.last_rx_time = time.monotonic() #any valid message means the radio is alive: last receive time is now now
            if self.failsafe: #go out of failsafe if in it
                self.failsafe = False
                self.get_logger().info('Radio link established')
            if kind == COMMAND and code == CMD_DRIVE and len(payload) == 2: #drive: right byte then left byte
                self.manual_pub.publish(Bool(data=True))
                right, left = from_byte(payload[0]), from_byte(payload[1])
                moving = bool(right or left)
                if moving != self.moving: #log only when driving starts or stops
                    self.moving = moving
                    self.get_logger().info(f'Drive {"moving" if moving else "stopped"}')
                self.drive_pub.publish(Float32MultiArray(data=[right, left])) #publish the value rover side
            elif kind == COMMAND and code == CMD_ESTOP and len(payload) == 0: #e-stop has no payload
                self.get_logger().warn('E-STOP received', throttle_duration_sec=1.0)
                self.estop_pub.publish(Bool(data=True))
            else: #anything else (other codes, data messages, odd payload sizes) is passed on as it is
                self.get_logger().info(f'Passing on message: kind {kind} code {code} ({len(payload)} payload bytes)',
                                       throttle_duration_sec=1.0)
                self.raw_pub.publish(UInt8MultiArray(data=[(kind << 7) | code] + list(payload))) #type byte, then payload

    def check_watchdog(self):
        stale = (self.last_rx_time is None
                 or time.monotonic() - self.last_rx_time > self.watchdog_timeout)
        if stale:
            if not self.failsafe:
                self.failsafe = True # go into failsafe if its stale -> stops driving bc we dont know what is wanted (who cares)
                self.moving = False
                self.get_logger().warn('Radio silent: failsafe, drive 0, 0')
            self.drive_pub.publish(Float32MultiArray(data=[0.0, 0.0])) #publish it locally (so whatever listens stops)
        self.link_pub.publish(Bool(data=not stale)) #tell local nodes whether the radio link is alive

    def send_ack(self):
        packet = build_message(DATA, DATA_ACK, self.seq,
                               bytes([int(not self.failsafe)])) #ack: one status byte, 1 = accepting commands, 0 = failsafe
        self.seq = (self.seq + 1) & 0xFF #next sequence number, wraps 255 -> 0
        try:
            self.ser.write(packet) #send over the radio link serially the bytes, ends with 0x00
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
        node.drive_pub.publish(Float32MultiArray(data=[0.0, 0.0]))
        node.link_pub.publish(Bool(data=False))
        node.ser.close()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()