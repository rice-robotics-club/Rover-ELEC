from evdev import UInput, AbsInfo, ecodes as e
import time

events = {
    e.EV_KEY: [
        e.BTN_SOUTH,       # A
        e.BTN_EAST,        # B
        e.BTN_WEST,        # X
        e.BTN_NORTH,       # Y

        e.BTN_TL,          # Left bumper
        e.BTN_TR,          # Right bumper

        e.BTN_SELECT,      # Back
        e.BTN_START,       # Start

        e.BTN_MODE,        # Xbox / Guide button

        e.BTN_THUMBL,      # Left stick press
        e.BTN_THUMBR,      # Right stick press
    ],

    e.EV_ABS: [
        (e.ABS_X, AbsInfo(0, -32768, 32767, 0, 0, 0)),   # Left stick X
        (e.ABS_Y, AbsInfo(0, -32768, 32767, 0, 0, 0)),   # Left stick Y

        (e.ABS_RX, AbsInfo(0, -32768, 32767, 0, 0, 0)),  # Right stick X
        (e.ABS_RY, AbsInfo(0, -32768, 32767, 0, 0, 0)),  # Right stick Y

        (e.ABS_Z, AbsInfo(0, 0, 255, 0, 0, 0)),          # Left trigger
        (e.ABS_RZ, AbsInfo(0, 0, 255, 0, 0, 0)),         # Right trigger
    ]
}

ui = UInput(events, name="Virtual Xbox Controller")

# Initialize all axes to neutral
for axis in [e.ABS_X, e.ABS_Y, e.ABS_RX, e.ABS_RY]:
    ui.write(e.EV_ABS, axis, 0)

for axis in [e.ABS_Z, e.ABS_RZ]:
    ui.write(e.EV_ABS, axis, 0)

ui.syn()

time.sleep(1)

# Press A
ui.write(e.EV_KEY, e.BTN_SOUTH, 1)
ui.syn()

time.sleep(0.5)

# Release A
ui.write(e.EV_KEY, e.BTN_SOUTH, 0)
ui.syn()

time.sleep(1000)

ui.close()