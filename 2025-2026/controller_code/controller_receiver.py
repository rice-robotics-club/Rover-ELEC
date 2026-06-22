import serial

PORT = "/dev/ttyUSB0"   # or /dev/ttyACM0 depending on your adapter
BAUD = 57600

ser = serial.Serial(PORT, BAUD, timeout=1)

HEADER = bytes([0xAA, 0x55])
PAYLOAD_LEN = 12
PACKET_LEN = 2 + PAYLOAD_LEN + 1  # header + payload + checksum

def find_packet():
    # scan stream until header found
    while True:
        b = ser.read(1)
        if not b:
            return None
        if b == HEADER[:1]:
            b2 = ser.read(1)
            if b2 == HEADER[1:]:
                rest = ser.read(PAYLOAD_LEN + 1)
                if len(rest) != PAYLOAD_LEN + 1:
                    return None
                payload = rest[:PAYLOAD_LEN]
                chk = rest[-1]
                # verify checksum
                x = 0
                for bb in payload:
                    x ^= bb
                if x == chk:
                    return payload
                # checksum fail → keep searching

while True:
    payload = find_packet()
    if payload is None:
        continue

    data = list(payload)
    # data indices:
    # 0 lx, 1 ly, 2 rx, 3 ry, 4 l1, 5 r1, 6 l2, 7 r2, 8 cross, 9 circle, 10 square, 11 triangle
    print("Got:", data)