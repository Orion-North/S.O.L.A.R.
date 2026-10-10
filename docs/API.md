# S.O.L.A.R. Robot API Reference

The ESP32-CAM exposes a local HTTP API for control, telemetry, calibration,
camera capture, and OTA updates. The technical control panel, mobile app,
remote gateway, and host-side RL scripts all build on these routes.

Unless command-token checks are disabled, control routes require the API token
configured in `firmware/solar_main/secrets.h`.

## Core Status

### Wi-Fi profiles

The robot starts `SOLAR_AP` on every boot, then tries the home Wi-Fi profile
for up to 15 seconds and the secondary profile for up to 20 seconds. The AP
stays available at `192.168.4.1` while either station profile is connected.
It accepts up to four devices. If both station attempts fail, the firmware
disables STA and runs AP-only so station scans cannot interrupt direct control.
If a connected station network is lost for five seconds, it also switches to
AP-only. Restart the robot to retry home/school Wi-Fi; it does not scan or retry
those networks during AP-only operation.

Configure credentials locally in the ignored `firmware/solar_main/secrets.h`.
For `SFUNET-SECURE`, set `SOLAR_SECONDARY_ENTERPRISE` to `1`, the SSID to
`SFUNET-SECURE`, and supply `SOLAR_SECONDARY_USERNAME` (`computingID@sfu.ca`),
`SOLAR_SECONDARY_PASSWORD`, and `SOLAR_SECONDARY_CA_PEM` (the current SFU
Wi-Fi certificate in PEM format). The identity defaults to the username.
This uses PEAP/MSCHAPv2, as documented in
[SFU's device setup guide](https://www.sfu.ca/information-systems/services/wireless-connectivity/sfunet-secure/other-devices.html).
An incomplete enterprise profile is skipped; certificate validation remains enabled.
Use a C++ raw string for the certificate, for example:

```cpp
#define SOLAR_SECONDARY_CA_PEM R"PEM(-----BEGIN CERTIFICATE-----
...current certificate contents...
-----END CERTIFICATE-----
)PEM"
```

Set `SOLAR_SECONDARY_ENTERPRISE` to `0` to use a conventional shared-password
secondary network instead. The full `/status` response reports
`secondary_wifi_configured`, `wifi_profile` (`home`, `secondary`, or `none`),
`wifi_mode`, `ip`, and `ap_ip`. Existing `home_wifi_status` and `home_wifi_rssi`
fields retain their names for compatibility and describe the station connection.

School connectivity must be tested on campus, including access from the laptop
to the robot's assigned IP. If local device traffic is blocked, connect the
laptop directly to `SOLAR_AP` and set the console target to `192.168.4.1`.
The firmware seeds certificate time checks from its build timestamp and updates
the clock with NTP after connecting; rebuild after certificate changes.

| Route | Method | Purpose |
| --- | --- | --- |
| `/` | GET | Plain-text online check with firmware version. |
| `/version` | GET | Firmware version string. |
| `/ping` | GET | Connectivity test. |
| `/status` | GET | Full JSON robot status. |
| `/status?fast=1` | GET | Smaller low-rate status payload. |
| `/debug` | GET | Diagnostic text. Boot/recovery log retains the newest 2,048 bytes; task-safe snapshot followed by current mode, torque, calibration, flash and heap. Oldest text may be truncated mid-line. |
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

### IMU zeroing

With motors disabled, place the robot in the reference pose you want to treat
as level and keep it still. `POST /imu/calibrate` returns 202 immediately and
collects 100 fresh samples at 50 Hz. Poll `/imu` for `calibration_state`
(`collecting`, `saving`, `complete`, or `failed`) and `calibration_samples`.
Only `complete` confirms that the reference and gyro biases were saved in NVS.
They persist across reboot and OTA updates.

Motor activation, missing readings, acceleration outside 0.8–1.2 g, excessive
rotation, or excessive sample variance rejects calibration. Failure retains
the previous saved zero. Starting while motors are enabled, starting twice,
or resetting during calibration returns 409. Stale/missing IMU data returns 503.

`roll_deg` and `pitch_deg` are relative to the saved reference; `gyro_dps` has
the stationary bias removed. Raw accelerometer values remain unchanged.
`raw_roll_deg`, `raw_pitch_deg`, and `raw_gyro_dps` remain available for diagnostics.
`calibrated`, `zero_roll_deg`, `zero_pitch_deg`, `gyro_bias_dps`, and
`calibration_message` describe the saved calibration. Binary `/imu` and `/obs`,
and `/status`, use corrected readings without changing their binary layout.
This is roll/pitch zeroing and gyro bias correction, not accelerometer scale
calibration or a full sensor-to-body rotation transform.

`POST /imu/calibration/reset` clears the saved zero and biases with motors off.
The Windows panel provides **Zero IMU** and **Reset IMU Zero** controls.

## Camera And Flash

| Route | Method | Purpose |
| --- | --- | --- |
| `/capture` | GET | Capture one ESP32-CAM JPEG frame. |
| `/flash?state=1` / `/flash?state=0` | GET | Set the front white flash on GPIO4 on/off. |
| `/flash/auto` | GET | Clear the manual front flash by switching it off (legacy route). |

Both full and fast `/status` responses report `flash_enabled`. The red GPIO33
status LED continues its normal heartbeat independently of the front flash.

On boards with PSRAM, capture uses VGA JPEG, two frame buffers, a 20 MHz
camera clock and latest-frame buffering. The delivery limit is 20 FPS while
stationary and 10 FPS in `walk` mode; these are ceilings, not guaranteed rates.
Boards without PSRAM use QVGA with a single buffer in internal RAM.

Successful captures include `X-Camera-Interval-Ms` for client pacing and
`X-Camera-Frame-Ms` for the captured frame's timestamp since boot. HTTP 429
responses include `X-Retry-After-Ms`. Clients should keep one capture in flight
and include request/decode time in their interval instead of adding a full
delay after each frame. The Windows proxy passes these headers to the panel.

JPEG acquisition and transfer run in a dedicated worker. `/capture` retains
the same port, API-token checks, image format, and pacing headers. Only one
camera request is accepted at a time; overlapping requests receive HTTP 429.
A stalled transfer is closed after 2.5 seconds of sending, with its frame
buffer returned. Control requests and the web updater can proceed while a
camera transfer is active.

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

Web OTA is the only firmware update service. ArduinoOTA has been removed;
the browser `/ota` page and upload still use `SOLAR_OTA_USER` and
`SOLAR_OTA_PASSWORD`. An empty password disables the web updater.

The project uses `board_build.partitions = min_spiffs.csv` so web OTA has `app0`
and `app1` slots. A board that was previously flashed with a non-OTA partition
layout must be flashed once over USB with the current build before web OTA can
swap firmware reliably.
