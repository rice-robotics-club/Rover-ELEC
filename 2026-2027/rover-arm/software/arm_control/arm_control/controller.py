#!/usr/bin/env python3
"""Gamepad teleop for the ESP32 actuator controller (plain Python, no ROS).


Run "python3 gamepad_teleop.py probe" to print the live axis and button numbers
if your controller does not match the layout constants below.
"""
import rclpy
from rclpy.node import Node

from sensor_msgs.msg import Joy

import os
import struct
import sys
import time

# Read the Controller Without a Window, and Keep Reading it When Another Window Has Focus
os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1")
os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")

import pygame

# Controller Layout, SDL Numbering for an Xbox-Style Pad on Linux (Check With the probe Mode)
# Axes
AXIS_LEFT_STICK_X = 0
AXIS_LEFT_STICK_Y = 1

AXIS_LEFT_TRIGGER = 2

AXIS_RIGHT_STICK_X = 3
AXIS_RIGHT_STICK_Y = 4

AXIS_RIGHT_TRIGGER = 5


# Buttons
BUTTON_A = 0
BUTTON_B = 1
BUTTON_X = 2
BUTTON_Y = 3

BUTTON_LEFT_BUMPER = 4
BUTTON_RIGHT_BUMPER = 5

BUTTON_BACK = 6
BUTTON_START = 7

BUTTON_XBOX = 8

BUTTON_LEFT_STICK = 9
BUTTON_RIGHT_STICK = 10




STICK_THRESHOLD = 0.5         # Stick Must Pass 50% of its Travel, Same as round() in the ROS Script
TRIGGER_THRESHOLD = 0.25      # Works for Triggers Reading -1..1 (Rest at -1) or 0..1 (Rest at 0)



class ControllerStatePublisher(Node):

    axes = [0.0] * 6
    
    buttons = [0] * 11
    
    def __init__(self):
        super().__init__('controller_state_publisher')
        self.joy_pub = self.create_publisher(
            Joy,
            '/controller_state',
            10
        )
        self.timer = self.create_timer(0.01, self.publish_controller_state)

        



    def publish_controller_state(self):
        msg = Joy()

        # Timestamp
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.axes = self.axes
        msg.buttons = self.buttons
        self.joy_pub.publish(msg)











def find_controller():
    """Prefer the 8BitDo pad by name, otherwise use the first controller found."""
    controllers = [pygame.joystick.Joystick(n) for n in range(pygame.joystick.get_count())]
    for controller in controllers:
        if "8bitdo" in controller.get_name().lower():
            return controller
    return controllers[0] if controllers else None


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

    rclpy.init(args=None) 

    node = ControllerStatePublisher()

    rclpy.spin_once(node)

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


    last_lines = None
    drawn = False

    try:
        while True:
            # Handle the Controller Being Unplugged or Plugged Back In (Also Updates the Controller State)
            for event in pygame.event.get():
                if event.type == pygame.JOYDEVICEREMOVED and controller is not None \
                        and event.instance_id == controller.get_instance_id():
                    controller = None
                elif event.type == pygame.JOYDEVICEADDED and controller is None:
                    controller = find_controller()

            if controller is not None:
                if controller is not None:

                    # Analog axes
                    node.axes[0] = controller.get_axis(AXIS_LEFT_STICK_X)
                    node.axes[1] = controller.get_axis(AXIS_LEFT_STICK_Y)
                    node.axes[2] = controller.get_axis(AXIS_RIGHT_STICK_X)
                    node.axes[3] = controller.get_axis(AXIS_RIGHT_STICK_Y)
                    node.axes[4] = controller.get_axis(AXIS_LEFT_TRIGGER)
                    node.axes[5] = controller.get_axis(AXIS_RIGHT_TRIGGER)

                    # Buttons
                    node.buttons[0] = controller.get_button(BUTTON_A)
                    node.buttons[1] = controller.get_button(BUTTON_B)
                    node.buttons[2] = controller.get_button(BUTTON_X)
                    node.buttons[3] = controller.get_button(BUTTON_Y)
                    node.buttons[4] = controller.get_button(BUTTON_LEFT_BUMPER)
                    node.buttons[5] = controller.get_button(BUTTON_RIGHT_BUMPER)
                    node.buttons[6] = controller.get_button(BUTTON_BACK)
                    node.buttons[7] = controller.get_button(BUTTON_START)
                    node.buttons[8] = controller.get_button(BUTTON_XBOX)
                    node.buttons[9] = controller.get_button(BUTTON_LEFT_STICK)
                    node.buttons[10] = controller.get_button(BUTTON_RIGHT_STICK)


            else:
                directions = {bit: 0 for bit, _ in ACTUATOR_NAMES}    # Controller Lost, Stop Everything
                y_was_pressed = False

            print(
                f"\rAxes: { [round(x, 2) for x in node.axes] }"
                f" | Buttons: {node.buttons}",
                end="",
                flush=True
                )
        
            rclpy.spin_once (node)


    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()
        print()
        pygame.quit()


if __name__ == "__main__":
    main()