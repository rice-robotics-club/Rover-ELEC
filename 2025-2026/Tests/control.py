import serial
import time

ser = None
def drill(value):
    ser.write(b"*L"+str(value).encode("utf-8")+b"&")

def motor(m, value):
    ser.write(b"*M"+str(value).encode("utf-8")+b"&")

try:
    # Open the serial port
    # Replace '/dev/ttyUSB0' with your actual port and 9600 with your baud rate
    ser = serial.Serial('/dev/ttyUSB0', 9600, timeout=1) 
    print(f"Serial port {ser.name} opened successfully.")

    # Data to send (must be bytes)
    # Write data to the serial port

    while True:

        drill(100)


        time.sleep(2);

        ser.write(b"*S0 0 1&")
        ser.write(b"*L0&")


        ser.write(b"*R2 0.15&")
        ser.write(b"*R3 0.15&")

        
        ser.write(b"*C2 1000&")
        ser.write(b"*C3 1000&")

        time.sleep(20)


        ser.write(b"*C2 -1000&")
        ser.write(b"*C3 -1000&")


        time.sleep(20)

except serial.SerialException as e:
    print(f"Error: {e}")

finally:
    # Close the serial port
    if 'ser' in locals() and ser.is_open:
        ser.close()
        print("Serial port closed.")
