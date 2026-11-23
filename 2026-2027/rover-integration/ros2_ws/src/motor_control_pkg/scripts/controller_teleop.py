#!/usr/bin/env python3
"""ROS 2 Jazzy gamepad teleop node for the ESP32 actuator controller.

Data path:
    gamepad --(USB/Bluetooth)--> Jetson Nano --(joy_node publishes /joy)--> this node
            --(USB serial, 6-byte frames)--> ESP32 --> actuators

Each actuator only gets three states: forward, backward, or off. Frame format:
    0xAA 0x55 | enable | forward | backward | checksum (XOR of the 3 command bytes)

Bit layout (matches the order of ACTUATORS[] in the firmware):
    bit 0 -> Robstride 04 (ID 7)  left stick,  up = forward, down = backward
    bit 1 -> Robstride 02         right stick, up = forward, down = backward
    bit 2 -> Dynamixel            RT = forward, LT = backward
    bit 7 of the enable byte -> enable flag

Y button engages all actuators.

Axis/button numbers come from the joy_node mapping, NOT from pygame, so they differ
from the standalone script. Check yours with:  ros2 topic echo /joy
"""

import struct
import time

import rclpy
import serial
from rclpy.node import Node
from sensor_msgs.msg import Joy

SYNC1 = 0xAA
SYNC2 = 0x55

ENABLE_FLAG = 0x80

RS04_BIT = 0x01
RS02_BIT = 0x02
DYNAMIXEL_BIT = 0x04
ALL_ACTUATORS = RS04_BIT | RS02_BIT | DYNAMIXEL_BIT
ACTUATOR_BITS = (RS04_BIT, RS02_BIT, DYNAMIXEL_BIT)


def checksum(enable, forward, backward):
    """XOR of the three command bytes."""
    return enable ^ forward ^ backward


def build_frame(enable, forward, backward):
    """Pack sync bytes, the three command bytes, and the checksum into 6 bytes."""
    return struct.pack("<6B", SYNC1, SYNC2, enable, forward, backward,
                       checksum(enable, forward, backward))


