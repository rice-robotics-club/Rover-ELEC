# Rover Integration

Integration connects every subsystem into one working rover. This team owns the **Jetson**, the **ROS 2 workspace**, and the **communication between the Jetson and every subsystem's microcontrollers**.

Integration has no microcontroller of its own, so its layout is slightly different from the other subsystems.

---

## How this subsystem differs

| Standard subsystem | Integration | Why |
|---|---|---|
| `firmware/` | Removed | No microcontroller of its own |
| `software/` | `ros2_ws/` | Owns the whole rover's ROS 2 workspace |
| — | `jetson/` | Jetson configuration that isn't ROS code |

Everything else (`docs/`, `hardware/`, `scripts/`) works the same as in every other subsystem.

---

## Layout

```
rover-integration/
├── README.md
├── docs/
│   ├── architecture.md
│   ├── hardware.md
│   ├── software.md
│   ├── testing.md
│   └── media/
├── ros2_ws/
│   ├── src/
│   └── rover.repos
├── jetson/
│   ├── README.md
│   ├── udev/
│   └── network/
├── hardware/
│   ├── pcb/
│   └── mechanical/
└── scripts/
```

### What goes where

**`docs/`**: How the whole rover fits together.
- `architecture.md`: Full-rover node graph and how subsystems connect. Includes the **Interfaces** section, the contract every subsystem follows for talking to the Jetson (link type, baud rate, message format, device name).
- `hardware.md`: Jetson I/O, the carrier board, and data cabling to each microcontroller.
- `software.md`: Workspace layout, launch files, and how to build and run.
- `testing.md`: System-level tests such as full bringup, e-stop, and loss of communication.
- `media/`: Small doc images. Videos go on Google Drive or YouTube, linked from `media/README.md`.

**`ros2_ws/`**: The ROS 2 workspace for the whole rover.
- `src/`: Integration's own packages.
  - `rover_interfaces`: Custom messages and services shared by all subsystems.
  - `rover_launch`: Launch files and config that start the whole rover.
- `rover.repos`: Third-party ROS packages built from source, pinned to specific commits.

**`jetson/`**: Everything needed to set up a Jetson from scratch.
- `README.md`: Ubuntu 24.04 and ROS 2 Jazzy install, and first boot.
- `udev/`: Rules that give each microcontroller a fixed name (e.g. `/dev/rover_arm`), so boards don't swap `/dev/ttyUSB0` and `/dev/ttyUSB1` between reboots.
- `network/`: Network config for the link to the base station.

**`hardware/`**: Physical design files.
- `pcb/`: Jetson carrier board (houses the Jetson alongside the microcontrollers): KiCad source plus fab outputs.
- `mechanical/`: Jetson and board mounting, enclosure.

**`scripts/`**: Helper scripts such as `setup_jetson.sh` and `build.sh`.

---

## Quick start

**1. Set up the Jetson.** Follow [`jetson/README.md`](jetson/README.md).

**2. Build the workspace:**

```bash
cd 2026-2027/rover-integration/ros2_ws
source /opt/ros/jazzy/setup.bash
vcs import src < rover.repos                          # third-party source packages
rosdep install --from-paths src --ignore-src -r -y    # system dependencies
colcon build
source install/setup.bash
```

**3. Launch the rover:**

```bash
ros2 launch rover_launch rover.launch.py
```

> `vcs` comes from `sudo apt install python3-vcstool`. Never commit `build/`, `install/`, or `log/`.

---

## Rules for other subsystems

- **Talking to the Jetson?** Follow the Interfaces section in [`docs/architecture.md`](docs/architecture.md). If your board needs something new, talk to the integration lead before writing code.
- **Need a custom message type** used by more than one subsystem? It goes in `rover_interfaces`, not in your own package.
- **Opening a serial port?** Use the fixed udev name (`/dev/rover_<subsystem>`), never `/dev/ttyUSB0`.
- **Your ROS 2 packages** stay in your own subsystem's `software/` folder. Integration pulls them into the full rover build.

---

## Status

- [ ] Jetson set up (Ubuntu 24.04 + ROS 2 Jazzy)
- [ ] Communication protocols finalized and documented in `architecture.md`
- [ ] `rover_interfaces` and `rover_launch` packages created
- [ ] udev rules for each microcontroller
- [ ] Base-station network link
- [ ] Subsystem packages linked into `ros2_ws/` for full-rover builds

---

## Lead

TODO: name / GitHub username