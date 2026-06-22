import serial
from pynput import keyboard

# Change to your PC's radio port (e.g., 'COM3' on Windows or '/dev/ttyUSB0' on Linux)
ser = serial.Serial('/dev/ttyUSB0', 57600)

def on_press(key):
    try:
        if key == keyboard.Key.up:
            ser.write(b'U')
        elif key == keyboard.Key.down:
            ser.write(b'D')
        elif key == keyboard.Key.left:
            ser.write(b'L')
        elif key == keyboard.Key.right:
            ser.write(b'R')
    except Exception as e:
        print(f"Error: {e}")

print("Radio Controller Active. Use Arrows to send commands...")
with keyboard.Listener(on_press=on_press) as listener:
    listener.join()
    