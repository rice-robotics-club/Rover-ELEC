"""Gamepad teleop for the ESP32 actuator controller (plain Python, no ROS).

(docstring unchanged)
"""
import rclpy
from rclpy.node import Node

from sensor_msgs.msg import JointState
from sensor_msgs.msg import Joy

import math
import os
import struct
import sys
import time

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1")
os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")

import serial

#PORT = "/dev/ttyUSB0"
PORT = "/tmp/fm"
BAUD = 115200
RATE_HZ = 100
PERIOD = 1.0 / RATE_HZ

SYNC1 = 0xAA
SYNC2 = 0x55

ENABLE_FLAG = 0x80
ENABLE_FRAMES = 5

RS04_BIT = 0x01
RS02_BIT = 0x02
DYNAMIXEL_BIT = 0x04
ALL_ACTUATORS = RS04_BIT | RS02_BIT | DYNAMIXEL_BIT

AXIS_LEFT_STICK_Y = 1
AXIS_RIGHT_STICK_Y = 4
AXIS_LEFT_TRIGGER = 2
AXIS_RIGHT_TRIGGER = 5
BUTTON_Y = 3

STICK_THRESHOLD = 0.5
TRIGGER_THRESHOLD = 0.25

ACTUATOR_NAMES = [(RS04_BIT, "RS-04"), (RS02_BIT, "RS-02"), (DYNAMIXEL_BIT, "Dynamixel")]

# ---------------------------------------------------------------- Telemetry
# Packet: 0xAA 0x55 | 3 x [uint32 motorID, uint16 pos, vel, torque, temp] | crc
TELEMETRY_FMT = "<2B" + "IHHHH" * 3 + "B"
TELEMETRY_SIZE = struct.calcsize(TELEMETRY_FMT)          # 39 bytes
TELEMETRY_IDS = (0x07, 0x7F, 0x04)                       # Same order as ACTUATORS[]

# Ranges used by the firmware's floatToUint16() / the Robstride protocol
POS_RANGE = 4.0 * math.pi      # +/- rad
VEL_RANGE = 44.0               # +/- rad/s
TORQUE_RANGE = 17.0            # +/- N*m


def raw_to_float(raw, limit):
    """Inverse of floatToUint16(): map 0..65535 back to -limit..+limit."""
    return raw / 65535.0 * (2.0 * limit) - limit


class TelemetryParser:
    """Accumulates serial bytes and extracts the newest valid telemetry packet."""

    def __init__(self):
        self.buffer = bytearray()
        self.latest = None          # List of dicts, one per actuator
        self.packets = 0

    def feed(self, data):
        self.buffer += data
        while True:
            # Find the sync bytes
            idx = self.buffer.find(bytes((SYNC1, SYNC2)))
            if idx < 0:
                # Keep a trailing 0xAA in case the 0x55 hasn't arrived yet
                self.buffer = self.buffer[-1:] if self.buffer[-1:] == bytes((SYNC1,)) else bytearray()
                return
            del self.buffer[:idx]
            if len(self.buffer) < TELEMETRY_SIZE:
                return

            fields = struct.unpack(TELEMETRY_FMT, bytes(self.buffer[:TELEMETRY_SIZE]))
            ids = tuple(fields[2 + 5 * k] for k in range(3))

            # The firmware CRC is always 0, so the motor IDs act as the sanity check
            if ids != TELEMETRY_IDS:
                del self.buffer[:1]     # False sync, skip one byte and rescan
                continue

            del self.buffer[:TELEMETRY_SIZE]
            self.latest = []
            for k in range(3):
                mid, pos, vel, tq, temp = fields[2 + 5 * k: 7 + 5 * k]
                self.latest.append({
                    "id": mid,
                    "position": raw_to_float(pos, POS_RANGE),      # rad
                    "velocity": raw_to_float(vel, VEL_RANGE),      # rad/s
                    "torque": raw_to_float(tq, TORQUE_RANGE),      # N*m
                    "temperature": temp / 10.0,                    # degC (verify scaling)
                })
            self.packets += 1


# ---------------------------------------------------------------- ROS node
class JointStatePublisher(Node):

    def __init__(self):
        super().__init__('joint_state_publisher')
        self.positions = [-1.511, -0.9, 1.8]
        self.axes = [0.0] * 6
        self.buttons = [0] * 11
        self.have_telemetry = False

        self.publisher = self.create_publisher(JointState, '/joint_states', 10)
        self.controller_sub = self.create_subscription(
            Joy, '/controller_state', self.controller_callback, 10)
        self.timer = self.create_timer(0.01, self.publish_joint_states)

    def controller_callback(self, msg):
        self.axes = list(msg.axes)
        self.buttons = list(msg.buttons)

    def publish_joint_states(self):
        msg = JointState()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.name = ['dof_shoulder', 'dof_elbow', 'dof_claw']
        msg.position = list(self.positions)
        self.publisher.publish(msg)


