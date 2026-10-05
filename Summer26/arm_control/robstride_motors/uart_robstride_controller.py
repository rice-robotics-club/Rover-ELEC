# WARNING: IMPLEMENTATION IN PROGRESS

import serial
import struct
import sys
import termios
import tty
import select
import time

MOTOR_IDS = {'1': 6, '2': 7, '3': 127}
active = 6  # default motor: ID 6

def read_key():
    dr, _, _ = select.select([sys.stdin], [], [], 0)
    return sys.stdin.read(1) if dr else None

def main():
    global active
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

    try:
        tty.setcbreak(fd)
        print("1/2/3 = select motor (6/7/127), w/s = forward/reverse, space = stop, q = quit\r")
        while True:
            key = read_key()

            if key in MOTOR_IDS:
                active = MOTOR_IDS[key]
            elif key == 'w':
                velocity = 1.0
            elif key == 's':
                velocity = -1.0
            elif key == ' ':
                velocity = 0.0
            elif key == 'q':
                break

            if key is not None:
                line = f"motor: {active}  velocity: {velocity}"
                sys.stdout.write('\r' + line.ljust(40))
                sys.stdout.flush()

            port.write(struct.pack('<Ifff', active, 0.0, 0.0, velocity))
            time.sleep(0.01)  # 100Hz
    finally:
        print()  # move off the overwritten line before restoring the terminal
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)

if __name__ == "__main__":
    main()
