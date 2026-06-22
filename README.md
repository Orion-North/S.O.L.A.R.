# S.O.L.A.R. - Self-Operating Legged Autonomous Robot

S.O.L.A.R. is a solar-assisted 12-DOF quadruped robot built as a capstone STEM
project. The current prototype walks from a browser control panel, streams
camera and IMU telemetry over Wi-Fi, supports OTA firmware updates, and includes
the first host-side simulation/RL path for future solar-seeking autonomy.

Walking demo video: [docs/assets/hero-walking-control-panel.mp4](docs/assets/hero-walking-control-panel.mp4)

## Current Status

- Built and assembled a physical 12-servo quadruped.
- Verified every motor channel and calibration path.
- Confirmed walking from the technical control panel.
- Added live MPU-6050 roll, pitch, acceleration, and gyro telemetry.
- Added ESP32-CAM HTTP API, camera capture, emergency stop, torque control,
  calibration storage, OTA update page, and charge-rest posture.
- Added a mobile PWA and authenticated remote gateway path.
- Added Isaac Lab simulation and host-side RL deployment scaffolding.
- Solar panel charging has been demonstrated, but closed-loop solar telemetry
  and autonomous energy behavior are still future work.

## Hardware

| Area | Components |
| --- | --- |
| Controller | AI-Thinker ESP32-CAM |
| Actuation | 12x MG90S metal gear micro servos |
| Servo driver | PCA9685 16-channel I2C PWM board |
| IMU | MPU-6050 accelerometer and gyroscope |
| Power | 7.4 V 2S LiPo, 5 V 5 A BEC |
| Solar | 2x 5 V 1 W monocrystalline panels and 2S solar charger |
| Structure | PLA-CF frame components, TPU 95A feet |
| Host compute | RTX laptop/PC for simulation and off-board inference |

See [PARTS_LIST.md](PARTS_LIST.md) and [docs/WIRING_DIAGRAM.md](docs/WIRING_DIAGRAM.md)
for wiring and part-level detail.

## Software

| Path | Purpose |
| --- | --- |
| [firmware](firmware/) | ESP32-CAM firmware for movement, telemetry, camera, OTA, safety, and calibration |
| [control-panel](control-panel/) | Browser/Electron technical panel for walking, calibration, camera, and telemetry |
| [mobile-app](mobile-app/) | Android-installable PWA for simplified remote operation |
| [remote-gateway](remote-gateway/) | Authenticated relay for controlling the local robot through one endpoint |
| [scripts/rl](scripts/rl/) | Host-side robot client and policy runner |
| [simulation](simulation/) | Isaac Lab assets, training scripts, and solar-aware RL task setup |
| [docs](docs/) | Portfolio report, API reference, wiring, demo checklist, and capstone deliverables |

## Documentation

- [Project Report](docs/PROJECT_REPORT.md): publication-style writeup for the
  portfolio page.
- [API Reference](docs/API.md): robot HTTP routes and telemetry payloads.
- [Wiring Diagram](docs/WIRING_DIAGRAM.md): power, I2C, servo, IMU, and solar
  telemetry wiring notes.
- [Simulation README](simulation/README.md): Isaac Lab setup and training tasks.
- [Host Inference README](scripts/rl/README.md): real-robot policy runner.

## Build And Run

Main ESP32 firmware:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

Control panel:

```powershell
cd control-panel
npm install
npm run build
npm run electron:dev
```

Remote gateway:

```powershell
cd remote-gateway
$env:ROBOT_BASE_URL="http://solar.local"
$env:GATEWAY_TOKEN="choose-a-remote-access-code"
npm start
```

Isaac Lab smoke test:

```powershell
.\simulation\run_builtin_a1_smoke_test.ps1
```

## Roadmap

- [x] Build physical quadruped prototype.
- [x] Test individual motors and servo channel map.
- [x] Walk from the browser control panel.
- [x] Add live IMU telemetry.
- [x] Add camera capture and FPV pull mode.
- [x] Add OTA-capable partition layout.
- [x] Add host-side RL client and Isaac Lab simulation setup.
- [ ] Finalize solar panel voltage telemetry hardware.
- [ ] Train and validate solar-charge policy in simulation.
- [ ] Deploy bounded solar-aware behavior on real hardware.
- [ ] Add autonomous navigation or computer-vision sun seeking.

Developed as a Capstone STEM Project.