class ActuatorTeleop(Node):
    def __init__(self):
        super().__init__("actuator_teleop")

        # ---- Serial link to the ESP32 ----
        # TODO: confirm the device name on the Jetson (ls /dev/ttyUSB* /dev/ttyACM*).
        self.declare_parameter("serial_port", "/dev/ttyUSB0")
        self.declare_parameter("baud", 115200)

        # ---- Timing ----
        # Must stay well above 1 / HOST_TIMEOUT_MS in the firmware (5 Hz in the original script).
        self.declare_parameter("rate_hz", 100.0)
        self.declare_parameter("enable_frames", 5)
        # If /joy goes quiet this long, stop everything (controller unplugged, joy_node died, ...).
        # Use a value that suits your joy_node autorepeat_rate (see the launch file).
        self.declare_parameter("joy_timeout_s", 0.5)

        # ---- Controller layout (sensor_msgs/Joy indices) ----
        # ROS joy_node SDL mapping for an Xbox-style pad.
        # Verify with `ros2 topic echo /joy` while moving each input, especially for the 8BitDo pad.
        self.declare_parameter("axis_left_stick_y", 1)
        self.declare_parameter("axis_right_stick_y", 3)
        self.declare_parameter("axis_left_trigger", 4)
        self.declare_parameter("axis_right_trigger", 5)
        self.declare_parameter("button_engage", 3)      

        self.declare_parameter("stick_up_is_positive", True)
        self.declare_parameter("stick_threshold", 0.5)

        # Test trigger values at rest and fully pressed. joy_node on Xbox pads usually reports
        # rest = 1.0, pressed = -1.0.
        self.declare_parameter("trigger_released_value", 1.0)
        self.declare_parameter("trigger_pressed_value", -1.0)
        self.declare_parameter("trigger_threshold", 0.25)  # Fraction of travel

        # ---- Read parameters ----
        p = self.get_parameter
        self.port = p("serial_port").value
        self.baud = p("baud").value
        self.rate_hz = p("rate_hz").value
        self.enable_frames_len = p("enable_frames").value
        self.joy_timeout = p("joy_timeout_s").value
        self.ax_ls = p("axis_left_stick_y").value
        self.ax_rs = p("axis_right_stick_y").value
        self.ax_lt = p("axis_left_trigger").value
        self.ax_rt = p("axis_right_trigger").value
        self.btn_engage = p("button_engage").value
        self.up_positive = p("stick_up_is_positive").value
        self.stick_thr = p("stick_threshold").value
        self.trig_released = p("trigger_released_value").value
        self.trig_pressed = p("trigger_pressed_value").value
        self.trig_thr = p("trigger_threshold").value

        # ---- State ----
        self.last_joy = None
        self.last_joy_time = 0.0
        self.y_was_pressed = False
        self.enable_frames_left = 0
        self.engaged = 0
        self.ser = None
        self.next_reconnect = 0.0

        self.open_serial()

        self.create_subscription(Joy, "joy", self.on_joy, 10)
        self.timer = self.create_timer(1.0 / self.rate_hz, self.tick)
        self.get_logger().info(f"Teleop ready: /joy -> {self.port} @ {self.baud}")

    # ---------------- Serial ----------------
    def open_serial(self):
        """Open the port with DTR/RTS low so opening it does not reset the ESP32."""
        try:
            ser = serial.Serial()
            ser.port = self.port
            ser.baudrate = self.baud
            ser.dtr = False
            ser.rts = False
            ser.open()
            self.ser = ser
            self.get_logger().info(f"Opened {self.port}")
        except serial.SerialException as exc:
            self.ser = None
            self.get_logger().error(f"Cannot open {self.port}: {exc}")
        self.next_reconnect = time.monotonic() + 1.0

    def write_frame(self, enable, forward, backward):
        if self.ser is None:
            if time.monotonic() >= self.next_reconnect:
                self.open_serial()
            return
        try:
            self.ser.write(build_frame(enable, forward, backward))
        except (serial.SerialException, OSError) as exc:
            self.get_logger().error(f"Serial write failed: {exc}", throttle_duration_sec=2.0)
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
            self.next_reconnect = time.monotonic() + 1.0

    # ---------------- Controller decoding ----------------
    def on_joy(self, msg):
        self.last_joy = msg
        self.last_joy_time = time.monotonic()

    def axis(self, msg, index):
        return msg.axes[index] if 0 <= index < len(msg.axes) else 0.0

    def button(self, msg, index):
        return bool(msg.buttons[index]) if 0 <= index < len(msg.buttons) else False

    def stick_direction(self, value):
        """Stick up is forward (+1), down is backward (-1), else off (0)."""
        if not self.up_positive:
            value = -value
        if value > self.stick_thr:
            return 1
        if value < -self.stick_thr:
            return -1
        return 0

    def trigger_amount(self, value):
        """Normalize a trigger reading to 0 (released) .. 1 (fully pressed)."""
        span = self.trig_pressed - self.trig_released
        return (value - self.trig_released) / span if span else 0.0

    def trigger_direction(self, right, left):
        """RT alone is forward (+1), LT alone is backward (-1), neither or both is off (0)."""
        forward = self.trigger_amount(right) > self.trig_thr
        backward = self.trigger_amount(left) > self.trig_thr
        if forward and not backward:
            return 1
        if backward and not forward:
            return -1
        return 0

    def read_directions(self, msg):
        return {
            RS04_BIT: self.stick_direction(self.axis(msg, self.ax_ls)),
            RS02_BIT: self.stick_direction(self.axis(msg, self.ax_rs)),
            DYNAMIXEL_BIT: self.trigger_direction(self.axis(msg, self.ax_rt),
                                                  self.axis(msg, self.ax_lt)),
        }

    @staticmethod
    def directions_to_bytes(directions):
        forward = 0
        backward = 0
        for bit, direction in directions.items():
            if direction > 0:
                forward |= bit
            elif direction < 0:
                backward |= bit
        return forward, backward

    # ---------------- Main loop ----------------
    def tick(self):
        joy_fresh = (self.last_joy is not None and
                     time.monotonic() - self.last_joy_time < self.joy_timeout)

        if joy_fresh:
            directions = self.read_directions(self.last_joy)

            # Y engages all actuators, only on the moment it is pressed
            y_pressed = self.button(self.last_joy, self.btn_engage)
            if y_pressed and not self.y_was_pressed:
                self.enable_frames_left = self.enable_frames_len
                self.engaged |= ALL_ACTUATORS
                self.get_logger().info("Engage pressed")
            self.y_was_pressed = y_pressed
        else:
            directions = {bit: 0 for bit in ACTUATOR_BITS}   # Controller lost, stop everything
            self.y_was_pressed = False
            if self.last_joy is not None:
                self.get_logger().warn("No /joy messages, actuators stopped",
                                       throttle_duration_sec=2.0)

        forward, backward = self.directions_to_bytes(directions)

        # The enable byte is a short pulse, the direction bits follow the controller
        enable = (ENABLE_FLAG | ALL_ACTUATORS) if self.enable_frames_left > 0 else 0
        if self.enable_frames_left > 0:
            self.enable_frames_left -= 1

        # Send every cycle, even if nothing changed, so the ESP32 watchdog stays fed
        self.write_frame(enable, forward, backward)

    # ---------------- Shutdown ----------------
    def send_stop(self):
        """Send a few stop frames so the motors ramp down, then close the port."""
        if self.ser is None:
            return
        try:
            for _ in range(10):
                self.ser.write(build_frame(0, 0, 0))
                time.sleep(1.0 / self.rate_hz)
            self.ser.close()
        except (serial.SerialException, OSError):
            pass


def main(args=None):
    rclpy.init(args=args)
    node = ActuatorTeleop()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.send_stop()
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
