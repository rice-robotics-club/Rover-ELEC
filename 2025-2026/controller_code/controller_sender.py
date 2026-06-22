import pygame
import serial
import time

PORT = 'COM5'
BAUD = 57600
ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(2)

pygame.init()
pygame.joystick.init()

if pygame.joystick.get_count() == 0:
    print("No controller detected!")
    raise SystemExit

joystick = pygame.joystick.Joystick(0)
joystick.init()

def scale_axis(v):
    # maps -1..1 -> 0..255
    v = max(-1.0, min(1.0, float(v)))
    return int((v + 1.0) * 127.5)

def scale_trigger(v):
    # many controllers report triggers as -1..1, some as 0..1
    v = float(v)
    if v < -0.1:  # likely -1..1 style
        return scale_axis(v)
    v = max(0.0, min(1.0, v))
    return int(v * 255)

HEADER = bytes([0xAA, 0x55])  # start-of-packet marker

while True:
    pygame.event.pump()

    left_x  = joystick.get_axis(0)
    left_y  = -joystick.get_axis(1)
    right_x = joystick.get_axis(2)
    right_y = -joystick.get_axis(3)

    l1 = joystick.get_button(9)
    r1 = joystick.get_button(10)

    l2 = joystick.get_axis(4)
    r2 = joystick.get_axis(5)

    cross    = joystick.get_button(0)
    circle   = joystick.get_button(1)
    square   = joystick.get_button(2)
    triangle = joystick.get_button(3)

    payload = bytearray([
        scale_axis(left_x),
        scale_axis(left_y),
        scale_axis(right_x),
        scale_axis(right_y),
        int(l1),
        int(r1),
        scale_trigger(l2),
        scale_trigger(r2),
        int(cross),
        int(circle),
        int(square),
        int(triangle),
    ])

    # optional checksum (simple XOR)
    chk = 0
    for b in payload:
        chk ^= b

    packet = HEADER + payload + bytes([chk])  # 2 + 12 + 1 = 15 bytes total
    ser.write(packet)

    print("Sent:", list(payload), "chk:", chk)
    time.sleep(0.1)
    
    #sam is hung