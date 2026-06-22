# import serial
# import time
# import sys
# import struct
# from pynput import keyboard

# # --- CONFIGURATION ---
# RADIO_PORT = '/dev/ttyUSB0' 
# BAUD = 57600
# MASTER_ID = 0xFD
# MOTOR_04_ID = 7    # A/D Keys
# MOTOR_02_ID = 127  # W/S Keys

# # --- SMOOTHING TUNES ---
# MOVE_INCREMENT = 0.12  # Larger steps to bridge radio gaps
# KP_ARM = 25.0          # Stiff tracking
# KD_ARM = 2.2           # Damping for smooth stops

# # --- STATE ---
# targets = {MOTOR_04_ID: 0.0, MOTOR_02_ID: 0.0}
# active_keys = {'w': False, 's': False, 'a': False, 'd': False}
# is_enabled = False
# HEADER = bytes([0xAA, 0x55])

# try:
#     ser = serial.Serial(RADIO_PORT, BAUD, timeout=0.1)
#     print(f"✅ Radio Link Active on {RADIO_PORT}")
# except Exception as e:
#     print(f"❌ Radio Error: {e}"); sys.exit(1)

# def float_to_uint16(v, v_min, v_max):
#     v = max(v_min, min(v_max, v))
#     return int((v - v_min) * 65535 / (v_max - v_min))

# def send_radio_frame(mode_byte, motor_id, data_bytes):
#     """[Header 2b][Mode 1b][ID 1b][Data 8b][Checksum 1b]"""
#     frame = HEADER + bytes([mode_byte, motor_id]) + data_bytes
#     chk = mode_byte ^ motor_id
#     for b in data_bytes: chk ^= b
#     packet = frame + bytes([chk])
#     ser.write(packet)
#     return packet

# def on_press(key):
#     global is_enabled
#     try:
#         if key.char in active_keys: active_keys[key.char] = True
#         if key.char == 'e':
#             print("\n[!] Enabling Motors...")
#             for m_id in [MOTOR_04_ID, MOTOR_02_ID]:
#                 send_radio_frame(0x04, m_id, bytes([0]*8)) # Clear
#                 time.sleep(0.05)
#                 send_radio_frame(0x04, m_id, bytes([1] + [0]*7)) # Enable
#                 time.sleep(0.05)
#                 send_radio_frame(0x03, m_id, bytes([0]*8)) # Op Mode
#             is_enabled = True
#             print("✅ Ready.")
#     except: pass

# def on_release(key):
#     try:
#         if key.char in active_keys: active_keys[key.char] = False
#         if key.char == 'x': return False
#     except: pass

# listener = keyboard.Listener(on_press=on_press, on_release=on_release)
# listener.start()

# print(f"CONTROLLER: A/D -> ID {MOTOR_04_ID} | W/S -> ID {MOTOR_02_ID}")
# print("[E] Enable | [X] Exit")

# try:
#     while listener.running:
#         if is_enabled:
#             # Update targets for ID 127 (02)
#             if active_keys['w']: targets[MOTOR_02_ID] += MOVE_INCREMENT
#             if active_keys['s']: targets[MOTOR_02_ID] -= MOVE_INCREMENT
            
#             # Update targets for ID 7 (04)
#             if active_keys['a']: targets[MOTOR_04_ID] -= MOVE_INCREMENT
#             if active_keys['d']: targets[MOTOR_04_ID] += MOVE_INCREMENT

#             for m_id in [MOTOR_04_ID, MOTOR_02_ID]:
#                 targets[m_id] = max(-12.5, min(12.5, targets[m_id]))
                
#                 # Build Robstride Motion Packet
#                 p_int = float_to_uint16(targets[m_id], -12.5, 12.5)
#                 can_data = struct.pack('>HHHH', p_int, 0, 
#                                    float_to_uint16(KP_ARM, 0, 500), 
#                                    float_to_uint16(KD_ARM, 0, 5))
                
#                 send_radio_frame(0x01, m_id, can_data)

#             sys.stdout.write(f"\rTargets -> 04(ID7): {targets[7]:.2f} | 02(ID127): {targets[127]:.2f}   ")
#             sys.stdout.flush()

#         time.sleep(0.02) 

# except KeyboardInterrupt: pass
# finally:
#     ser.close()


# import serial
# import time
# import sys
# import struct
# from pynput import keyboard

# # --- CONFIGURATION ---
# RADIO_PORT = '/dev/tty.usbserial-B004A1RV' 
# BAUD = 57600
# HEADER = bytes([0xAA, 0x55])
# active_keys = {'w': False, 's': False, 'a': False, 'd': False, 'q': False, 'e': False}
# ROVER_ID = 255 
# MOTOR_04_ID = 7    # A/D Keys
# MOTOR_02_ID = 127
# targets = {MOTOR_04_ID: 0.0, MOTOR_02_ID: 0.0}


# # --- MODES ---
# MODE_ARM = 0
# MODE_ROVER = 1
# current_control_mode = MODE_ARM

