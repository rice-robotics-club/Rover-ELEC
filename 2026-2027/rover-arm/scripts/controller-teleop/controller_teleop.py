#!/usr/bin/env python3
"""Gamepad teleop for the ESP32 actuator controller (plain Python, no ROS).

The sticks and triggers are analog, but each actuator only gets three states:
forward, backward, or off. The same 6-byte frame as teleop.py is sent over USB serial:
    0xAA 0x55 | enable | forward | backward | checksum (XOR of the 3 command bytes)

Bit layout (matches the order of ACTUATORS[] in the firmware):
    bit 0 -> Robstride 04  (ID 7)  left stick,  up = forward, down = backward
    bit 1 -> Robstride 02          right stick, up = forward, down = backward
    bit 2 -> Dynamixel             RT = forward, LT = backward
    bit 7 of the enable byte -> enable flag

Y button engages all actuators. Ctrl+C quits.

Run "python3 gamepad_teleop.py probe" to print the live axis and button numbers
if your controller does not match the layout constants below.
"""

import os
import struct
import sys
import time

# Read the Controller Without a Window, and Keep Reading it When Another Window Has Focus
os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1")
os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")

import pygame
import serial

PORT = "/dev/ttyUSB0"
BAUD = 115200
RATE_HZ = 100                 # Frames per Second, Must Be Well Above 1 / HOST_TIMEOUT_MS (5 Hz)
PERIOD = 1.0 / RATE_HZ

SYNC1 = 0xAA
SYNC2 = 0x55

ENABLE_FLAG = 0x80            # MSB of the Enable Byte
ENABLE_FRAMES = 5             # Frames the Enable Byte is Held, so the ESP32 Sees it Change Once

# Actuator Bits, Same Order as ACTUATORS[] in the Firmware
RS04_BIT = 0x01
RS02_BIT = 0x02
DYNAMIXEL_BIT = 0x04
ALL_ACTUATORS = RS04_BIT | RS02_BIT | DYNAMIXEL_BIT

# Controller Layout, SDL Numbering for an Xbox-Style Pad on Linux (Check With the probe Mode)
AXIS_LEFT_STICK_Y = 1
AXIS_RIGHT_STICK_Y = 4
AXIS_LEFT_TRIGGER = 2
AXIS_RIGHT_TRIGGER = 5
BUTTON_Y = 3

STICK_THRESHOLD = 0.5         # Stick Must Pass 50% of its Travel, Same as round() in the ROS Script
TRIGGER_THRESHOLD = 0.25      # Works for Triggers Reading -1..1 (Rest at -1) or 0..1 (Rest at 0)

# Display Order of the Actuators
ACTUATOR_NAMES = [(RS04_BIT, "RS-04"), (RS02_BIT, "RS-02"), (DYNAMIXEL_BIT, "Dynamixel")]


def checksum(enable, forward, backward):
    """XOR of the three command bytes."""
    return enable ^ forward ^ backward


def build_frame(enable, forward, backward):
    """Pack sync bytes, the three command bytes, and the checksum into 6 bytes."""
    return struct.pack("<6B", SYNC1, SYNC2, enable, forward, backward,
                       checksum(enable, forward, backward))


def stick_direction(value):
    """Stick pushed up (negative axis) is forward (+1), pushed down is backward (-1), else off (0)."""
    if value < -STICK_THRESHOLD:
        return 1
    if value > STICK_THRESHOLD:
        return -1
    return 0


def trigger_direction(right, left):
    """RT alone is forward (+1), LT alone is backward (-1), neither or both is off (0)."""
    forward = right > TRIGGER_THRESHOLD
    backward = left > TRIGGER_THRESHOLD
    if forward and not backward:
        return 1
    if backward and not forward:
        return -1
    return 0


def read_directions(controller):
    """Direction (+1, 0, -1) of each actuator, read from the controller."""
    return {
        RS04_BIT: stick_direction(controller.get_axis(AXIS_LEFT_STICK_Y)),
        RS02_BIT: stick_direction(controller.get_axis(AXIS_RIGHT_STICK_Y)),
        DYNAMIXEL_BIT: trigger_direction(controller.get_axis(AXIS_RIGHT_TRIGGER),
                                         controller.get_axis(AXIS_LEFT_TRIGGER)),
    }


def directions_to_bytes(directions):
    """Turn the per-actuator directions into the forward and backward bytes."""
    forward = 0
    backward = 0
    for bit, direction in directions.items():
        if direction > 0:
            forward |= bit
        elif direction < 0:
            backward |= bit
    return forward, backward


