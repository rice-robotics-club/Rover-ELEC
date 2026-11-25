# Rover control software (ROS 2 Jazzy)

Control chain for the rover: command station laptop, RFD900x radios, Jetson Orin Nano,
SPI, ESP32 motor controllers.

```
Command station (laptop)                 Rover (Jetson, in Docker)                    ESP32s
------------------------                 -------------------------                    ------
controller / GUI nodes                   rover nodes (manual_drive, arm, ...)
   |  ROS topics                            ^  ROS topics        |  ROS topics
   v                                        |                    v
command_pc_gateway ~~~ radio protocol ~~~ rover_gateway       master_spi ~~ SPI protocol ~~ ESP32 (CS0)
                                                                                         ~~ ESP32 (CS1) ...
```

## Design rules

1. **Two protocols, two owners.** Only the two gateways know the radio protocol. Only
   `master_spi` knows the SPI protocol. Nothing else parses bytes.
2. **Gateways are transport only.** They turn radio messages into ordinary ROS topics (and
   back). They hold no control logic and never touch SPI.
3. **Compute stays on the rover.** Radio traffic is kept as small as possible. Rover nodes
   talk to each other over local topics and use the radio only for what the operator
   must send or see.
4. **Translate, never forward raw bytes.** The Jetson converts between the radio format
   and the SPI format; the two are never mixed.
5. **Every link fails safe.** Each hop has its own timeout and falls back to zero output.
6. **Topic names differ per side** (`/station/...` on the laptop, `/rover/...` on the
   rover) so the two ROS graphs cannot feed each other even on the same network. Also use
   different `ROS_DOMAIN_ID`s (the laptop uses 42).

## Layout

```
ros2_ws/src/rover_link/
  rover_link/
    command_pc_gateway.py   laptop: station topics <-> radio
    rover_gateway.py        Jetson: radio <-> rover topics
    manual_drive.py         Jetson: gateway drive command -> drive output (gating, e-stop)
    master_spi.py           Jetson: drive output -> SPI frames to the ESP32
    steady.py               timer on the monotonic clock (immune to wall-clock steps)
    protocol.py, topic_bridge.py, keyboard_motor.py, demo_*.py   older / test code
```

Older tests (`topic_bridge`, `demo_talker`, `demo_echo`, `keyboard_motor`) belong to the
first JSON protocol and do not work with the current gateways.

## Radio protocol (laptop <-> Jetson)

One message:

```
[type][seq][payload ...][CRC-16 high][CRC-16 low]   -> COBS encoded -> 0x00 delimiter
```

- **type**: bit 7 = direction (0 command to the rover, 1 data from the rover), bits 0-6 = code.
- **seq**: 1 byte, wraps 255 -> 0. Detects lost or repeated messages.
- **CRC**: CRC-16/SPI-FUJITSU (poly 0x1021, init 0x1D0F, no reflection, no final XOR),
  high byte first, computed over type, seq and payload before COBS.
- **COBS** removes every 0x00 from the message so a 0x00 can mark message boundaries.
- Control values are one signed byte: value x 127, so -1.0 .. 1.0 maps to -127 .. 127.

| Command code (laptop -> rover) | Meaning | Payload |
|---|---|---|
| 1 | Drive | right byte, left byte |
| 2 | Drive to coordinates | not yet defined |
| 3 | E-stop | none |
| 4 | Placeholder | |
| 5 | Arm teleop | not yet defined |
| 6 | Science | not yet defined |

| Data code (rover -> laptop) | Meaning | Payload |
|---|---|---|
| 1 | Coordinates | not yet defined |
| 2 | Acknowledge | 1 byte: 1 = accepting commands, 0 = failsafe |
| 3 | Twist | not yet defined |
| 4 | Spectrometer | not yet defined |

## SPI protocol (Jetson <-> ESP32)

Fixed 10-byte frame, in both directions:

```
0xAA 0x55 | code | seq | payload (4 bytes, zero padded) | CRC-16 high | CRC-16 low
```

- The CRC is the same CRC-16/SPI-FUJITSU, computed over code, seq and payload (not the header).
- Code 0x01 = drive; payload = right byte, left byte, 0x00, 0x00.
- Example: `AA 55 01 07 5F C0 00 00 94 68` (right 0.75, left -0.5, seq 7).
- The ESP32 reply uses the same layout and arrives one transaction late. It is a
  placeholder echo for now.
- Wiring: one controller, many peripherals. Shared SCK / MOSI / MISO, one chip select per
  ESP32, a 10 kohm pull-up from each CS line to that ESP32's own 3V3, common ground, 3.3 V logic.

## Topics

| Topic | Type | Direction | Meaning |
|---|---|---|---|
| `/station/drive_cmd` | Float32MultiArray [right, left] | into laptop gateway | operator drive command |
| `/station/estop` | Bool | into laptop gateway | e-stop request |
| `/station/link_ok` | Bool | out of laptop gateway | the rover is acknowledging |
| `/rover/drive_cmd` | Float32MultiArray [right, left] | out of rover gateway | received drive command |
| `/rover/estop` | Bool | out of rover gateway | received e-stop |
| `/rover/link_ok` | Bool | out of rover gateway | radio link is alive |
| `/rover/radio_rx` | UInt8MultiArray [type, payload ...] | out of rover gateway | every message received, raw |
| `/manual` | Bool | into manual_drive | true = pass drive commands on |
| `/rover/drive_out` | Float32MultiArray [right, left] | out of manual_drive | the command master_spi sends |

## Build

On the laptop and on the Jetson (inside the container), from `ros2_ws`:

```
colcon build --packages-select rover_link --symlink-install
source install/setup.bash
```

