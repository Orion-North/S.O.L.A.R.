# S.O.L.A.R. (Self-Operating Legged Autonomous Robot)

**S.O.L.A.R.** is a quadruped platform engineered for long-term autonomy. The project explores the synergy between on-board solar energy harvesting and off-board high-performance Reinforcement Learning (RL) inference.

---

## 🏗 Planned System Architecture
The robot utilizes a **Distributed Compute Model** to balance power efficiency with high-performance processing:
* **Edge (ESP32-CAM):** Manages real-time gait execution, IMU stabilization, and low-latency video streaming.
* **Host (PC / RTX 5080):** Receives telemetry and vision data over Wi-Fi to run complex RL locomotion policies, returning motor commands to the Edge.



## Hardware Stack

### Power & Energy
* **Solar Array:** 3 5V 1W Monocrystalline Solar Panels.
* **Management:** Solar LiPo Charger + 5V 5A BEC (Step-Down) for high-current servo stabilization.
* **Storage:** 7.4V 2S High-Discharge LiPo.

### Compute & Motion
* **Controller:** ESP32-CAM (Vision + Logic).
* **Actuation:** 12-DOF via MG90S Metal Gear Micro Servos (3-DOF per leg).
* **Driver:** PCA9685 16-Channel I2C PWM Controller.

### Structural Materials
* **Frame:** PLA-CF (Carbon Fiber Reinforced) for maximum structural rigidity and minimal weight.
* **Contact:** TPU 95A HF for high-traction, vibration-dampening feet.

---

## Repository Structure
* `/firmware`: ESP32 source code (Inverse Kinematics & UDP Comms).
* `/scripts`: Python-based RL inference and GPU-side processing.
* `/simulation`: URDF models and virtual training environments.
* `/docs`: System schematics and power-cycle logic.

## Development Roadmap
- [x] Hardware Procurement
- [ ] Inverse Kinematics (IK) Implementation
- [ ] WiFi/UDP Telemetry Bridge (ESP32 to RTX 5080)
- [ ] Solar Charge/Sleep Logic Integration
- [ ] Autonomous Navigation (RL Stretch Goal)

---
*Developed as a Capstone STEM Project.*