def find_controller():
    """Prefer the 8BitDo pad by name, otherwise use the first controller found."""
    controllers = [pygame.joystick.Joystick(n) for n in range(pygame.joystick.get_count())]
    for controller in controllers:
        if "8bitdo" in controller.get_name().lower():
            return controller
    return controllers[0] if controllers else None


def status_lines(enable, forward, backward, engaged, connected):
    """Two lines: the transmitted bits, and what each actuator is doing (each under 80 columns)."""
    bits = f"en={enable:08b} fwd={forward:08b} bwd={backward:08b}"
    if not connected:
        return [bits, "CONTROLLER DISCONNECTED - all actuators stopped"]

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
    return [bits, "  |  ".join(actuators)]


def redraw(lines, drawn):
    """Rewrite the status block in place, moving back up over it if it is already on screen."""
    out = ""
    if drawn:
        out += f"\x1b[{len(lines) - 1}A"
    out += "\n".join("\r\x1b[K" + line for line in lines)
    sys.stdout.write(out)
    sys.stdout.flush()


def probe():
    """Print live axis and button numbers so the layout constants can be checked."""
    pygame.display.init()         # Needed for the Event Queue, Skips Initializing Audio
    pygame.joystick.init()
    controller = find_controller()
    if controller is None:
        sys.exit("No controller detected")
    print(f"Connected: {controller.get_name()}")
    print("Move each stick and trigger and press Y to see which numbers change. Ctrl+C quits.")
    try:
        while True:
            pygame.event.pump()
            axes = " ".join(f"{n}:{controller.get_axis(n):+.2f}" for n in range(controller.get_numaxes()))
            buttons = [n for n in range(controller.get_numbuttons()) if controller.get_button(n)]
            sys.stdout.write(f"\r\x1b[Kaxes {axes} | buttons {buttons}")
            sys.stdout.flush()
            time.sleep(0.05)
    except KeyboardInterrupt:
        print()


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "probe":
        probe()
        return

    pygame.display.init()         # Needed for the Event Queue, Skips Initializing Audio
    pygame.joystick.init()

    controller = find_controller()
    if controller is None:
        sys.exit("No controller detected")
    print(f"Connected: {controller.get_name()}")
    print(__doc__)

    # Set DTR/RTS Low Before Opening so Opening the Port Does Not Reset the ESP32
    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = BAUD
    ser.dtr = False
    ser.rts = False
    ser.open()

    enable_frames = 0             # Frames Left in the Current Enable Pulse
    engaged = 0                   # Records That an Enable Was Sent This Session, the Firmware Does Not Report Back
    y_was_pressed = False
    last_lines = None
    drawn = False

    try:
        next_tick = time.monotonic()
        while True:
            # Handle the Controller Being Unplugged or Plugged Back In (Also Updates the Controller State)
            for event in pygame.event.get():
                if event.type == pygame.JOYDEVICEREMOVED and controller is not None \
                        and event.instance_id == controller.get_instance_id():
                    controller = None
                elif event.type == pygame.JOYDEVICEADDED and controller is None:
                    controller = find_controller()

            if controller is not None:
                directions = read_directions(controller)

                # Y Engages All Actuators, Only on the Moment it is Pressed
                y_pressed = bool(controller.get_button(BUTTON_Y))
                if y_pressed and not y_was_pressed:
                    enable_frames = ENABLE_FRAMES
                    engaged |= ALL_ACTUATORS
                y_was_pressed = y_pressed
            else:
                directions = {bit: 0 for bit, _ in ACTUATOR_NAMES}    # Controller Lost, Stop Everything
                y_was_pressed = False

            forward, backward = directions_to_bytes(directions)

            # The Enable Byte is a Short Pulse, the Direction Bits Follow the Controller
            enable = (ENABLE_FLAG | ALL_ACTUATORS) if enable_frames > 0 else 0
            if enable_frames > 0:
                enable_frames -= 1

            # Send Every Cycle, Even if Nothing Changed, so the ESP32 Watchdog Stays Fed
            ser.write(build_frame(enable, forward, backward))

            # Redraw the Status Block in Place Only When it Changes
            lines = status_lines(enable, forward, backward, engaged, controller is not None)
            if lines != last_lines:
                redraw(lines, drawn)
                drawn = True
                last_lines = lines

            # Sleep Until the Next Frame is Due
            next_tick += PERIOD
            delay = next_tick - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            else:
                next_tick = time.monotonic()      # Fell Behind, Do Not Burst to Catch Up
    except KeyboardInterrupt:
        pass
    finally:
        print()
        # Send a Few Stop Frames so the Motors Ramp Down, Then Close
        try:
            for _ in range(10):
                ser.write(build_frame(0, 0, 0))
                time.sleep(PERIOD)
            ser.close()
        except serial.SerialException:
            pass
        pygame.quit()


if __name__ == "__main__":
    main()
