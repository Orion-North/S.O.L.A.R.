# S.O.L.A.R. (Self-Operating Legged Autonomous Robot)
<img width="1920" height="1080" alt="solar" src="https://github.com/user-attachments/assets/05dcadb7-5c9f-4229-860c-bed806fdc94c" />
**S.O.L.A.R.** is a quadruped platform engineered for long-term autonomy. The project explores the synergy between on-board solar energy harvesting and off-board high-performance Reinforcement Learning (RL) inference.

---

## Planned System Architecture
The robot utilizes a **Distributed Compute Model** to balance power efficiency with high-performance processing:
* **Edge (ESP32-CAM):** Manages real-time gait execution, IMU stabilization, and low-latency video streaming.
* **Host (PC / RTX 5080):** Receives telemetry and vision data over Wi-Fi to run complex RL locomotion policies, returning motor commands to the Edge.



## Hardware Stack

### Power & Energy
* **Solar Array:** 2 5V 1W Monocrystalline Solar Panels.
* **Management:** Solar LiPo Charger + 5V 5A BEC (Step-Down) for high-current servo stabilization.
* **Storage:** 7.4V 2S High-Discharge LiPo.

### Compute & Motion
* **Controller:** ESP32-CAM (Vision + Logic).
* **Actuation:** 12-DOF via MG90S Metal Gear Micro Servos (3-DOF per leg).
* **Driver:** PCA9685 16-Channel I2C PWM Controller.
* **IMU:** Adafruit 10-DOF over shared I2C. L3GD20H gyroscope and LSM303D accel/mag are sampled locally at 50 Hz; BMP180 is detected but not used for high-rate telemetry.

### Structural Materials
* **Frame:** PLA-CF (Carbon Fiber Reinforced) for maximum structural rigidity and minimal weight.
* **Contact:** TPU 95A HF for high-traction, vibration-dampening feet.

---

## Repository Structure
* `/firmware`: ESP32 code (Inverse Kinematics & UDP Comms).
* `/control-panel`: Technical browser control panel for calibration, telemetry, and low-level operation.
* `/mobile-app`: Consumer-friendly Android-installable remote interface.
* `/remote-gateway`: Optional internet-facing relay that forwards the existing ESP32 HTTP API without changing firmware behavior.
* `/scripts`: Python-based host-side inference and GPU-side processing.
* `/simulation`: Motion-only quadruped RL setup, URDF/USD assets, and virtual training environments.
* `/docs`: System schematics and power-cycle logic.

## Remote Access Direction
The ESP32 should stay on a trusted Wi-Fi network and keep serving the same local API (`/cmd`, `/status`, `/capture`, `/estop`, calibration, OTA). For world-wide control, run `/remote-gateway` on a machine that can reach the robot locally, then expose that gateway through a secure tunnel, VPS, or reverse proxy.

IMU data is exposed without continuous Wi-Fi streaming. `/status` includes low-rate roll, pitch, heading, and readiness fields for the apps. `/imu` returns the latest cached sample with accel, gyro, mag, and attitude values when higher-detail telemetry is needed; `/imu?fmt=bin` returns the same sample as a compact binary frame for host-side polling.

The Android app path is the separate `/mobile-app` PWA:
1. Run the remote gateway with `ROBOT_BASE_URL`, `ROBOT_API_TOKEN`, and `GATEWAY_TOKEN`.
2. Open the gateway URL on Android Chrome.
3. Use Chrome's install option to add S.O.L.A.R. Control to the home screen.
4. Set the target to the gateway URL and enter the gateway access code.

Production remote control requires `GATEWAY_TOKEN`; without it, the gateway refuses robot control routes by default.

## Development Roadmap
- [x] Hardware Procurement
- [x] Build The Robot
- [x] Program Simple Movement
- [x] ESP32 to RTX 5080 Bridge Over Wifi
- [x] RL Inference Bridge Starter
- [ ] Solar Charge/Sleep Logic Integration
- [ ] Autonomous Navigation (RL Stretch Goal)

## RL Starter
The current RL priority is motion only: train the quadruped to stand, walk, and
track velocity commands before adding solar autonomy. The Isaac Lab setup lives
in `/simulation` and is ready to run built-in quadruped training until the
custom S.O.L.A.R. robot model exists.

```powershell
.\simulation\run_builtin_a1_smoke_test.ps1
```

The later host-side inference bridge lives in `/scripts/rl`; keep it separate
from simulator training until a policy is ready to deploy.

## Nested Objectives (For STEM Program Deliverables)
- [x] Build Functioning Quadruped Prototype With No Solar Power (By End Of Capstone Alpha Project)
- [x] Charge The Battery Any Amount With Solar Pannels Mounted On The Robot
- [ ] Program The Robot To Charge And Move So It Can Sustain Itself In Sunny Areas (By End Of Capstone Beta Project)
- [ ] Program The Robot With RL/CV To Locate Sunny Areas And Budget Energy Until They Are Found (Stretch Goal)

---
*Developed as a Capstone STEM Project.*
