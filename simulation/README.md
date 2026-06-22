# S.O.L.A.R. Motion RL Setup

This folder is for the quadruped RL path. Basic flat locomotion is ready, and
there is now a solar-voltage seeking task that trains without gyro/IMU
observations.

The supported local training stack is:

- Isaac Lab source: `external/IsaacLab`
- Isaac Lab venv: `external/IsaacLab/.venv`
- Isaac Sim: 5.1.0 from NVIDIA pip packages
- RL backend: RSL-RL
- CUDA-capable NVIDIA GPU with PyTorch CUDA support

## What Is Ready

The Isaac Lab software is installed and a built-in quadruped smoke test passed:

```powershell
.\simulation\run_builtin_a1_smoke_test.ps1
```

That command runs one headless PPO iteration on the built-in Unitree A1 flat
velocity-tracking task. It verifies the simulator, PhysX, CUDA, Isaac Lab, and
RSL-RL before the custom S.O.L.A.R. model exists.

## Open The GUI

Open the simulator GUI with:

```powershell
.\simulation\open_isaac_sim.ps1
```

Use this launcher instead of calling `isaacsim.exe` directly. The script sets
the Isaac Lab venv, user-site isolation, and Omniverse EULA environment before
starting the GUI. It uses the PXR/Hydra viewport, which is the stable GUI path
for this project.

The full RTX Isaac Sim launcher is also available:

```powershell
.\simulation\open_isaac_sim_rtx.ps1
```

## Run Built-In Training

Use this as a simulator and RSL-RL regression check. It trains the built-in A1
task and is still useful when debugging Isaac Lab, CUDA, or driver changes:

```powershell
.\simulation\train_builtin_a1.ps1
```

The script defaults to 1024 parallel environments and 1000 iterations. You can
override both:

```powershell
.\simulation\train_builtin_a1.ps1 -NumEnvs 2048 -MaxIterations 2000
```

Training logs go under:

```text
external/IsaacLab/logs/rsl_rl/unitree_a1_flat
```

## S.O.L.A.R. Proxy Asset

The full Fusion STEP assembly lives in:

```text
simulation/source_model/assembly.step
```

The generated Isaac training proxy lives in:

```text
simulation/assets/solar/solar.urdf
simulation/assets/solar/solar.usd
```

The proxy is intentionally simple: one body box, 12 actuated servo joints, four
leg chains, and four fixed foot bodies named `FL_foot`, `FR_foot`, `RL_foot`,
and `RR_foot`. Regenerate it from the STEP analysis with:

```powershell
python .\simulation\tools\generate_solar_proxy_urdf.py
.\simulation\convert_solar_urdf.ps1
```

Do not merge fixed joints during conversion. Isaac Lab needs the foot bodies to
remain separate for contact rewards and termination checks.

## Run S.O.L.A.R. Proxy Training

The local task is registered as:

```text
Solar-Velocity-Flat-v0
```

Solar-aware charging training is registered separately as:

```text
Solar-Charge-Flat-NoIMU-v0
```

Run a quick smoke test:

```powershell
.\simulation\train_solar_proxy.ps1 -NumEnvs 16 -MaxIterations 1
```

Run a first real flat-ground training pass:

```powershell
.\simulation\train_solar_proxy.ps1 -NumEnvs 256 -MaxIterations 300
```

Run a longer high-throughput training pass:

```powershell
.\simulation\train_solar_proxy.ps1 -NumEnvs 2048 -MaxIterations 1000
```

Train the no-IMU solar-charge policy, which observes only previous servo
actions, an open-loop gait phase, and simulated panel voltage. It is rewarded
for walking on flat ground toward a bright patch within a 12-second episode,
then lowering into a still charging pose while minimizing motion and joint
power:

```powershell
.\simulation\train_solar_charge.ps1 -NumEnvs 32768 -MaxIterations 2500
```

Logs go under:

```text
external/IsaacLab/logs/rsl_rl/solar_flat
external/IsaacLab/logs/rsl_rl/solar_flat_solar_charge_no_imu
```

The current proxy standing pose uses radial hips near 45 degrees outward and
thigh joints just inside their 60-degree downward mechanical stop.

## Model Boundary

The generated proxy is intentionally simpler than the physical robot. Before
using a trained policy as more than a simulation experiment, inspect and tune:

- 12 actuated revolute joints, 3 per leg.
- Correct joint names and left/right sign conventions.
- Approximate body and leg masses.
- Joint limits matching the safe servo range.
- Foot collision geometry with realistic friction.
- A neutral standing pose.

The flat velocity and no-IMU solar-charge tasks are registered, but any policy
trained from this proxy should be treated as experimental until it is validated
with conservative output scales on real hardware.
