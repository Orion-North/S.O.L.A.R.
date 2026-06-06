import math

import torch


def simulated_solar_voltage(
    env,
    sunny_voltage: float = 1.0,
    dark_voltage: float = 0.15,
    sun_center_x: float = 1.2,
    sun_center_y: float = 0.0,
    sun_radius: float = 0.8,
    asset_cfg=None,
) -> torch.Tensor:
    """Synthetic panel voltage from a smooth circular sun patch on the floor."""
    asset_cfg = asset_cfg or getattr(env, "_solar_robot_asset_cfg", None)
    asset_name = asset_cfg.name if asset_cfg is not None else "robot"
    asset = env.scene[asset_name]
    root_pos = asset.data.root_pos_w
    dx = root_pos[:, 0] - sun_center_x
    dy = root_pos[:, 1] - sun_center_y
    dist_sq = dx * dx + dy * dy
    sigma_sq = max(sun_radius * sun_radius, 1.0e-6)
    intensity = torch.exp(-dist_sq / (2.0 * sigma_sq))
    return dark_voltage + (sunny_voltage - dark_voltage) * intensity


def solar_panel_voltage(
    env,
    normalize_by: float = 1.0,
    sunny_voltage: float = 1.0,
    dark_voltage: float = 0.15,
    sun_center_x: float = 1.2,
    sun_center_y: float = 0.0,
    sun_radius: float = 0.8,
    asset_cfg=None,
) -> torch.Tensor:
    """Expose simulated panel voltage as a policy observation."""
    voltage = simulated_solar_voltage(
        env,
        sunny_voltage=sunny_voltage,
        dark_voltage=dark_voltage,
        sun_center_x=sun_center_x,
        sun_center_y=sun_center_y,
        sun_radius=sun_radius,
        asset_cfg=asset_cfg,
    )
    if normalize_by > 0.0:
        voltage = voltage / normalize_by
    return voltage.unsqueeze(-1)


def phase_clock(env, cycle_time: float = 0.85, harmonics: int = 2) -> torch.Tensor:
    """Open-loop gait phase from elapsed episode time."""
    elapsed = env.episode_length_buf.to(dtype=torch.float32) * env.step_dt
    phase = torch.remainder(elapsed / cycle_time, 1.0) * (2.0 * math.pi)
    clocks = []
    for harmonic in range(1, harmonics + 1):
        harmonic_phase = phase * harmonic
        clocks.extend((torch.sin(harmonic_phase), torch.cos(harmonic_phase)))
    return torch.stack(clocks, dim=-1)