def checksum(enable, forward, backward):
    return enable ^ forward ^ backward


def build_frame(enable, forward, backward):
    return struct.pack("<6B", SYNC1, SYNC2, enable, forward, backward,
                       checksum(enable, forward, backward))


def stick_direction(value):
    if value < -STICK_THRESHOLD:
        return 1
    if value > STICK_THRESHOLD:
        return -1
    return 0


def trigger_direction(right, left):
    forward = right > TRIGGER_THRESHOLD
    backward = left > TRIGGER_THRESHOLD
    if forward and not backward:
        return 1
    if backward and not forward:
        return -1
    return 0


def read_directions(node):
    return {
        RS04_BIT: -stick_direction(node.axes[AXIS_LEFT_STICK_Y]),
        RS02_BIT: stick_direction(node.axes[AXIS_RIGHT_STICK_Y]),
        DYNAMIXEL_BIT: trigger_direction(node.axes[AXIS_RIGHT_TRIGGER],
                                         node.axes[AXIS_LEFT_TRIGGER])
    }


def directions_to_bytes(directions):
    forward = 0
    backward = 0
    for bit, direction in directions.items():
        if direction > 0:
            forward |= bit
        elif direction < 0:
            backward |= bit
    return forward, backward


def status_lines(enable, forward, backward, engaged, connected, telemetry=None):
    """Three lines: command bits, actuator states, and decoded telemetry."""
    bits = f"en={enable:08b} fwd={forward:08b} bwd={backward:08b}"
    if not connected:
        return [bits, "CONTROLLER DISCONNECTED - all actuators stopped", ""]

    actuators = []
    for bit, name in ACTUATOR_NAMES:
        eng = "ENG" if engaged & bit else "---"
        if forward & bit:
            motion = "FWD "
        elif backward & bit:
            motion = "BACK"
        else:
            motion = "STOP"
        actuators.append(f"{name}: {eng} {motion}")

    if telemetry is None:
        tele = "pos: no telemetry"
    else:
        tele = "pos(rad) " + "  ".join(
            f"{name}:{t['position']:+7.3f}"
            for (_, name), t in zip(ACTUATOR_NAMES[:2], telemetry[:2]))
    return [bits, "  |  ".join(actuators), tele]


def redraw(lines, drawn):
    out = ""
    if drawn:
        out += f"\x1b[{len(lines) - 1}A"
    out += "\n".join("\r\x1b[K" + line for line in lines)
    sys.stdout.write(out)
    sys.stdout.flush()


def main():

    rclpy.init(args=None)

    node = JointStatePublisher()

    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = BAUD
    ser.dtr = False
    ser.rts = False
    ser.timeout = 0               # Non-blocking reads so the control loop keeps its rate
    ser.open()

    parser = TelemetryParser()

    enable_frames = 0
    engaged = 0
    y_was_pressed = False
    last_lines = None
    drawn = False

    try:
        next_tick = time.monotonic()
        while True:
            directions = read_directions(node)
            y_pressed = bool(node.buttons[BUTTON_Y])
            if y_pressed and not y_was_pressed:
                enable_frames = ENABLE_FRAMES
                engaged |= ALL_ACTUATORS
            y_was_pressed = y_pressed
            forward, backward = directions_to_bytes(directions)
            enable = (ENABLE_FLAG | ALL_ACTUATORS) if enable_frames > 0 else 0
            if enable_frames > 0:
                enable_frames -= 1
            ser.write(build_frame(enable, forward, backward))

            # Read and Decode Any Telemetry That Has Arrived
            waiting = ser.in_waiting
            if waiting:
                parser.feed(ser.read(waiting))

            # Dead-reckon First, Then Overwrite With Measured Positions When Available
            node.positions[0] += ((forward & 0b001) - (backward & 0b001)) / RATE_HZ
            node.positions[1] += (((forward & 0b010) >> 1) - ((backward & 0b010) >> 1)) / RATE_HZ
            node.positions[2] += (((forward & 0b100) >> 2) - ((backward & 0b100) >> 2)) / RATE_HZ

            if parser.latest is not None:
                node.have_telemetry = True
                node.positions[0] = parser.latest[0]["position"]   # RS-04 -> shoulder
                node.positions[1] = parser.latest[1]["position"]   # RS-02 -> elbow
                # Claw (Dynamixel) stays dead-reckoned, the firmware sends zeros for it

            lines = status_lines(enable, forward, backward, engaged, True, parser.latest)
            if lines != last_lines:
                redraw(lines, drawn)
                drawn = True
                last_lines = lines

            rclpy.spin_once(node, timeout_sec=0)

            next_tick += PERIOD
            delay = next_tick - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            else:
                next_tick = time.monotonic()
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()
        print()
        try:
            for _ in range(10):
                ser.write(build_frame(0, 0, 0))
                time.sleep(PERIOD)
            ser.close()
        except serial.SerialException:
            pass


if __name__ == "__main__":
    main()