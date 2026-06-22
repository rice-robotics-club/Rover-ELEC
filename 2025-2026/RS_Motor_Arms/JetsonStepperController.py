import serial
import sys
import tty
import termios

# Pin 12/13 is /dev/ttyTHS2
UART_PORT = '/dev/ttyTHS2' 

try:
    ser = serial.Serial(UART_PORT, 115200, timeout=10)
except Exception as e:
    print(f"Error: Could not open {UART_PORT}. Did you enable UART2 in jetson-io? {e}")
    sys.exit()

def get_key():
    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setraw(sys.stdin.fileno())
        ch = sys.stdin.read(1)
        if ch == '\x1b': # Handle arrow key sequences
            ch += sys.stdin.read(2)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    return ch

print("Manual Control Initialized. Use Arrow Keys. 'q' to quit.")

try:
    while True:
        key = get_key()
        if key == '\x1b[A': # Up Arrow
            ser.write(b"UTest\n")
            ser.flush()
            print("\rSent: UP  ", end="", flush=True)
        elif key == '\x1b[B': # Down Arrow
            print(ser.write(b'D'))
            ser.flush()
            print("\rSent: DOWN", end="", flush=True)
        elif key.lower() == 'q':
            print("\nExiting.")
            break
except KeyboardInterrupt:
    pass
finally:
    ser.close()