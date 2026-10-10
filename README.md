# Rice Robotics URC Rover

Firmware, software, and hardware for Rice Robotics' [University Rover Challenge](https://urc.marssociety.org/) team.

<!-- TODO: rover photo or banner, e.g. ![Rover](docs/media/rover.jpg) — keep it under ~500 KB -->

---

## About

The University Rover Challenge (URC) is an international competition, run by the Mars Society, where student teams design and build a Mars rover prototype. The rover competes in field missions such as science sample analysis, equipment servicing, extreme retrieval and delivery, and autonomous navigation. Check the current year's rules for the exact missions.

This repository holds all of our rover work. The current focus is the **2026-2027 season**.

- **Club website:** TODO
- **URC rules:** TODO (link to the current year's rules)

---

## Repository Layout

```
rice-robotics-urc/
├── 2024-2025/              # archive
├── 2025-2026/              # archive
└── 2026-2027/              # current season
    ├── rover-arm/
    ├── rover-drivetrain/
    ├── rover-science/
    ├── rover-communication/
    ├── rover-power/
    └── rover-integration/
```

Older seasons are kept as-is for reference. **All new work goes in `2026-2027/`.**

---

## Subsystems

| Subsystem | What it does | Lead |
|---|---|---|
| [rover-arm](2026-2027/rover-arm/) | TODO | TODO |
| [rover-drivetrain](2026-2027/rover-drivetrain/) | TODO | TODO |
| [rover-science](2026-2027/rover-science/) | TODO | TODO |
| [rover-communication](2026-2027/rover-communication/) | TODO | TODO |
| [rover-power](2026-2027/rover-power/) | TODO | TODO |
| [rover-integration](2026-2027/rover-integration/) | Jetson, ROS 2 workspace, and connecting all subsystems | TODO |

Not sure who to ask? Start with the subsystem lead.

---

## Subsystem Structure

Every subsystem except `rover-integration` uses the same layout. If you know one, you know them all. `rover-integration` has its own layout, described in [its README](2026-2027/rover-integration/).

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

**`firmware/`**: Code that runs on the microcontrollers. Each folder inside is one board's job, named by what it does (`joint-controller`, `power-monitor`), not by chip.
- `arduino-core/`: Arduino code, built and uploaded with the **Arduino IDE**. The `.ino` file must have the same name as its folder.
- `esp-idf/`: ESP-IDF code, built and uploaded with **PlatformIO**. When an Arduino node is ported to ESP-IDF, it keeps the same folder name.

**`software/`**: Code that runs on the Jetson.
- `rover_<subsystem>_<function>/`: One full ROS 2 package per folder (for example, `rover_arm_control`).
- `dependencies.md`: Outside libraries and tools this subsystem uses, with links and versions.

**`hardware/`**: Physical design files.
- `pcb/`: KiCad source files plus fabrication outputs (Gerbers, BOM, PDF schematic).
- `mechanical/`: CAD files, or links to Onshape/Fusion.

**`scripts/`**: Helper scripts for flashing, calibration, parsing logs, and similar tasks.

### Ground rules

- Don't delete folders your subsystem isn't using yet. Leave the stub `README.md` so every subsystem looks the same.
- Arduino IDE is only for `arduino-core/`, and PlatformIO is only for `esp-idf/`.
- Never commit build outputs (`.pio/`, `build/`, `install/`, `log/`) or large videos.

---

## Tech Stack

| Area | Standard |
|---|---|
| Onboard computer | NVIDIA Jetson, Ubuntu 24.04, ROS 2 Jazzy |
| Microcontrollers | Arduino (initial testing only), ESP32 DevKit V1, ESP32-S3 (long-term target) |
| Firmware | Arduino core (Arduino IDE) or ESP-IDF (PlatformIO) |
| PCB design | KiCad TODO (version) |
| Mechanical CAD | TODO |

---

## Getting Started

1. **Install git** and set up your GitHub account. Ask a lead to add you to the repo.
2. **Clone the repo:**
```bash
   git clone TODO (repo URL)
   cd TODO (repo folder)
```
3. **Install the tools for your subsystem:**
   - **Firmware (Arduino):** [Arduino IDE](https://www.arduino.cc/en/software) + the ESP32 board package
   - **Firmware (ESP-IDF):** [PlatformIO](https://platformio.org/install/ide?install=vscode) in VS Code
   - **ROS 2 / Jetson:** see [`rover-integration/docs/jetson-setup.md`](2026-2027/rover-integration/docs/jetson-setup.md)
   - **PCB:** [KiCad](https://www.kicad.org/download/) TODO (version)
4. **Read your subsystem's README** and [CONTRIBUTING.md](CONTRIBUTING.md) before your first commit.

---

## Contributing

1. Pull the latest `main`.
2. Create a branch: `<subsystem>/<short-description>` (example: `arm/joint-limits`).
3. Commit small, clear changes.
4. Open a pull request into `main`. Your subsystem lead will review it.

Never commit directly to `main`. Full rules are in [CONTRIBUTING.md](CONTRIBUTING.md).

---

## Resources

- **Google Drive (photos, videos, large files):** TODO
- **Discord / Slack:** TODO
- **URC rules:** TODO
- **Team docs / meeting notes:** TODO

---

## License

This project is licensed under the terms in [LICENSE](LICENSE).