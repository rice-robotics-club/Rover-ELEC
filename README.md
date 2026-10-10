## Subsystem Structure

Every subsystem in `2026-2027/` (`rover-arm`, `rover-drivetrain`, `rover-science`, `rover-communication`, `rover-power`) uses the same layout. If you know one, you 
know them all. `rover-integration` has its own layout and is documented separately.

```
rover-<subsystem>/
├── README.md
├── docs/
│   ├── architecture.md
│   ├── hardware.md
│   ├── software.md
│   ├── testing.md
│   └── media/
├── firmware/
│   ├── arduino-core/
│   │   └── <node-name>/
│   │       ├── README.md
│   │       └── <node-name>.ino
│   └── esp-idf/
│       └── <node-name>/
│           ├── README.md
│           ├── platformio.ini
│           └── src/
├── software/
│   ├── rover_<subsystem>_<function>/
│   └── dependencies.md
├── hardware/
│   ├── pcb/
│   └── mechanical/
└── scripts/
```

### What goes where

**`README.md`**: Start here. What the subsystem does, which boards it uses, and how to build and upload.

**`docs/`**: Everything someone needs to understand the subsystem without asking you.
- `architecture.md`: The big picture. Block diagram and how the pieces talk to each other.
- `hardware.md`: Power, wiring, and pinout. "Which wire goes where?"
- `software.md`: ROS 2 nodes, topics, parameters, and how to run them.
- `testing.md`: How things were tested, the results, and how each number was measured.
- `media/`: Small images used in the docs (diagrams, pinouts). Videos and photo dumps go on Google Drive or YouTube, linked from `media/README.md`.

**`firmware/`**: Code that runs on the microcontrollers. Each folder inside is one board's job, named by what it does (`joint-controller`, `power-monitor`), not by 
chip.
- `arduino-core/`: Arduino code, built and uploaded with the **Arduino IDE**. The `.ino` file must have the same name as its folder.
- `esp-idf/`: ESP-IDF code, built and uploaded with **PlatformIO**. When an Arduino node is ported to ESP-IDF, it keeps the same folder name.

**`software/`**: Code that runs on the Jetson (Ubuntu 24.04, ROS 2 Jazzy).
- `rover_<subsystem>_<function>/`: One full ROS 2 package per folder (for example, `rover_arm_control`).
- `dependencies.md`: Outside libraries and tools this subsystem uses, with links and versions.

**`hardware/`**: Physical design files.
- `pcb/`: KiCad source files plus fabrication outputs (Gerbers, BOM, PDF schematic).
- `mechanical/`: CAD files, or links to Onshape/Fusion.

**`scripts/`**: Helper scripts for flashing, calibration, parsing logs, and similar tasks.

### Ground rules

- Don't delete folders your subsystem isn't using yet. Leave the stub `README.md` so every subsystem looks the same.
- Allowed boards: Arduino (initial testing only), ESP32 DevKit V1, ESP32-S3.
- Arduino IDE is only for `arduino-core/`, and PlatformIO is only for `esp-idf/`.
- Never commit build outputs (`.pio/`, `build/`, `install/`, `log/`) or large videos.# Rover-ELEC
Repo for all you rover elec fiends
