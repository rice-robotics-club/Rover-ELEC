# WARNING: IMPLEMENTATION IN PROGRESS

import serial
from serial import EIGHTBITS, PARITY_NONE, STOPBITS_ONE
import struct

import threading
import keyboard
import time

torque = 0.0
position = 0.0
velocity = 0.0

MOTOR_IDS = {'1': 127, '2': 7, '3': 6}
target_lock = threading.Lock()

def read_target_id():
    global target
    while True:
        target_choice = input()
        if target_choice in MOTOR_IDS:
            with target_lock:
                target = MOTOR_IDS[target_choice]
        else:
            print("Invalid ID") 

def main():
    # Define and Configure UART Port
    port = serial.Serial(
        port='/dev/ttyAMA0', 
        baudrate=115200, 
        bytesize=EIGHTBITS, 
        parity=PARITY_NONE, 
        stopbits=STOPBITS_ONE, 
        timeout=None, 
        xonxoff=False, 
        rtscts=False, 
        write_timeout=None, 
        dsrdtr=False, 
        inter_byte_timeout=None,
        exclusive=None
    )

    # Execute Parallel Threads to Read Keyboard Inputs
    thread = threading.Thread(target=read_target_id, daemon=True)
    thread.start()

    while(1):
        if keyboard.is_pressed('w'):
            velocity = 1.0
        elif keyboard.is_pressed('s'):
            velocity = -1.0
        else:
            velocity = 0.0

        with target_lock:
            current_target = target

        port.write(struct.pack('<Ifff', current_target, torque, position, velocity))
        time.sleep(0.01)  # 100Hz

if __name__ == "__main__":
    main()
