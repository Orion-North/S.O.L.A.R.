# S.O.L.A.R. Motion RL Setup

This folder is for the motion-only quadruped RL path. Solar charging,
sun-seeking, camera navigation, and energy budgeting are intentionally out of
scope until basic locomotion works.

The installed local stack is:

- Isaac Lab source: `external/IsaacLab`
- Isaac Lab venv: `external/IsaacLab/.venv`
- Isaac Sim: 5.1.0 from NVIDIA pip packages
- RL backend: RSL-RL
- Verified GPU: RTX 5080 via CUDA PyTorch

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
starting the GUI. It uses the PXR/Hydra viewport because the full RTX viewport
currently crashes on this Windows 11 / RTX 5080 machine inside
`rtx.scenedb.plugin.dll`.

The full RTX Isaac Sim launcher is still available for retesting after driver or
Isaac Sim updates:

```powershell
.\simulation\open_isaac_sim_rtx.ps1
```

## Run Built-In Training

Use this while waiting on the S.O.L.A.R. model:

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

## Model Boundary

Do not start the custom S.O.L.A.R. environment until the robot model exists.
The model should provide:

- 12 actuated revolute joints, 3 per leg.
- Correct joint names and left/right sign conventions.
- Approximate body and leg masses.
- Joint limits matching the safe servo range.
- Foot collision geometry with realistic friction.
- A neutral standing pose.

Once that exists, the next step is to clone the built-in A1 velocity task into a
S.O.L.A.R. task and swap in the S.O.L.A.R. asset.
