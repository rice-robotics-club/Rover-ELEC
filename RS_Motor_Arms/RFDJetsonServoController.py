import serial
import sys

# Configuration
RADIO_PORT = '/dev/ttyTHS1'  # Pins 8 (TX) and 10 (RX)
USB_PORT = '/dev/ttyUSB0'    # ESP32 USB
BAUD_RATE = 57600

try:
    # Open Radio
    radio = serial.Serial(RADIO_PORT, BAUD_RATE, timeout=0.05)
    # Open ESP32
    esp32 = serial.Serial(USB_PORT, BAUD_RATE, timeout=0.05)
    print(f"Bridge Active: {RADIO_PORT} <--> {USB_PORT}")
except Exception as e:
    print(f"Error opening ports: {e}")
    print("Ensure you ran: sudo chmod 666 /dev/ttyTHS1 /dev/ttyUSB0")
    sys.exit()

try:
    while True:
        # 1. Listen for data coming from the Radio (Host Computer)
        if radio.in_waiting > 0:
            data = radio.read(radio.in_waiting)
            esp32.write(data) # Forward to ESP32
            print(f"Relayed from Radio: {data.decode(errors='ignore')}")

        # 2. (Optional) Listen for data from ESP32 to send back to Radio
        if esp32.in_waiting > 0:
            data = esp32.read(esp32.in_waiting)
            radio.write(data.encode())
            
except KeyboardInterrupt:
    print("\nShutting down bridge...")
finally:
    radio.close()
    esp32.close()