`--symlink-install` makes edits to the Python files take effect without a rebuild. Without
it, colcon copies the files into `install/`, and a stale copy keeps running after you edit
the source. If a topic you expect is missing, check this first, then that both machines
use the same `ROS_DOMAIN_ID`.

## Run

Jetson (radio plugged in before the container starts):

```
ros2 run rover_link rover_gateway --ros-args -p port:=/dev/ttyTHS1
ros2 run rover_link manual_drive
ros2 run rover_link master_spi
```

Command station:

```
ros2 run rover_link command_pc_gateway --ros-args -p port:=/dev/ttyUSB0
```

Then publish commands to `/station/drive_cmd` (and set `/manual` true on the Jetson).
To test without a controller:

```
ros2 topic pub -r 10 /station/drive_cmd std_msgs/msg/Float32MultiArray "{data: [0.5, -0.5]}"
ros2 topic pub -r 1 /manual std_msgs/msg/Bool "{data: true}"
ros2 topic echo /rover/drive_out
```

Useful checks: `ros2 topic echo /station/link_ok` (laptop), `ros2 topic echo /rover/drive_cmd`
(Jetson), and the ESP32 serial monitor at 115200 baud.

## Parameters

| Node | Parameter | Default | Meaning |
|---|---|---|---|
| command_pc_gateway | port, baud | /dev/ttyUSB0, 57600 | radio serial port |
| | send_hz | 10 | heartbeat rate to the rover |
| | cmd_timeout | 1.0 s | no control input for this long -> send zeros |
| | link_timeout | 2.0 s | no ack for this long -> link_ok false |
| rover_gateway | port, baud | /dev/ttyUSB0, 57600 | radio serial port (Jetson: /dev/ttyTHS1) |
| | watchdog_timeout | 0.5 s | radio silent -> publish drive 0, 0 and link_ok false |
| | ack_hz | 2 | acknowledge rate |
| manual_drive | publish_hz | 20 | output rate |
| | cmd_timeout | 0.5 s | stale drive command -> 0, 0 |
| | estop_hold | 1.0 s | output held at 0, 0 after the last e-stop message |
| | neutral | 0.05 | stick value counted as neutral when re-arming after an e-stop |
| master_spi | topic | /rover/drive_out | topic to send over SPI |
| | bus, device | 0, 0 | /dev/spidev<bus>.<device> (device = chip select) |
| | speed_hz, mode | 1000000, 0 | SPI clock and mode |
| | spi_hz | 50 | frames per second |
| | cmd_timeout | 0.5 s | stale command -> frames carry 0, 0 |

`manual_drive` also scales the drive command by a constant `k` (currently 0.5 in code) to
limit speed.

## Safety behaviour

Each stage times out on its own, so one dead node cannot leave the motors running:

| Stage | Trigger | Result |
|---|---|---|
| Laptop gateway | no control input for 1 s | sends drive 0, 0 |
| Laptop gateway | shutdown | sends drive 0, 0 three times |
| Rover gateway | no valid radio message for 0.5 s | publishes drive 0, 0, link_ok false |
| manual_drive | /manual false or no /manual yet | output 0, 0 |
| manual_drive | no drive command for 0.5 s | output 0, 0 |
| manual_drive | e-stop message | output 0, 0 at once, held for 1 s, and re-arms only when the stick is neutral |
| master_spi | no command for 0.5 s | frames carry 0, 0 |
| master_spi | shutdown | sends three stop frames |
| ESP32 | no good frame for 500 ms | motors to 0, 0 |

The e-stop is momentary: the station sends five e-stop messages, and the rover holds the
stop on its own and needs a neutral stick to resume.

## Adding a new subsystem (for example the arm)

1. Decide the radio payload for its command code and the SPI payload for its frame code.
2. Station: a node publishes `/station/<thing>_cmd`; add it to `command_pc_gateway`.
3. Rover gateway: decode the code and publish `/rover/<thing>_cmd`; extend its
   watchdog so silence publishes zeros for it too.
4. Rover node: a small node like `manual_drive` that gates the command (manual flag,
   timeout, e-stop) and publishes `/rover/<thing>_out` at a steady rate.
5. `master_spi`: send `/rover/<thing>_out` to that subsystem's ESP32 (its own chip
   select and sequence counter, and its own stale timeout).
6. ESP32: accept the new code, with a failsafe that zeros the outputs.
7. Update this README and the protocols PDF.

## Radio hardware

- Antennas on before powering the radios.
- Both radios need the same NetID, air speed, frequency range and ECC; the serial baud
  must match `baud` (57600); MAVLink framing off.
- Raw test without ROS: `screen /dev/ttyUSB0 57600` on both machines; typing on one
  should appear on the other. Quit screen with Ctrl-A then K.
- AT commands: `+++` (wait 1 s before and after, no Enter), `ATI5` shows settings,
  `ATS3=25` sets a value, `AT&W` saves, `ATZ` reboots.
- Linux: `sudo usermod -aG dialout $USER`, then log out and in.
- WSL2 cannot see USB serial devices until attached: once `usbipd bind --busid X`, then
  each session `usbipd attach --wsl --busid X` and `sudo chmod 666 /dev/ttyUSB0`.

## Jetson SPI

Enable SPI with `sudo /opt/nvidia/jetson-io/jetson-io.py` (40-pin header, SPI1), then reboot.
`/dev/spidev0.0` is the first chip select, `/dev/spidev0.1` the second. The Jetson has only
two hardware chip selects per bus; more devices need GPIO chip selects or a decoder.
Check header pin numbers against NVIDIA's pinout before wiring.