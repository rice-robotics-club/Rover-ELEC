#!/usr/bin/env python3
"""
Raspberry Pi UART controller for the Arduino Dynamixel Hat.

Connects to the Arduino Hat via the Pi's hardware UART (GPIO14/15)
and sends serial commands to control Dynamixel motors (IDs 1-4).

Usage:
    python3 raspi_dxl_control.py              # Interactive mode
    python3 raspi_dxl_control.py "P 1"        # One-shot command
    python3 raspi_dxl_control.py --scan       # Scan all motor IDs
    python3 raspi_dxl_control.py --home       # Home all motors to position 2048
    python3 raspi_dxl_control.py --torque 1   # Enable torque on all motors

Pi UART Setup (one-time):
    1. sudo raspi-config  →  Interface Options  →  Serial Port
       - "Would you like a login shell?"  →  No
       - "Would you like the hardware enabled?"  →  Yes
    2. sudo reboot
    3. Confirm: ls -l /dev/serial0  (should link to ttyAMA0 or ttyS0)

Wiring:
    Pi GPIO14 (TXD)  →  Arduino pin 7 (RX)
    Pi GPIO15 (RXD)  →  Arduino pin 6 (TX)
    Pi GND           →  Arduino GND
"""

import sys
import time
import argparse

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip3 install pyserial")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

import os

# Try common Pi UART devices in order of preference
_UART_CANDIDATES = [
    "/dev/serial0",      # symlink created by raspi-config when UART is enabled
    "/dev/ttyAMA0",      # hardware UART (if Bluetooth disabled)
    "/dev/ttyS0",        # mini UART (always present on Pi 3/4/5)
]

def _find_uart():
    for dev in _UART_CANDIDATES:
        if os.path.exists(dev):
            return dev
    return _UART_CANDIDATES[-1]  # fallback to ttyS0, let it fail with a clear error

UART_PORT = _find_uart()
BAUD = 57600
TIMEOUT = 2.0

MOTOR_IDS = (1, 2, 3, 4)

# ---------------------------------------------------------------------------
# UART Communication
# ---------------------------------------------------------------------------

class DXLHat:
    """Wraps the serial connection to the Arduino Dynamixel Hat."""

    def __init__(self, port=UART_PORT, baud=BAUD, timeout=TIMEOUT):
        self.ser = serial.Serial(port, baud, timeout=timeout)
        time.sleep(0.1)  # let the port settle
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    def send(self, cmd: str) -> str:
        """Send a command and return the response line(s)."""
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()
        time.sleep(0.05)  # brief wait for Arduino to process
        response = self._read_response()
        return response

    def _read_response(self) -> str:
        """Read all available response lines."""
        lines = []
        while True:
            line = self.ser.readline()
            if not line:
                break
            decoded = line.decode(errors="replace").strip()
            if decoded:
                lines.append(decoded)
        return "\n".join(lines)

    # --- Convenience methods -----------------------------------------------

    def check_connection(self) -> bool:
        """Verify the Arduino Hat is responding on the UART."""
        resp = self.send("HELP")
        # The HELP response starts with "OK --- Commands ---"
        return resp.startswith("OK --- Commands ---")

    def ping(self, motor_id: int) -> bool:
        resp = self.send(f"P {motor_id}")
        return resp.startswith("OK Ping")

    def torque(self, motor_id: int, enable: bool) -> bool:
        val = 1 if enable else 0
        resp = self.send(f"T {motor_id} {val}")
        return resp.startswith("OK")

    def goal(self, motor_id: int, position: int) -> bool:
        resp = self.send(f"G {motor_id} {position}")
        return resp.startswith("OK")

    def goal_degrees(self, motor_id: int, degrees: float) -> bool:
        resp = self.send(f"GD {motor_id} {degrees}")
        return resp.startswith("OK")

    def position(self, motor_id: int) -> int | None:
        resp = self.send(f"S {motor_id}")
        if resp.startswith("OK Position"):
            try:
                return int(resp.split("=")[-1].strip())
            except (ValueError, IndexError):
                return None
        return None

    def position_degrees(self, motor_id: int) -> float | None:
        resp = self.send(f"SD {motor_id}")
        if resp.startswith("OK PosDeg"):
            try:
                return float(resp.split("=")[-1].strip())
            except (ValueError, IndexError):
                return None
        return None

    def velocity(self, motor_id: int, vel: int) -> bool:
        resp = self.send(f"V {motor_id} {vel}")
        return resp.startswith("OK")

    def led(self, motor_id: int, on: bool) -> bool:
        val = 1 if on else 0
        resp = self.send(f"LED {motor_id} {val}")
        return resp.startswith("OK")

    def set_mode(self, motor_id: int, mode: int) -> bool:
        resp = self.send(f"MODE {motor_id} {mode}")
        return resp.startswith("OK")

    def read_register(self, motor_id: int, addr: int, length: int) -> str:
        return self.send(f"R {motor_id} {addr} {length}")

    def write_register(self, motor_id: int, addr: int, value: int,
                       width: int = 1) -> bool:
        if width == 1:
            resp = self.send(f"W {motor_id} {addr} {value}")
        elif width == 2:
            resp = self.send(f"W2 {motor_id} {addr} {value}")
        elif width == 4:
            resp = self.send(f"W4 {motor_id} {addr} {value}")
        else:
            return False
        return resp.startswith("OK")

    def reboot(self, motor_id: int) -> bool:
        resp = self.send(f"REBOOT {motor_id}")
        return resp.startswith("OK")

    def factory_reset(self, motor_id: int):
        return self.send(f"FACTORY {motor_id}")

    def scan(self) -> dict[int, bool]:
        """Scan for all configured motor IDs. Returns {id: alive}."""
        results = {}
        for mid in MOTOR_IDS:
            results[mid] = self.ping(mid)
        return results


