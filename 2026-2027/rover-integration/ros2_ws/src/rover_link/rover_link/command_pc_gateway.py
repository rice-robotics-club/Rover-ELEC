#!/usr/bin/env python3
"""Command-station gateway: ROS 2 drive / e-stop commands  ->  RFD900x radio A.

Subscribes: /station/drive_cmd (std_msgs/Float32MultiArray)  data = [right, left], each -1..1
            /station/estop     (std_msgs/Bool)               True = send e-stop
Publishes:  /station/link_ok   (std_msgs/Bool)   True while the rover is acking AND accepting commands
Params:     port, baud, send_hz, cmd_timeout, link_timeout

Radio protocol (see the protocols PDF). One message, before COBS:
    [type][sequence number][payload ...][CRC-16 high byte][CRC-16 low byte]
    type = bit 7: 0 = command / 1 = data, bits 6-0: the code
Then the whole thing is COBS encoded and a 0x00 byte is added to mark the end.

Safety: the last drive command is re-sent at send_hz (the rover's watchdog needs a
heartbeat), and if no command arrives from the control node for cmd_timeout
seconds the gateway sends drive 0, 0 by itself.
"""
import math
import time

import rclpy
import serial
from rclpy.node import Node
from std_msgs.msg import Bool, Float32MultiArray

from rover_link.steady import steady_timer

STALL_S = 0.3   # a timer/callback that runs this much later than expected is logged
SLOW_S = 0.05   # a serial write or publish slower than this is logged

# ---- protocol constants (codes from the protocols PDF) ---------------------
COMMAND, DATA = 0, 1        # the top bit of the type byte
CMD_DRIVE = 1               # command 1: drive motors, payload = right byte, left byte
CMD_ESTOP = 3               # command 3: turn off drive motors (e-stop), no payload
DATA_ACK = 2                # data 2: acknowledgement, payload = one status byte
ESTOP_REPEATS = 5           # a momentary e-stop is sent this many times in a row
MAX_FRAME = 256             # bytes; more than this with no 0x00 in sight is garbage


# ---- protocol helpers (these can move into protocol.py later) --------------
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


def to_byte(v: float) -> int:
    """-1.0 .. 1.0 -> signed 8-bit (value x 127, -127..127) as an unsigned byte."""
    return round(max(-1.0, min(1.0, v)) * 127) & 0xFF


def build_message(kind: int, code: int, seq: int, payload: bytes = b'') -> bytes:
    """One complete radio message: COBS( type, seq, payload, CRC ) + 0x00."""
    body = bytes([(kind << 7) | code, seq]) + payload
    body += crc16_spi_fujitsu(body).to_bytes(2, 'big')
    return cobs_encode(body) + b'\x00' #cobs_encode the body (remove confusing zeros) and add a 0x00 to mark the end of the message


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


