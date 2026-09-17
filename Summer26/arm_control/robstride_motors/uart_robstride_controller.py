# WARNING: IMPLEMENTATION IN PROGRESS

import serial
import struct
import sys
import termios
import tty
import select
import time

MOTOR_IDS = {'1': 127, '2': 7, '3': 6}
target = MOTOR_IDS['1']
torque = 0.0
position = 0.0

HOLD_TIMEOUT = 0.2  # seconds - treat key as "still held" if repeats arrive faster than this

def read_key():
    """Non-blocking: returns a single character if one is waiting, else None."""
    dr, _, _ = select.select([sys.stdin], [], [], 0)
    if dr:
        return sys.stdin.read(1)
    return None

def main():
    global target
    port = serial.Serial(
        port='/dev/ttyAMA0',
        baudrate=115200,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=None,
    )

    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    velocity = 0.0
    last_w = last_s = 0.0

    try:
        tty.setcbreak(fd)  # keys register immediately, no Enter needed
        print("w/s = forward/reverse (hold), 1/2/3 = select motor, q = quit")
        while True:
            key = read_key()
            now = time.monotonic()

            if key == 'w':
                last_w = now
            elif key == 's':
                last_s = now
            elif key in MOTOR_IDS:
                target = MOTOR_IDS[key]
            elif key == 'q':
                break

            # Terminal key-repeat acts as our "still held" heartbeat
            if now - last_w < HOLD_TIMEOUT:
                velocity = 1.0
            elif now - last_s < HOLD_TIMEOUT:
                velocity = -1.0
            else:
                velocity = 0.0

            port.write(struct.pack('<Ifff', target, torque, position, velocity))
            time.sleep(0.01)  # 100Hz
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)

if __name__ == "__main__":
    main()