# ---------------------------------------------------------------------------
# Interactive Shell
# ---------------------------------------------------------------------------

HELP_TEXT = """
Commands:
  p <id>              Ping motor
  t <id> <0|1>        Torque off/on
  g <id> <pos>        Set goal position (raw, 0-4095)
  gd <id> <deg>       Set goal position in degrees
  v <id> <vel>        Set goal velocity
  s <id>              Read present position (raw)
  sd <id>             Read present position (degrees)
  led <id> <0|1>      LED off/on
  mode <id> <m>       Set operating mode (3=position, 1=velocity, 0=current)
  r <id> <addr> <n>   Read n bytes from register
  w <id> <addr> <v>   Write 1 byte to register
  w2 <id> <addr> <v>  Write 2 bytes to register
  w4 <id> <addr> <v>  Write 4 bytes to register
  reboot <id>         Reboot motor
  factory <id>        Factory reset (DANGER)

  scan                Ping all motors (1-4)
  torque-on           Enable torque on all motors
  torque-off          Disable torque on all motors
  home                Home all motors to position 2048
  all <cmd> <args>    Send command to all motors (e.g. "all g 2048")
  raw <text>          Send raw text to Arduino
  help                Show this help
  quit / exit / q     Exit
"""


def interactive_shell(hat: DXLHat):
    """Run the interactive control shell."""
    print("=== Raspberry Pi Dynamixel Control ===")
    print(f"UART: {UART_PORT} @ {BAUD} baud")
    print(f"Motors: {list(MOTOR_IDS)}")
    print("Type 'help' for commands, 'quit' to exit.\n")

    # Check Arduino connectivity first
    print("Checking Arduino...", end=" ", flush=True)
    if not hat.check_connection():
        print("NO RESPONSE")
        print("ERROR: Cannot communicate with Arduino Hat.")
        print("Check: power, wiring (GPIO14→pin7, GPIO15→pin6, GND→GND)")
        print("       Arduino firmware uploaded? Correct board selected?")
        return
    print("OK")

    # Scan motors
    print("Scanning motors...")
    results = hat.scan()
    for mid, alive in results.items():
        status = "OK" if alive else "NO RESPONSE"
        print(f"  Motor {mid}: {status}")
    print()

    while True:
        try:
            line = input("dxl> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not line:
            continue

        parts = line.split()
        cmd = parts[0].lower()

        # --- Exit ---
        if cmd in ("quit", "exit", "q"):
            break

        # --- Help ---
        elif cmd == "help":
            print(HELP_TEXT)

        # --- Scan ---
        elif cmd == "scan":
            results = hat.scan()
            for mid, alive in results.items():
                status = "OK" if alive else "NO RESPONSE"
                print(f"  Motor {mid}: {status}")

        # --- Torque all ---
        elif cmd == "torque-on":
            for mid in MOTOR_IDS:
                resp = hat.send(f"T {mid} 1")
                print(f"  Motor {mid}: {resp}")

        elif cmd == "torque-off":
            for mid in MOTOR_IDS:
                resp = hat.send(f"T {mid} 0")
                print(f"  Motor {mid}: {resp}")

        # --- Home all motors ---
        elif cmd == "home":
            # Enable torque on all, then set to centre position (2048)
            for mid in MOTOR_IDS:
                hat.send(f"T {mid} 1")
            time.sleep(0.2)
            for mid in MOTOR_IDS:
                resp = hat.send(f"G {mid} 2048")
                print(f"  Motor {mid}: {resp}")

        # --- Broadcast to all motors ---
        elif cmd == "all":
            if len(parts) < 2:
                print("ERR usage: all <command> <args>")
                continue
            # Reconstruct: "all g 2048" → send "G 1 2048", "G 2 2048", etc.
            sub_cmd = parts[1].upper()
            sub_args = " ".join(parts[2:])
            for mid in MOTOR_IDS:
                resp = hat.send(f"{sub_cmd} {mid} {sub_args}")
                print(f"  Motor {mid}: {resp}")

        # --- Raw send ---
        elif cmd == "raw":
            if len(parts) < 2:
                print("ERR usage: raw <text>")
                continue
            raw_text = " ".join(parts[1:])
            resp = hat.send(raw_text)
            print(resp)

        # --- Single-character commands (p, t, g, s, v, etc.) ---
        else:
            # Forward directly to the Arduino — just uppercase the command
            # and pass through
            resp = hat.send(line.upper() if cmd in ("p","t","g","gd","v","s","sd",
                                                      "r","w","w2","w4","led",
                                                      "mode","reboot","factory")
                            else line)
            print(resp)

    print("Goodbye.")


# ---------------------------------------------------------------------------
# One-Shot / CLI Mode
# ---------------------------------------------------------------------------

def oneshot(hat: DXLHat, command: str):
    """Send a single command and print the response."""
    resp = hat.send(command)
    print(resp)

def scan_mode(hat: DXLHat):
    """Scan all motor IDs and report status."""
    print("Scanning motors...")
    results = hat.scan()
    for mid, alive in results.items():
        status = "OK" if alive else "NO RESPONSE"
        print(f"  Motor {mid}: {status}")

def home_mode(hat: DXLHat):
    """Home all motors to centre position."""
    print("Homing all motors to 2048...")
    for mid in MOTOR_IDS:
        hat.send(f"T {mid} 1")
    time.sleep(0.2)
    for mid in MOTOR_IDS:
        resp = hat.send(f"G {mid} 2048")
        print(f"  Motor {mid}: {resp}")

def torque_mode(hat: DXLHat, enable: bool):
    """Set torque on all motors."""
    val = 1 if enable else 0
    action = "ON" if enable else "OFF"
    print(f"Torque {action} all motors...")
    for mid in MOTOR_IDS:
        resp = hat.send(f"T {mid} {val}")
        print(f"  Motor {mid}: {resp}")

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Raspberry Pi Dynamixel Hat Controller")
    parser.add_argument("command", nargs="?", default=None,
                        help="Single command to send (e.g. 'P 1', 'G 1 2048'). "
                             "Omit for interactive mode.")
    parser.add_argument("--port", "-p", default=UART_PORT,
                        help=f"UART port (default: {UART_PORT})")
    parser.add_argument("--baud", "-b", type=int, default=BAUD,
                        help=f"Baud rate (default: {BAUD})")
    parser.add_argument("--scan", action="store_true",
                        help="Scan all motor IDs and exit")
    parser.add_argument("--home", action="store_true",
                        help="Home all motors to position 2048")
    parser.add_argument("--torque", type=int, choices=[0, 1], metavar="0|1",
                        help="Set torque on all motors (0=off, 1=on)")
    args = parser.parse_args()

    # Open UART
    try:
        hat = DXLHat(port=args.port, baud=args.baud)
    except serial.SerialException as e:
        print(f"ERROR: Cannot open {args.port}: {e}")
        print("Check: sudo raspi-config → Interface Options → Serial Port")
        print("       Is the UART enabled and not used as a login shell?")
        sys.exit(1)
    except PermissionError:
        print(f"ERROR: Permission denied for {args.port}")
        print("Try: sudo chmod 666 {args.port}")
        print("  or add user to dialout group: sudo usermod -a -G dialout $USER")
        sys.exit(1)

    try:
        if args.command:
            # One-shot command mode
            oneshot(hat, args.command)
        elif args.scan:
            scan_mode(hat)
        elif args.home:
            home_mode(hat)
        elif args.torque is not None:
            torque_mode(hat, bool(args.torque))
        else:
            # Interactive mode
            interactive_shell(hat)
    finally:
        hat.close()


if __name__ == "__main__":
    main()
