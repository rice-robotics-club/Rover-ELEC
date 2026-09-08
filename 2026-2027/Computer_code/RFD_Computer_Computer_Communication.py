# this program can be ran by both computers and allows them to send messages back and forth simultaneously

import serial
import threading

# Change to match your computer COM for the ESP32
PORT = "COM4"

# Matches ESP's selected baud rate
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=0.1)

print()
print("==============================")
print("      RFD900 RADIO CHAT")
print("==============================")
print(f"Connected to {PORT}")
print("Type a message and press Enter.")
print()

# Constantly checking if the esp is receiving messages from the radio
def receive_messages():
    while True:
        try:
            if ser.in_waiting: # checks to see if any bits have been received 
                message = ser.readline().decode(
                    "utf-8",
                    errors="replace"
                ).strip() # decodes the received bits into a stored message
                # errors = "replace" prevents the code from crashing if junk data is received

                if message: # only prints a messaeg if one was received
                    print(f"\nREMOTE: {message}") # moves to a new line
                    print("YOU: ", end="", flush=True) 

        except Exception as e:
            print("\nSerial error:", e)
            break


receiver = threading.Thread( # allows messages to be received while sending them yourself
    target=receive_messages,
    daemon=True
)

receiver.start()


try: # allows user to send messages
    while True:
        message = input("YOU: ")

        ser.write( # encodes the messages into bits and sends them to the esp
            (message + "\n").encode("utf-8")
        )

except KeyboardInterrupt: # can end code with Crtl+C
    print("\nClosing radio chat.")

finally: # helps end the code
    ser.close() # allows com port to be used by other programs when the code's not running