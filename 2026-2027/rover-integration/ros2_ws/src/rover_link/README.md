# rover_link

## First test: send an example topic across the radios

No motor or SPI needed. The PC publishes `/station/hello` ("<n>,<time>") once per
second, the radios carry it to the Jetson as `/rover/hello`, a small node echoes it
back as `/rover/pong`, and the PC prints the round-trip time.

```
PC:     demo_talker -> /station/hello -> topic_bridge -> Radio A ~~~ Radio B -> topic_bridge -> /rover/hello -> demo_echo
        demo_talker <- /station/pong  <- topic_bridge <- Radio A ~~~ Radio B <- topic_bridge <- /rover/pong  <-'
```

Jetson (inside Docker, radio plugged in before the container starts):
```
ros2 run rover_link topic_bridge --ros-args -p port:=/dev/ttyUSB0 -p tx_topic:=/rover/pong -p rx_topic:=/rover/hello
ros2 run rover_link demo_echo
```
Command PC:
```
ros2 run rover_link topic_bridge --ros-args -p port:=/dev/ttyUSB0 -p tx_topic:=/station/hello -p rx_topic:=/station/pong
ros2 run rover_link demo_talker
```
Expected on the PC: `Reply to #3: round trip 80 ms` (exact time depends on radio settings).
Also try `ros2 topic echo /rover/hello` on the Jetson. The bridge logs sent/received counts every 5 s.
Messages are limited to `max_hz` (default 5) and about 200 bytes per packet.

## Bring-up checklist
1. Antennas on both radios BEFORE powering them.
2. Plug each radio into its computer; find the port with `ls /dev/ttyUSB*` (or `/dev/ttyACM*`).
   On Linux add yourself to the dialout group: `sudo usermod -aG dialout $USER`, then log out and in.
   (WSL2 cannot see USB serial devices unless attached with usbipd-win.)
3. Raw test with no ROS: `screen /dev/ttyUSB0 57600` on both machines. Typing on one should
   appear on the other. Quit screen with Ctrl-A then K.
4. If nothing appears: both radios need the same NetID, air speed and frequency range, and the
   serial baud must match. Enter command mode with `+++` (wait 1 s before and after, no Enter),
   view settings with `ATI5`, change with e.g. `ATS3=25`, save with `AT&W`, reboot with `ATZ`.
   Names and defaults are in the RFD900x manual.
5. Set different `ROS_DOMAIN_ID`s on the PC and the Jetson so the two ROS networks stay separate.

---

RFD900x radio link: command station PC  ->  Jetson  ->  SPI  ->  ESP32 motor.

```
PC:     keyboard_motor -> /station/motor_cmd -> command_pc_gateway -> Radio A
                                                                          ~~~ 900 MHz ~~~
Jetson: Radio B -> rover_gateway -> /rover/motor_cmd -> spi_bit_node -> SPI -> ESP32
```

Topic names differ on each side (`/station/...` vs `/rover/...`) so the two ROS
graphs can never feed each other even if they end up on the same network.
Still set different `ROS_DOMAIN_ID`s (or `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`).

## Run

Jetson (inside Docker, radio plugged in before the container starts):
```
ros2 run spi_bit spi_bit_node --ros-args -r spi_bit/set:=/rover/motor_cmd -p resend_period:=0.1
ros2 run rover_link rover_gateway --ros-args -p port:=/dev/ttyUSB0
```
Command PC (one terminal each):
```
ros2 run rover_link command_pc_gateway --ros-args -p port:=/dev/ttyUSB0
ros2 run rover_link keyboard_motor        # 1 = ON, 0 = OFF, q = quit
```
`ros2 topic echo /station/link_ok` on the PC shows whether the rover is acking.

## Safety behaviour
- Rover motor is OFF at startup until the first valid packet arrives.
- No valid packet for 0.5 s (radio lost, PC gateway crashed) -> motor OFF.
- Control node silent for 1 s -> PC gateway sends OFF.
- PC gateway sends OFF on shutdown (Ctrl-C).
- The ESP32 should also stop the motor itself if it receives no SPI byte for ~0.5 s.

## Radio settings (both radios)
Same NetID, air speed, frequency range and ECC; serial baud matching `baud`
(default 57600); MAVLink framing off. Antennas on before power.