class CommandPCGateway(Node):
    def __init__(self):
        super().__init__('command_pc_gateway')
        self.declare_parameter('port', '/dev/ttyUSB0') # overriden by --ros-args -p port:=...
        self.declare_parameter('baud', 57600)
        self.declare_parameter('send_hz', 10.0)
        self.declare_parameter('cmd_timeout', 1.0)
        self.declare_parameter('link_timeout', 2.0)

        port = self.get_parameter('port').value
        baud = int(self.get_parameter('baud').value)
        self.cmd_timeout = float(self.get_parameter('cmd_timeout').value)
        self.link_timeout = float(self.get_parameter('link_timeout').value)

        # Raises if the radio is missing: better than running with no link.
        self.ser = serial.Serial(port, baud, timeout=0)
        self.decoder = MessageDecoder()

        self.drive = (0.0, 0.0)      # latest (right, left) from the control node
        self.seq = 0                 # sequence number of the next message, wraps 255 -> 0
        self.estop_left = 0          # how many more e-stop messages to send
        self.last_cmd_time = None    # monotonic (only goes up) time of last control message
        self.last_ack_time = None    # monotonic time of last ack (acknowledgment) from rover
        self.rover_accepting = False # status byte of the last ack: 1 = rover accepts commands
        self.link_ok = False
        self._ticks = {}             # callback name -> monotonic time of last run

        self.create_subscription(Float32MultiArray, '/station/drive_cmd', self.on_drive, 10) #make a subscription to /station/drive_cmd
        self.create_subscription(Bool, '/station/estop', self.on_estop, 10) #make a subscription to /station/estop
        self.link_pub = self.create_publisher(Bool, '/station/link_ok', 10) #make a publisher for if the link is good
        steady_timer(self, 1.0 / float(self.get_parameter('send_hz').value), #heartbeat 10hz
                     self.send_state)
        steady_timer(self, 0.02, self.read_radio) #to read the radio every 20ms
        self.get_logger().info(f'Radio A open on {port} @ {baud} baud')

    # ---- diagnostics ----------------------------------------------------
    def _tick_check(self, name):
        """Warn if a callback runs much later than its period (executor stall)."""
        now = time.monotonic()
        last = self._ticks.get(name)
        self._ticks[name] = now
        if last is not None and now - last > STALL_S:
            self.get_logger().warn(f'STALL: {name} did not run for {now - last:.2f} s')

    # ---- outgoing -------------------------------------------------------
    def on_drive(self, msg: Float32MultiArray): #the callback function in the drive subscriber
        self._tick_check('on_drive') #check for a stall
        if len(msg.data) != 2 or not all(math.isfinite(v) for v in msg.data):
            self.get_logger().warn('drive_cmd needs exactly two finite values [right, left]: ignored',
                                   throttle_duration_sec=2.0)
            return
        self.last_cmd_time = time.monotonic()
        # clamp to -1..1 and store; the heartbeat (send_state) sends it, so a fast
        # joystick can't flood the radio
        self.drive = tuple(max(-1.0, min(1.0, float(v))) for v in msg.data)

    def on_estop(self, msg: Bool):
        if not msg.data:
            return
        self.get_logger().warn('E-STOP: sending e-stop')
        self.drive = (0.0, 0.0)
        self.estop_left = ESTOP_REPEATS
        self.send_state()  # don't wait for the next heartbeat

    def send_state(self):
        self._tick_check('send_state')
        now = time.monotonic()
        if self.estop_left > 0: # an e-stop takes the heartbeat slot until it has been sent enough times
            self.estop_left -= 1
            self._send(COMMAND, CMD_ESTOP)
            return
        if self.drive != (0.0, 0.0) and (self.last_cmd_time is None
                                         or now - self.last_cmd_time > self.cmd_timeout):
            self.drive = (0.0, 0.0) # stop if it hasnt gotten a drive command yet or if its been the timeout (who cares)
            self.get_logger().warn('Control node went quiet: sending drive 0, 0')
        right, left = self.drive
        self._send(COMMAND, CMD_DRIVE, bytes([to_byte(right), to_byte(left)])) #drive message: right byte then left byte

    def _send(self, kind, code, payload=b''):
        packet = build_message(kind, code, self.seq, payload) #COBS( type, seq, payload, CRC ) + 0x00
        self.seq = (self.seq + 1) & 0xFF #next sequence number, wraps 255 -> 0
        self._write(packet)

    #now we have e.g. 07 01 07 5F C0 3A F9 00 for drive right 0.75, left -0.5, sequence 7

    def _write(self, packet: bytes):
        t0 = time.monotonic()
        try:
            self.ser.write(packet) #serial write the packet (raw bytes, ends with 0x00)
        except (serial.SerialException, OSError) as e:
            self.get_logger().error(f'Radio write failed: {e}',
                                    throttle_duration_sec=2.0)
        took = time.monotonic() - t0
        if took > SLOW_S:
            self.get_logger().warn(f'SLOW: serial write took {took:.2f} s')

    # ---- incoming -------------------------------------------------------
    def read_radio(self):
        self._tick_check('read_radio')
        try:
            data = self.ser.read(256) #serial read up to 256 bytes
        except (serial.SerialException, OSError) as e:
            self.get_logger().error(f'Radio read failed: {e}',
                                    throttle_duration_sec=2.0)
            data = b''
        for kind, code, seq, payload in self.decoder.feed(data): #look at incoming msgs (already un-COBSed and CRC checked)
            if kind == DATA and code == DATA_ACK and len(payload) == 1: #an ack carries one status byte
                self.last_ack_time = time.monotonic()
                self.rover_accepting = payload[0] == 1
        self._update_link()

    def _update_link(self):
        ok = (self.last_ack_time is not None
              and time.monotonic() - self.last_ack_time < self.link_timeout
              and self.rover_accepting) #the rover must also say it is accepting commands
        if ok != self.link_ok: #ok could be false, if it is or if its now true update
            self.link_ok = ok
            self.get_logger().info('Rover link UP' if ok else 'Rover link LOST')
        t0 = time.monotonic()
        self.link_pub.publish(Bool(data=self.link_ok)) #publish the boolean of if the links good (local, not over the radio)
        took = time.monotonic() - t0
        if took > SLOW_S:
            self.get_logger().warn(f'SLOW: ROS publish took {took:.2f} s')

    def close(self):
        for _ in range(3):  # best effort: leave the rover stopped
            self._send(COMMAND, CMD_DRIVE, bytes([to_byte(0.0), to_byte(0.0)]))
        self.ser.close()


def main(args=None):
    rclpy.init(args=args)
    node = CommandPCGateway()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.close()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
