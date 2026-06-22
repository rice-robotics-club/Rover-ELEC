import serial
import can
import sys
import time

# --- CONFIGURATION ---
RADIO_PORT = '/dev/ttyTHS1'   # Hardware UART Pins 8 & 10
RADIO_BAUD = 57600
CAN_CHANNEL = '/dev/ttyACM0'  # USB-to-CAN adapter
MASTER_ID = 0xFD              # Standard Robstride Master ID
MOTOR_IDS = [7, 127]          # Tracking both 04 and 02

# Initialize CAN Bus
try:
    bus = can.interface.Bus(interface='slcan', channel=CAN_CHANNEL, bitrate=1000000)
    print(f"✅ CAN initialized on {CAN_CHANNEL}")
except Exception as e:
    print(f"❌ CAN Error: {e}")
    sys.exit(1)

# Initialize Radio
try:
    radio = serial.Serial(RADIO_PORT, RADIO_BAUD, timeout=0.01)
    print(f"✅ Radio listening on {RADIO_PORT}")
    print("TIP: If no data appears, ensure 'sudo systemctl stop nvgetty' was run.")
except Exception as e:
    print(f"❌ UART Error: {e}")
    sys.exit(1)

def run_bridge():
    HEADER = [0xAA, 0x55]
    print("\n--- JETSON DUAL-MOTOR BRIDGE ACTIVE ---")
    print(f"Listening for Motor ID {MOTOR_IDS[0]} (04) and ID {MOTOR_IDS[1]} (02)...")

    try:
        while True:
            # Expected Packet: [AA][55] [MODE] [ID] [DATA x8] [CHK] = 13 bytes
            if radio.in_waiting >= 13:
                # Look for 2-byte Header
                if radio.read(1)[0] == HEADER[0]:
                    if radio.read(1)[0] == HEADER[1]:
                        
                        # Read the rest of the frame
                        mode_byte = radio.read(1)[0]
                        m_id = radio.read(1)[0]
                        can_payload = radio.read(8)
                        received_chk = radio.read(1)[0]

                        # XOR Checksum Validation
                        calc_chk = mode_byte ^ m_id
                        for b in can_payload:
                            calc_chk ^= b

                        if calc_chk == received_chk:
                            # 1. Build the Robstride Arbitration ID
                            # Mode 0x01 = Move, 0x04 = Enable/Stop, 0x03 = Operational
                            arb_id = (mode_byte << 24) | (MASTER_ID << 8) | m_id
                            
                            # 2. Forward to CAN Bus
                            msg = can.Message(
                                arbitration_id=arb_id,
                                data=can_payload,
                                is_extended_id=True
                            )
                            bus.send(msg)
                            
                            # 3. Print Feedback
                            motor_name = "04" if m_id == 7 else "02"
                            m_type = "MOVE" if mode_byte == 0x01 else "CMD "
                            print(f"[{m_type}] Motor {motor_name} (ID {m_id:3}) | Data: {can_payload.hex().upper()}")
                        else:
                            print("⚠️ Checksum Mismatch")
            
            # Ultra-low sleep to keep response snappy
            time.sleep(0.001)

    except KeyboardInterrupt:
        print("\n\nShutting down bridge...")
    finally:
        # EMERGENCY STOP: Force both motors to Limp/Stop on exit
        print("Sending safety STOP to all motors...")
        for m in MOTOR_IDS:
            stop_id = (0x04 << 24) | (MASTER_ID << 8) | m
            try:
                bus.send(can.Message(arbitration_id=stop_id, data=[0]*8, is_extended_id=True))
            except:
                pass
        
        radio.close()
        bus.shutdown()
        print("Done.")

if __name__ == "__main__":
    run_bridge()