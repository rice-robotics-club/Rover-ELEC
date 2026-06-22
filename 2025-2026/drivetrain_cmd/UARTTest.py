import serial
import time

# Try /dev/ttyTHS1 first, then /dev/ttyTHS0 if it fails
port = '/dev/ttyTHS1' 
ser = serial.Serial(port, 115200, timeout=1)

print(f"Testing {port}...")
ser.write(b'HELLO_JETSON')
time.sleep(0.1)
response = ser.read(12)

if response == b'HELLO_JETSON':
    print("SUCCESS: You are on the correct UART port!")
else:
    print(f"FAILED: Received {response}. Try a different /dev/ttyTHS port.")
ser.close()