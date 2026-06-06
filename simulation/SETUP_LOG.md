# Isaac Lab Setup Log

Date: 2026-05-04

Completed:

- Installed Python 3.12 with `winget`. The current Isaac Lab checkout uses
  Python 3.11, so this is available but not used by the venv.
- Installed `uv` with `winget`.
- Cloned Isaac Lab into `external/IsaacLab`.
- Created `external/IsaacLab/.venv` using Python 3.11.
- Installed `isaacsim[all,extscache]==5.1.0`.
- Installed Isaac Lab editable packages with RSL-RL.
- Installed CUDA PyTorch packages for `cu128`.
- Pinned `tensordict==0.7.2` after `0.12.2` crashed on import during the RSL-RL smoke test.
- Verified CUDA PyTorch sees `NVIDIA GeForce RTX 5080 Laptop GPU`.
- Verified Isaac Sim launches with `OMNI_KIT_ACCEPT_EULA=YES`.
- Verified one headless RSL-RL iteration on `Isaac-Velocity-Flat-Unitree-A1-v0`.
- Confirmed the full RTX GUI path crashes on Windows in
  `rtx.scenedb.plugin.dll`.
- Switched `open_isaac_sim.ps1` to the PXR/Hydra Kit app for a non-headless
  GUI that avoids the RTX scene database crash.
- Imported `simulation/source_model/assembly.step` with FreeCAD and extracted
  component measurements.
- Generated `simulation/assets/solar/solar.urdf` as a simplified motion-only
  training proxy.
- Converted the proxy URDF to `simulation/assets/solar/solar.usd`.
- Added the local Isaac Lab task `Solar-Velocity-Flat-v0`.
- Verified one headless RSL-RL iteration on `Solar-Velocity-Flat-v0` with 16
  environments.
- Updated the proxy hip axes for radial outward hip rotation and set the
  default stance near the real robot's 45-degree hip / 60-degree thigh-down
  pose.
- Ran `Solar-Velocity-Flat-v0` with 2048 environments for 1000 iterations. The
  final checkpoint is `external/IsaacLab/logs/rsl_rl/solar_flat/2026-05-12_18-36-47/model_999.pt`.

Known note:

- `pip check` reports a FastAPI/Starlette version conflict from upstream Isaac
  Sim and Isaac Lab pins. The locomotion smoke test still completed.
