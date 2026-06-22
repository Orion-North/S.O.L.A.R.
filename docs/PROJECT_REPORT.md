# S.O.L.A.R. Project Report

## Abstract

S.O.L.A.R. is a solar-assisted quadruped robot prototype designed to explore
long-duration mobile autonomy on a small embedded platform. The robot combines
an ESP32-CAM edge controller, a PCA9685 servo driver, 12 MG90S servos, an
MPU-6050 IMU, Wi-Fi telemetry, a browser-based control panel, and a simulation
path for future reinforcement learning. The current system has been physically
assembled, calibrated, and verified walking from the control panel. Solar
charging has been demonstrated, while closed-loop solar energy management
remains the next major integration step.

## Problem Statement

Small mobile robots are usually limited by battery capacity. S.O.L.A.R. tests a
practical architecture for extending runtime by combining:

- Lightweight quadruped locomotion.
- Solar-assisted charging hardware.
- A low-power embedded controller for real-time safety and actuation.
- Off-board compute for heavier perception or reinforcement learning work.

The goal is not only to build a walking robot, but to create a platform that can
eventually search for bright areas, rest in a charging posture, and resume
movement when enough energy is available.

## Design Goals

- Build a working 12-DOF quadruped from accessible components.
- Keep servo control and safety logic on the ESP32.
- Expose a simple HTTP API for control, telemetry, calibration, and OTA updates.
- Provide a technical control panel for development and testing.
- Add live IMU telemetry for attitude and policy observations.
- Support a future host-side RL loop without overloading the ESP32.
- Keep solar charging hardware physically integrated with the robot.

## Hardware Architecture

| Subsystem | Implementation |
| --- | --- |
| Edge controller | AI-Thinker ESP32-CAM |
| Actuation | 12 MG90S servos, 3 per leg |
| Servo driver | PCA9685 over I2C |
| IMU | MPU-6050 on a scanned I2C bus |
| Power | 2S LiPo battery and 5 V 5 A BEC |
| Solar | 2x 5 V 1 W panels with a 2S solar LiPo charger |
| Structure | 3D printed PLA-CF frame and TPU feet |

The firmware uses logical body motor IDs for calibration and testing, then maps
those IDs to the physical PCA9685 channels. This mapping has been tested on the
physical robot, and walking from the control panel is confirmed.

Detailed wiring is documented in [WIRING_DIAGRAM.md](WIRING_DIAGRAM.md).

## Firmware Architecture

The ESP32-CAM firmware is responsible for:

- Wi-Fi connection and fallback access point behavior.
- HTTP API routes for robot control and telemetry.
- Servo interpolation and gait execution.
- Emergency stop, torque disable, and command watchdog behavior.
- Calibration storage in ESP32 NVS.
- Camera capture and low-rate FPV pull mode.
- MPU-6050 sampling at 50 Hz.
- OTA update support through a browser upload page.

The firmware intentionally keeps neural-network inference off-board. Host-side
policies send bounded servo action packets through `/rl`, while the ESP32 keeps
watchdog and safety behavior local.

## Control Panel

The technical control panel is used for development and robot operation. It can:

- Connect directly to the ESP32 or through the desktop proxy.
- Send walking and turning commands.
- Trigger emotes such as sit, stretch, wag, wave, and charge-rest.
- Toggle torque and calibration mode.
- Test individual motors.
- Save calibration offsets.
- Display live status and IMU telemetry.
- Pull camera frames from the ESP32-CAM.
- Trigger emergency stop and clear emergency stop.

This panel is the main operator interface used to verify walking behavior.

## Mobile And Remote Access

The mobile app is a simplified PWA intended for remote operation through the
gateway. The remote gateway forwards the existing ESP32 HTTP API through one
authenticated endpoint, so the firmware does not need to be exposed directly to
the public internet.

## Simulation And RL

The simulation folder contains an Isaac Lab setup for quadruped training. The
project includes:

- A generated S.O.L.A.R. proxy asset.
- Flat-ground velocity task setup.
- Solar-charge task setup using panel-voltage observations.
- Host-side Python clients for dry-run and real-robot policy execution.

The current direction is to train solar-aware behavior in simulation, then deploy
bounded host-side inference to the real robot through the `/rl` route.

## Testing And Validation

| Area | Status |
| --- | --- |
| Physical assembly | Complete |
| Individual motor tests | Complete |
| Servo channel map | Verified on robot |
| Walking from control panel | Verified |
| Main firmware build | Verified with PlatformIO |
| Control panel build | Verified with Vite |
| IMU endpoint/API shape | Implemented |
| OTA partition layout | Implemented; final real-board OTA confirmation recommended |
| Solar panel charging | Demonstrated |
| Closed-loop solar telemetry | Not finalized |
| Solar-charge RL training | Scaffolded, not final validated policy |

## Limitations

- Solar voltage telemetry still needs final ADC hardware selection or an I2C ADC
  module.
- The solar-charge RL policy path is scaffolded but still needs a trained,
  hardware-safe checkpoint.
- The ESP32-CAM is useful for low-cost vision and control, but limited for
  onboard ML inference.
- The robot should remain supervised during physical testing because small servo
  quadrupeds can overload, stall, or fall during aggressive motion.

## Future Work

- Finalize and document solar voltage sensing.
- Train the no-IMU solar-charge policy in Isaac Lab.
- Add real-hardware energy logs for panel voltage, pose, and movement state.
- Implement autonomous charge-rest behavior based on measured solar input.
- Add visual sun seeking or environmental navigation.
- Produce a polished public demo video.

## Portfolio Summary

S.O.L.A.R. demonstrates embedded firmware, robotics hardware integration,
browser tooling, telemetry design, safety controls, and early simulation/RL
infrastructure in one project. The most important completed milestone is that
the physical robot can walk under control-panel command with tested motor
mapping and live telemetry.
