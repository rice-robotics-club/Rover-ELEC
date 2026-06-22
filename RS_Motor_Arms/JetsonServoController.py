import serial
import sys
import tty
import termios

# Match the port you found in Step A
USB_PORT = '/dev/ttyUSB0' 
BAUD_RATE = 57600

try:
    ser = serial.Serial(USB_PORT, BAUD_RATE, timeout=0.1)
    print(f"Successfully connected to ESP32 at {USB_PORT}")
except Exception as e:
    print(f"Connection failed: {e}")
    sys.exit()

def get_key():
    """Captures a single keypress from the SSH terminal."""
    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setraw(sys.stdin.fileno())
        ch = sys.stdin.read(1)
        if ch == '\x1b': # Handle the Escape sequence for Arrow Keys
            ch += sys.stdin.read(2)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    return ch

print("Use Arrows to move Servos. Press 'q' to quit.")

try:
    while True:
        key = get_key()
        
        if key == '\x1b[A': # Up Arrow
            ser.write(b'U')
            print("\rAction: S1 UP   ", end="", flush=True)
        elif key == '\x1b[B': # Down Arrow
            ser.write(b'D')
            print("\rAction: S1 DOWN ", end="", flush=True)
        elif key == '\x1b[D': # Left Arrow
            ser.write(b'L')
            print("\rAction: S2 LEFT ", end="", flush=True)
        elif key == '\x1b[C': # Right Arrow
            ser.write(b'R')
            print("\rAction: S2 RIGHT", end="", flush=True)
        elif key.lower() == 'q':
            print("\nQuitting...")
            break
except KeyboardInterrupt:
    pass
finally:
    ser.close()