# try:
#     ser = serial.Serial(RADIO_PORT, BAUD, timeout=0.1)
#     print(f"✅ Radio Link Active on {RADIO_PORT}")
# except Exception as e:
#     print(f"❌ Radio Error: {e}"); sys.exit(1)

# def send_radio_frame(mode_byte, motor_id, data_bytes):
#     """Encapsulates data into the radio protocol"""
#     # Pad data to 8 bytes to maintain fixed frame length for the radio
#     data_bytes = data_bytes.ljust(8, b'\x00')
#     frame = HEADER + bytes([mode_byte, motor_id]) + data_bytes[:8]
#     chk = mode_byte ^ motor_id
#     for b in data_bytes[:8]: chk ^= b
#     ser.write(frame + bytes([chk]))

# def on_press(key):
#     global current_control_mode
#     try:
#         if key.char == 'm':
#             current_control_mode = MODE_ROVER if current_control_mode == MODE_ARM else MODE_ARM
#             mode_name = "ROVER (Terminal Pass-through)" if current_control_mode == MODE_ROVER else "ARM (Joints)"
#             print(f"\n🔄 Mode: {mode_name}")
#     except: pass

# listener = keyboard.Listener(on_press=on_press)
# listener.start()

# print("--- CONTROLLER ACTIVE ---")
# print("[M] Toggle Mode | [X] Exit")

# try:
#     while True:
#         if current_control_mode == MODE_ROVER:
#             # Direct terminal input passed to Radio
#             cmd = input("ROVER CMD (e.g. 1,30000): ")
#             if cmd:
#                 # Mode 0x05 signals this is a string command for the Jetson's USB0
#                 send_radio_frame(0x05, ROVER_ID, cmd.encode())
#                 print(f"📡 Sent to Radio: {cmd}")
#         else:
#             if active_keys['w']: targets[MOTOR_02_ID] += 0.12
#             if active_keys['s']: targets[MOTOR_02_ID] -= 0.12
#             if active_keys['a']: targets[MOTOR_04_ID] -= 0.12
#             if active_keys['d']: targets[MOTOR_04_ID] += 0.12
                
#             for m_id in [MOTOR_04_ID, MOTOR_02_ID]:
#                 targets[m_id] = max(-12.5, min(12.5, targets[m_id]))
#                 p_int = int((targets[m_id] + 12.5) * 65535 / 25.0)
#                 can_data = struct.pack('>HHHH', p_int, 0, int(25.0 * 131), int(2.2 * 13107))
#                 send_radio_frame(0x01, m_id, can_data)
                
#             status = f"ARM -> 04: {targets[7]:.2f} | 02: {targets[127]:.2f}"

#         sys.stdout.write(f"\r{status}      ")
#         sys.stdout.flush()
#     time.sleep(0.1)

# except KeyboardInterrupt: pass
# finally: ser.close()


import serial
import time
import sys
import struct
from pynput import keyboard

# --- CONFIGURATION ---
RADIO_PORT = '/dev/tty.usbserial-B004A1RV' 
BAUD = 57600
HEADER = bytes([0xAA, 0x55])

# Motor IDs
MOTOR_04_ID = 7    # A/D Keys
MOTOR_02_ID = 127  # W/S Keys
ROVER_ID = 255 
STEPPER_ID = 0x01 # Arbitrary ID for stepper commands

# MIT Mode Constants (From Manual)
P_MIN, P_MAX = -12.5, 12.5
V_MIN, V_MAX = -45.0, 45.0
KP_MIN, KP_MAX = 0.0, 500.0
KD_MIN, KD_MAX = 0.0, 5.0

# Movement Tuning
MOVE_INCREMENT = 0.15
KP_VAL = 30.0   # Adjust for stiffness
KD_VAL = 1.5    # Adjust for damping

# Stepper Tuning
STEPPER_SPEED = 500 # Steps per second (or delay value if ESP32 handles timing)
STEP_INCREMENT = 1 # Steps to send per key press

stepper_running = False
current_stepper_dir = 'CW'

# --- STATE ---
current_control_mode = 0  # 0: ARM, 1: ROVER
targets = {MOTOR_04_ID: 0.0, MOTOR_02_ID: 0.0}
active_keys = {'w': False, 's': False, 'a': False, 'd': False, 'c': False, 'v': False, 'z': False}
is_enabled = False

try:
    ser = serial.Serial(RADIO_PORT, BAUD, timeout=0.1)
    print(f"✅ Radio Link Active")
except Exception as e:
    print(f"❌ Radio Error: {e}"); sys.exit(1)

# --- PACKING UTILS ---
def float_to_uint(x, x_min, x_max, bits):
    span = x_max - x_min
    offset = x_min
    return int((x - offset) * ((1 << bits) - 1) / span)

