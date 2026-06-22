# import serial
# import sys

# ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)

# print("Type commands with newline character. Format is integer,speed. 30000 is a reasonable speed for testing. \n")
# print("0 = stop rover\n1 = rover go forward\n2 = rover go backward\n3 = rover turn right\n4 = rover turn left\n5 = motor0 forward\n6 = motor0 backward\n7 = motor0 stop\n8 = motor1 forward\n9 = motor1 backward\n10 = motor1 stop\n11 = motor2 forward\n12 = motor2 backward\n13 = motor2 stop\n14 = motor3 forward\n15 = motor3 backward\n16 = motor3 stop\n17 = motor4 forward\n18 = motor4 backward\n19 = motor4 stop")


# try:
#     while True:
#         input_data = sys.stdin.readline()
#         if input_data:
#             command = input_data + "\n"
#             ser.write(command.encode())
# except KeyboardInterrupt:
#     print("Exiting...")
# finally:
#     ser.close()

import serial

# Configuration
INPUT_PORT = '/dev/ttyTHS1'  # The "listener" port
OUTPUT_PORT = '/dev/ttyUSB0' # The "relay" port
BAUD_RATE = 57600
BAUD_RATE_USB = 115200

def start_relay():
    try:
        # Initialize both serial connections
        ser_in = serial.Serial(INPUT_PORT, BAUD_RATE, timeout=0.1)
        ser_out = serial.Serial(OUTPUT_PORT, BAUD_RATE_USB, timeout=1)
        
        print(f"Relay active: Listening on {INPUT_PORT} -> Sending to {OUTPUT_PORT}")
        print("Press Ctrl+C to stop.")

        while True:
            # Check if there is data waiting in the input buffer
            if ser_in.in_waiting > 0:
                # Read the line from THS1
                line = ser_in.readline()
                
                # Relay it to USB0
                ser_out.write(line)
                
                # Optional: Print to console for debugging
                print(f"Relayed: {line.decode('utf-8').strip()}")

    except serial.SerialException as e:
        print(f"Error opening serial ports: {e}")
    except KeyboardInterrupt:
        print("\nShutting down relay...")
    finally:
        # Ensure ports are closed on exit
        if 'ser_in' in locals(): ser_in.close()
        if 'ser_out' in locals(): ser_out.close()

if __name__ == "__main__":
    start_relay()