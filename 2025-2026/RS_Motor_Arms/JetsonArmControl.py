import can
import time
import struct
import sys
import tty
import termios
import threading

# --- SETTINGS ---
CHANNEL = '/dev/ttyACM0'
MOTOR_IDS = [7, 127]
MASTER_ID = 0xFD
LOOP_INTERVAL = 0.02 # 50Hz heartbeat

# GENTLE ARM TUNING
RAMP_SPEED = 0.02  
KP_ARM = 20.0      # Stiffness for the arm
KD_ARM = 1.8       # Damping to stop jitter

# --- STATE ---
targets = {7: 0.0, 127: 0.0}
current_cmds = {7: 0.0, 127: 0.0}
is_powered = {7: False, 127: False}
running = True

try:
    bus = can.interface.Bus(interface='slcan', channel=CHANNEL, bitrate=1000000)
except Exception as e:
    print(f"CAN Error: {e}"); sys.exit(1)

def float_to_uint16(v, v_min, v_max):
    v = max(v_min, min(v_max, v))
    return int((v - v_min) * 65535 / (v_max - v_min))

def uint16_to_float(v, v_min, v_max):
    return v * (v_max - v_min) / 65535 + v_min

def get_actual_pos(motor_id):
    """Reads the 16-bit encoder position from the motor."""
    while bus.recv(0.0001): pass 
    bus.send(can.Message(arbitration_id=(0x09 << 24) | (MASTER_ID << 8) | motor_id, data=[0]*8, is_extended_id=True))
    msg = bus.recv(0.1)
    if msg and len(msg.data) >= 2:
        return uint16_to_float((msg.data[0] << 8) | msg.data[1], -12.5, 12.5)
    return None

def can_heartbeat_thread():
    """Background thread: Ramps and sends position packets."""
    while running:
        for m_id in MOTOR_IDS:
            if not is_powered[m_id]: continue
            
            # Linear Ramp
            diff = targets[m_id] - current_cmds[m_id]
            if abs(diff) > RAMP_SPEED:
                current_cmds[m_id] += RAMP_SPEED if diff > 0 else -RAMP_SPEED
            else:
                current_cmds[m_id] = targets[m_id]

            # Pack Motion Command (Type 0x01)
            p_int = float_to_uint16(current_cmds[m_id], -12.5, 12.5)
            data = struct.pack('>HHHH', p_int, 0, 
                               float_to_uint16(KP_ARM, 0, 500), 
                               float_to_uint16(KD_ARM, 0, 5))
            bus.send(can.Message(arbitration_id=(0x01 << 24) | (MASTER_ID << 8) | m_id, data=data, is_extended_id=True))
        time.sleep(LOOP_INTERVAL)

def get_char():
    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setraw(sys.stdin.fileno())
        ch = sys.stdin.read(1)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    return ch

threading.Thread(target=can_heartbeat_thread, daemon=True).start()

print("""
ROBOTIC ARM CONTROLLER
---------------------------------------
[E] Enable/Align (Wait for spin to stop)
[SPACE] Toggle LIMP/HOLD (Safe stop)
[W/S/A/D] Move Arm
[Q] Return to Zero
[R] Print Current Encoder Positions
[X] Hard Exit
---------------------------------------
""")

try:
    while True:
        char = get_char().lower()
        
        if char == 'e':
            for m in MOTOR_IDS:
                # Clear and Enable
                bus.send(can.Message(arbitration_id=(0x04<<24)|(MASTER_ID<<8)|m, data=[1]+[0]*7, is_extended_id=True))
                time.sleep(0.05)
                bus.send(can.Message(arbitration_id=(0x03<<24)|(MASTER_ID<<8)|m, data=[0]*8, is_extended_id=True))
                print(f"Aligning Motor {m}...")
                time.sleep(1.5) # Wait for hardware alignment
                
                # Sync software to landing spot
                pos = get_actual_pos(m)
                if pos is not None:
                    targets[m] = pos
                    current_cmds[m] = pos
                    is_powered[m] = True
            print("Arm Ready.")

        elif char == ' ':
            for m in MOTOR_IDS:
                if is_powered[m]:
                    is_powered[m] = False
                    # Force Limp (Stop Command)
                    bus.send(can.Message(arbitration_id=(0x04<<24)|(MASTER_ID<<8)|m, data=[0]*8, is_extended_id=True))
                    print(f"\rMotor {m} LIMP (Safe)          ", end="")
                else:
                    # Re-enable and stay at current spot
                    pos = get_actual_pos(m)
                    if pos is not None:
                        targets[m] = pos
                        current_cmds[m] = pos
                    bus.send(can.Message(arbitration_id=(0x03<<24)|(MASTER_ID<<8)|m, data=[0]*8, is_extended_id=True))
                    is_powered[m] = True
                    print(f"\rMotor {m} HOLDING           ", end="")

        elif char == 'r':
            p7 = get_actual_pos(7)
            p127 = get_actual_pos(127)
            print(f"\nENCODER DATA -> ID7: {p7:.4f} | ID127: {p127:.4f}")

        elif char == 'w': targets[7] += 0.4; targets[127] += 0.4
        elif char == 's': targets[7] -= 0.4; targets[127] -= 0.4
        elif char == 'q': targets[7] = 0.0; targets[127] = 0.0
        elif char == 'x': break

finally:
    running = False
    for m in MOTOR_IDS: bus.send(can.Message(arbitration_id=(0x04<<24)|(MASTER_ID<<8)|m, data=[0]*8, is_extended_id=True))
    bus.shutdown()