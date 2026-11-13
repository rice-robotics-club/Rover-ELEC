#!/usr/bin/env python3
"""
Sends fake telemetry packets (same format as the ESP32 firmware) to a
pseudo-terminal through socat.

Packet (39 bytes, little-endian, packed):
  0xAA 0x55 | 3 x [uint32 motorID, uint16 pos, vel, torque, temp] | 0x00
"""

import math
import struct
import subprocess
import sys
import time

PORT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fs"
RATE_HZ = 100
DISPLAY_HZ = 20

SYNC = (0xAA, 0x55)
MOTOR_IDS = (0x07, 0x7F, 0x04)
FMT = "<2B" + "IHHHH" * 3 + "B"   # 39 bytes
assert struct.calcsize(FMT) == 39

POS_MAX = 4 * math.pi    # +/- rad
VEL_MAX = 44.0           # +/- rad/s
TQ_MAX = 17.0            # +/- N*m


def to_u16(x, lo, hi):
    """Same mapping as floatToUint16() in the firmware."""
    x = max(lo, min(hi, x))
    return int((x - lo) * 65535.0 / (hi - lo) + 0.5)


def make_packet(t):
    fields = list(SYNC)
    for k, mid in enumerate(MOTOR_IDS):
        if k < 2:
            # Robstrides: sine waves, phase-shifted per motor
            w = 2 * math.pi * 0.5                       # 0.5 Hz
            ph = k * math.pi / 2
            pos = 3.0 * math.sin(w * t + ph)            # rad
            vel = 3.0 * w * math.cos(w * t + ph)        # rad/s (derivative)
            tq = 5.0 * math.sin(2 * w * t + ph)         # N*m
            temp = 30.0 + 0.5 * t % 20                  # degC, slow ramp
            fields += [
                mid,
                to_u16(pos, -POS_MAX, POS_MAX),
                to_u16(vel, -VEL_MAX, VEL_MAX),
                to_u16(tq, -TQ_MAX, TQ_MAX),
                int(temp * 10),
            ]
        else:
            # Dynamixel slot: firmware only fills in the ID, rest is zero
            fields += [mid, 0, 0, 0, 0]
    fields.append(0x00)  # placeholder "CRC" like the firmware
    return struct.pack(FMT, *fields)


def bits(b):
    """Bytes -> '10101010 01010101 ...' in wire order."""
    return " ".join(f"{x:08b}" for x in b)


def format_packet(pkt):
    """Return the lines to display (bytes exactly as sent on the wire)."""
    lines = [f"SYNC        {bits(pkt[0:2])}    CRC {bits(pkt[38:39])}"]
    for k in range(3):
        m = pkt[2 + 12 * k: 14 + 12 * k]
        lines.append(f"M{k} id,pos    {bits(m[0:6])}")
        lines.append(f"   vel,tq,tmp {bits(m[6:12])}")
    return lines


class LiveDisplay:
    """Redraws the same block of lines in place instead of scrolling."""

    def __init__(self):
        self.n = 0

    def draw(self, lines):
        out = []
        if self.n:
            out.append(f"\x1b[{self.n}A")      # cursor up to start of block
        for line in lines:
            out.append(f"\r\x1b[K{line}\n")    # clear line, write, newline
        sys.stdout.write("".join(out))
        sys.stdout.flush()
        self.n = len(lines)


def main():
    cmd = ["socat", "-", f"{PORT},raw,echo=0"]
    print(f"Running: {' '.join(cmd)}  ({RATE_HZ} Hz, Ctrl-C to stop)")
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)

    display = LiveDisplay()
    display_period = 1.0 / DISPLAY_HZ
    next_draw = 0.0

    period = 1.0 / RATE_HZ
    start = time.perf_counter()
    next_t = start
    try:
        while True:
            if proc.poll() is not None:
                print("socat exited, is the port valid?", file=sys.stderr)
                break
            pkt = make_packet(time.perf_counter() - start)
            proc.stdin.write(pkt)
            proc.stdin.flush()

            now = time.perf_counter()
            if now >= next_draw:
                display.draw(format_packet(pkt))
                next_draw = now + display_period

            next_t += period
            delay = next_t - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
    except (KeyboardInterrupt, BrokenPipeError):
        pass
    finally:
        try:
            proc.stdin.close()
        except Exception:
            pass
        proc.terminate()


if __name__ == "__main__":
    main()