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

Known note:

- `pip check` reports a FastAPI/Starlette version conflict from upstream Isaac
  Sim and Isaac Lab pins. The locomotion smoke test still completed.
