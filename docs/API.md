# S.O.L.A.R. Robot API Reference

The ESP32-CAM exposes a local HTTP API for control, telemetry, calibration,
camera capture, and OTA updates. The technical control panel, mobile app,
remote gateway, and host-side RL scripts all build on these routes.

Unless command-token checks are disabled, control routes require the API token
configured in `firmware/solar_main/secrets.h`.

## Core Status

| Route | Method | Purpose |
| --- | --- | --- |
| `/` | GET | Plain-text online check with firmware version. |
| `/version` | GET | Firmware version string. |
| `/ping` | GET | Connectivity test. |
| `/status` | GET | Full JSON robot status. |
| `/status?fast=1` | GET | Smaller low-rate status payload. |
| `/debug` | GET | Diagnostic text. |
| `/i2c` | GET | I2C scan and IMU address diagnostics. |

Important `/status` fields include:

- `mode`
- `uptime_ms`
- `last_cmd_ms_ago`
- `emergency_stop`
- `torque_enabled`
- `calibration_mode`
- `solar_panel_voltage_v`
- `imu_ready`
- `accel_ready`
- `gyro_ready`
- `mpu6050_addr`
- `roll_deg`
- `pitch_deg`

## Motion And Safety

| Route | Method | Purpose |
| --- | --- | --- |
| `/cmd` | GET | High-level motion command. |
| `/rl` | GET | Bounded normalized servo-action packet from host policy. |
| `/obs` | GET | Compact observation endpoint for host-side policy loops. |
| `/torque?state=1` | GET | Enable servo torque. |
| `/torque?state=0` | GET | Disable servo torque. |
| `/estop` | GET | Latch emergency stop and disable torque. |
| `/estop/clear` | GET | Clear emergency stop; torque remains off. |
| `/charge-rest` | GET | Tuck into charge-rest posture, then disable torque. |

Common `/cmd` parameters:

- `mode`: `stand`, `idle`, `manual`, `walk`, `sit`, `stretch`, `wag`, `dance`,
  `flip`, `wave`, or `rl`
- `vx`: forward/back command
- `vy`: lateral command, currently kept near zero for normal operation
- `wz`: turn command
- `speed`: gait speed scalar
- `stride`: stride amplitude
- `lift`: foot lift amplitude

## IMU Telemetry

| Route | Method | Purpose |
| --- | --- | --- |
| `/imu` | GET | Latest cached MPU-6050 sample as JSON. |
| `/imu?fmt=bin` | GET | Latest cached sample as compact binary frame. |

JSON fields include:

- `seq`
- `sample_ms`
- `age_ms`
- `rate_hz`
- `imu_ready`
- `mpu6050_addr`
- `accel_ready`
- `gyro_ready`
- `accel_g`
- `gyro_dps`
- `roll_deg`
- `pitch_deg`

The control panel polls `/imu` directly for live telemetry and uses `/status`
for coarse robot state.

## Camera And Flash

| Route | Method | Purpose |
| --- | --- | --- |
| `/capture` | GET | Capture one ESP32-CAM JPEG frame. |
| `/flash` | GET | Toggle flash output. |
| `/flash/auto` | GET | Automatic flash behavior for capture. |

## Calibration

| Route | Method | Purpose |
| --- | --- | --- |
| `/settings/get` | GET | Read leg set assignments, offsets, and motor-channel map. |
| `/settings/set` | GET | Save calibration settings to NVS. |
| `/calib?state=1` | GET | Enable calibration mode. |
| `/calib?state=0` | GET | Disable calibration mode. |
| `/test?motor=N&angle=A` | GET | Drive one logical body motor to a test angle. |
| `/testseq` | GET | Identify leg sets through a test sequence. |
| `/seq` | GET | Execute a saved/path command sequence. |

Calibration and `/test?motor=` use logical body motor IDs. The firmware maps
those IDs to physical PCA9685 channels internally.

## OTA

| Route | Method | Purpose |
| --- | --- | --- |
| `/ota` | GET | Browser upload page with current firmware version. |
| `/ota` | POST | Upload a PlatformIO firmware `.bin`. |

The project uses `board_build.partitions = default.csv` so web OTA has `app0`
and `app1` slots. A board that was previously flashed with a non-OTA partition
layout must be flashed once over USB with the current build before web OTA can
swap firmware reliably.