def send_radio_frame(mode_byte, motor_id, data_bytes):
    """Jetson-ready frame: [AA 55] [Mode] [ID] [Data x8] [CHK]"""
    data_bytes = data_bytes.ljust(8, b'\x00')[:8]
    frame_head = HEADER + bytes([mode_byte, motor_id]) + data_bytes
    chk = mode_byte ^ motor_id
    for b in data_bytes: chk ^= b
    ser.write(frame_head + bytes([chk]))

def send_stepper_command(direction, steps):
    """
    Constructs and sends a single stepper frame.
    """
    dir_byte = 1 if direction == 'CW' else 0
    data = struct.pack('>BHHxxx', dir_byte, steps, STEPPER_SPEED)
    send_radio_frame(0x06, STEPPER_ID, data)

def on_press(key):
    global current_control_mode, is_enabled, stepper_running, current_stepper_dir
    try:
        if hasattr(key, 'char') and key.char is not None:
                k = key.char
                if k == 'm':
                    current_control_mode = 1 - current_control_mode
                    print(f"\n🔄 Mode: {'ROVER' if current_control_mode == 1 else 'ARM'}")
                
                if current_control_mode == 0:
                    if k == 'e':
                        print("\n[!] Sending Enable Sequence...")
                        for m_id in [MOTOR_04_ID, MOTOR_02_ID]:
                            send_radio_frame(0x03, m_id, bytes([1] + [0]*7))
                            time.sleep(0.05)
                        print("✅ Motors Online")
                        is_enabled = True
                    elif k == 'c':
                        current_stepper_dir = 'CCW'
                        stepper_running = True
                    elif k == 'v':
                        current_stepper_dir = 'CW'
                        stepper_running = True
                    elif k == 'z':
                        stepper_running = False
                        print("\n🛑 Stepper Stopped")
                
                if k in active_keys: active_keys[k] = True

        # 2. Handle Special Keys (Arrows for Servos)
        elif current_control_mode == 0:
            # Mode 0x07 = Servo Control, ID = Motor ID for the servo
            SERVO1_ID = 10 
            SERVO2_ID = 11
                
            if key == keyboard.Key.up:
                print("⬆️ Servo Up")
                send_radio_frame(0x07, SERVO1_ID, struct.pack('>bxxxxxxx', 5)) 
            elif key == keyboard.Key.down:
                print("⬇️ Servo Down")
                send_radio_frame(0x07, SERVO1_ID, struct.pack('>bxxxxxxx', -5))
            elif key == keyboard.Key.left:
                print("⬅️ Servo Left")
                send_radio_frame(0x07, SERVO2_ID, struct.pack('>bxxxxxxx', 5))
            elif key == keyboard.Key.right:
                print("➡️ Servo Right")
                send_radio_frame(0x07, SERVO2_ID, struct.pack('>bxxxxxxx', -5))
    except: pass

def on_release(key):
    try:
        if key.char in active_keys: active_keys[key.char] = False
        if key.char == 'x': return False
    except: pass

listener = keyboard.Listener(on_press=on_press, on_release=on_release)
listener.start()

print("--- ARM/ROVER CONTROLLER ---")
print("[M] Toggle Mode | [E] Enable | [C/V] Stepper | [X] Exit, [Z] Stop Stepper")
print("Arrow Keys: Move End Effector (ARM Mode)")

try:
    while listener.running:
        if current_control_mode == 0: # ARM MODE
            if stepper_running:
                # We send small bursts of steps continuously while the state is active
                send_stepper_command(current_stepper_dir, 25)
            if is_enabled:
                # --- Servo Control (WASD) ---
                if active_keys['w']: targets[MOTOR_02_ID] += MOVE_INCREMENT
                if active_keys['s']: targets[MOTOR_02_ID] -= MOVE_INCREMENT
                if active_keys['a']: targets[MOTOR_04_ID] -= MOVE_INCREMENT
                if active_keys['d']: targets[MOTOR_04_ID] += MOVE_INCREMENT

                for m_id in [MOTOR_04_ID, MOTOR_02_ID]:
                    targets[m_id] = max(P_MIN, min(P_MAX, targets[m_id]))
                    
                    p = float_to_uint(targets[m_id], P_MIN, P_MAX, 16)
                    v = float_to_uint(0, V_MIN, V_MAX, 16)
                    kp = float_to_uint(KP_VAL, KP_MIN, KP_MAX, 16)
                    kd = float_to_uint(KD_VAL, KD_MIN, KD_MAX, 16)
                    
                    can_data = struct.pack('>HHHH', p, v, kp, kd)
                    send_radio_frame(0x01, m_id, can_data)

                sys.stdout.write(f"\rTargets -> 04: {targets[7]:.2f} | 02: {targets[127]:.2f}  ")
                sys.stdout.flush()
            time.sleep(0.02)
        else: # ROVER MODE
            cmd = input("\rROVER CMD: ")
            if cmd: send_radio_frame(0x05, ROVER_ID, cmd.encode())

except KeyboardInterrupt: pass
finally: ser.close()

    