import can
import time
import struct
import sys
import select
import tty
import termios
import threading

# --- SETTINGS ---
CHANNEL = '/dev/ttyACM0'
MOTOR_04_ID = 7   
MOTOR_02_ID = 127 
MASTER_ID = 0xFD
LOOP_INTERVAL = 0.02 

# TUNING
RAMP_SPEED = 0.05      
MOVE_INCREMENT = 0.04   
KP_ARM = 20.0      
KD_ARM = 1.8       

# --- STATE ---
targets = {7: 0.0, 127: 0.0}
current_cmds = {7: 0.0, 127: 0.0}
is_powered = {7: False, 127: False}
last_seen = {'w': 0, 's': 0, 'a': 0, 'd': 0}
TIMEOUT = 0.4 
running = True

# Global bus variable
bus = None

def init_can():
    global bus
    try:
        # Check if bus is already open
        if bus is not None:
            bus.shutdown()
        
        bus = can.interface.Bus(interface='slcan', channel=CHANNEL, bitrate=1000000)
        print(f"✅ CAN Bus initialized on {CHANNEL}")
    except Exception as e:
        print(f"❌ Failed to open CAN: {e}")
        sys.exit(1)

def can_heartbeat_thread():
    global targets, running
    while running:
        now = time.time()
        
        # Update Targets
        if now - last_seen['w'] < TIMEOUT: targets[MOTOR_02_ID] += MOVE_INCREMENT
        if now - last_seen['s'] < TIMEOUT: targets[MOTOR_02_ID] -= MOVE_INCREMENT
        if now - last_seen['a'] < TIMEOUT: targets[MOTOR_04_ID] -= MOVE_INCREMENT
        if now - last_seen['d'] < TIMEOUT: targets[MOTOR_04_ID] += MOVE_INCREMENT

        for m_id in [MOTOR_04_ID, MOTOR_02_ID]:
            if not is_powered[m_id]: continue
            
            diff = targets[m_id] - current_cmds[m_id]
            if abs(diff) > RAMP_SPEED:
                current_cmds[m_id] += RAMP_SPEED if diff > 0 else -RAMP_SPEED
            else:
                current_cmds[m_id] = targets[m_id]

            p_int = int((max(-12.5, min(12.5, current_cmds[m_id])) - (-12.5)) * 65535 / 25)
            data = struct.pack('>HHHH', p_int, 0, 
                               int(KP_ARM * 65535 / 500), 
                               int(KD_ARM * 65535 / 5))
            
            try:
                bus.send(can.Message(arbitration_id=(0x01 << 24) | (MASTER_ID << 8) | m_id, data=data, is_extended_id=True))
            except:
                pass # Ignore transient send errors
            
        time.sleep(LOOP_INTERVAL)

def is_data():
    return select.select([sys.stdin], [], [], 0) == ([sys.stdin], [], [])

# --- MAIN EXECUTION ---
init_can() # Open the bus ONLY ONCE here
threading.Thread(target=can_heartbeat_thread, daemon=True).start()

print("HOLD [W/S] or [A/D]. [E] Enable. [X] Exit.")

old_settings = termios.tcgetattr(sys.stdin)
try:
    tty.setcbreak(sys.stdin.fileno())
    while True:
        if is_data():
            char = sys.stdin.read(1).lower()
            if char == 'x': break
            
            if char == 'e':
                for m in [MOTOR_04_ID, MOTOR_02_ID]:
                    bus.send(can.Message(arbitration_id=(0x04<<24)|(MASTER_ID<<8)|m, data=[1]+[0]*7, is_extended_id=True))
                    time.sleep(0.05)
                    bus.send(can.Message(arbitration_id=(0x03<<24)|(MASTER_ID<<8)|m, data=[0]*8, is_extended_id=True))
                    is_powered[m] = True
                print("\rMOTORS ENABLED!          ", end="")
            
            if char in last_seen:
                last_seen[char] = time.time()
                termios.tcflush(sys.stdin, termios.TCIFLUSH)
        
        time.sleep(0.01)

except Exception as e:
    print(f"\nCaught Error: {e}")

finally:
    running = False
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)
    print("\nShutting down CAN...")
    if bus:
        try:
            # Try to stop motors
            for m in [7, 127]:
                bus.send(can.Message(arbitration_id=(0x04<<24)|(MASTER_ID<<8)|m, data=[0]*8, is_extended_id=True))
            bus.shutdown()
            print("✅ Bus Shutdown Success.")
        except:
            print("⚠️ Bus could not be closed properly.")