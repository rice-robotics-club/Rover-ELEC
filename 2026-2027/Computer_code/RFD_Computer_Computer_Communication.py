import serial
import threading

# ==============================
# CHANGE THIS TO YOUR ESP32 PORT
# ==============================
PORT = "COM4"

BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=0.1)

print()
print("==============================")
print("      RFD900 RADIO CHAT")
print("==============================")
print(f"Connected to {PORT}")
print("Type a message and press Enter.")
print()


def receive_messages():
    while True:
        try:
            if ser.in_waiting:
                message = ser.readline().decode(
                    "utf-8",
                    errors="replace"
                ).strip()

                if message:
                    print(f"\nREMOTE: {message}")
                    print("YOU: ", end="", flush=True)

        except Exception as e:
            print("\nSerial error:", e)
            break


receiver = threading.Thread(
    target=receive_messages,
    daemon=True
)

receiver.start()


try:
    while True:
        message = input("YOU: ")

        ser.write(
            (message + "\n").encode("utf-8")
        )

except KeyboardInterrupt:
    print("\nClosing radio chat.")

finally:
    ser.close()
```
