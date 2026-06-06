import torch

from isaaclab.assets import Articulation
from isaaclab.assets import RigidObject
from isaaclab.managers import SceneEntityCfg

from .observations import simulated_solar_voltage


def forward_velocity_b(
    env, max_reward: float = 2.0, asset_cfg: SceneEntityCfg = SceneEntityCfg("robot")
) -> torch.Tensor:
    """Reward forward base-frame velocity, clipped to keep training bounded."""
    asset: RigidObject = env.scene[asset_cfg.name]
    return torch.clamp(asset.data.root_lin_vel_b[:, 0], min=0.0, max=max_reward)


def forward_velocity_b_unclipped(env, asset_cfg: SceneEntityCfg = SceneEntityCfg("robot")) -> torch.Tensor:
    """Log raw forward base-frame velocity as a low-weight metric term."""
    asset: RigidObject = env.scene[asset_cfg.name]
    return asset.data.root_lin_vel_b[:, 0]


def reverse_velocity_l2(env, asset_cfg: SceneEntityCfg = SceneEntityCfg("robot")) -> torch.Tensor:
    """Penalize backwards base-frame velocity."""
    asset: RigidObject = env.scene[asset_cfg.name]
    return torch.square(torch.clamp(asset.data.root_lin_vel_b[:, 0], max=0.0))


def lateral_velocity_l2(env, asset_cfg: SceneEntityCfg = SceneEntityCfg("robot")) -> torch.Tensor:
    """Penalize sideways base-frame velocity."""
    asset: RigidObject = env.scene[asset_cfg.name]
    return torch.square(asset.data.root_lin_vel_b[:, 1])


def yaw_rate_l2(env, asset_cfg: SceneEntityCfg = SceneEntityCfg("robot")) -> torch.Tensor:
    """Penalize yaw spin while optimizing forward speed."""
    asset: RigidObject = env.scene[asset_cfg.name]
    return torch.square(asset.data.root_ang_vel_b[:, 2])


def solar_charge_reward(
    env,
    sunny_voltage: float = 1.0,
    dark_voltage: float = 0.15,
    sun_center_x: float = 1.2,
    sun_center_y: float = 0.0,
    sun_radius: float = 0.8,
    target_voltage: float = 0.80,
    asset_cfg: SceneEntityCfg = SceneEntityCfg("robot"),
) -> torch.Tensor:
    """Reward being in strong simulated sun."""
    voltage = simulated_solar_voltage(
        env,
        sunny_voltage=sunny_voltage,
        dark_voltage=dark_voltage,
        sun_center_x=sun_center_x,
        sun_center_y=sun_center_y,
        sun_radius=sun_radius,
        asset_cfg=asset_cfg,
    )
    return torch.clamp(voltage / max(target_voltage, 1.0e-6), max=1.0)


def solar_search_velocity_reward(
    env,
    sunny_voltage: float = 1.0,
    dark_voltage: float = 0.15,
    sun_center_x: float = 1.2,
    sun_center_y: float = 0.0,
    sun_radius: float = 0.8,
    target_voltage: float = 0.80,
    max_reward: float = 1.0,
    asset_cfg: SceneEntityCfg = SceneEntityCfg("robot"),
) -> torch.Tensor:
    """Reward forward search only while panel voltage is below the charging target."""
    asset: RigidObject = env.scene[asset_cfg.name]
    voltage = simulated_solar_voltage(
        env,
        sunny_voltage=sunny_voltage,
        dark_voltage=dark_voltage,
        sun_center_x=sun_center_x,
        sun_center_y=sun_center_y,
        sun_radius=sun_radius,
        asset_cfg=asset_cfg,
    )
    needs_sun = torch.clamp((target_voltage - voltage) / max(target_voltage, 1.0e-6), min=0.0, max=1.0)
    forward_speed = torch.clamp(asset.data.root_lin_vel_b[:, 0], min=0.0, max=max_reward)
    return needs_sun * forward_speed


def solar_rest_reward(
    env,
    sunny_voltage: float = 1.0,
    dark_voltage: float = 0.15,
    sun_center_x: float = 1.2,
    sun_center_y: float = 0.0,
    sun_radius: float = 0.8,
    target_voltage: float = 0.80,
    sit_height: float = 0.085,
    height_tolerance: float = 0.035,
    asset_cfg: SceneEntityCfg = SceneEntityCfg("robot"),
) -> torch.Tensor:
    """Reward a low, still charging pose once the robot is in sun."""
    asset: RigidObject = env.scene[asset_cfg.name]
    voltage = simulated_solar_voltage(
        env,
        sunny_voltage=sunny_voltage,
        dark_voltage=dark_voltage,
        sun_center_x=sun_center_x,
        sun_center_y=sun_center_y,
        sun_radius=sun_radius,
        asset_cfg=asset_cfg,
    )
    charging = torch.clamp(voltage / max(target_voltage, 1.0e-6), max=1.0)
    speed_sq = torch.sum(torch.square(asset.data.root_lin_vel_b[:, :2]), dim=1)
    yaw_sq = torch.square(asset.data.root_ang_vel_b[:, 2])
    stillness = torch.exp(-8.0 * speed_sq - 2.0 * yaw_sq)
    height_error = (asset.data.root_pos_w[:, 2] - sit_height) / max(height_tolerance, 1.0e-6)
    sit_pose = torch.exp(-torch.square(height_error))
    return charging * stillness * sit_pose


def solar_charging_motion_l2(
    env,
    sunny_voltage: float = 1.0,
    dark_voltage: float = 0.15,
    sun_center_x: float = 1.2,
    sun_center_y: float = 0.0,
    sun_radius: float = 0.8,
    target_voltage: float = 0.80,
    asset_cfg: SceneEntityCfg = SceneEntityCfg("robot"),
) -> torch.Tensor:
    """Penalize movement while panel voltage says the robot is charging."""
    asset: RigidObject = env.scene[asset_cfg.name]
    voltage = simulated_solar_voltage(
        env,
        sunny_voltage=sunny_voltage,
        dark_voltage=dark_voltage,
        sun_center_x=sun_center_x,
        sun_center_y=sun_center_y,
        sun_radius=sun_radius,
        asset_cfg=asset_cfg,
    )
    charging = torch.clamp(voltage / max(target_voltage, 1.0e-6), max=1.0)
    lin_xy = torch.sum(torch.square(asset.data.root_lin_vel_b[:, :2]), dim=1)
    yaw = torch.square(asset.data.root_ang_vel_b[:, 2])
    return charging * (lin_xy + yaw)


def joint_power_l1(env, asset_cfg: SceneEntityCfg = SceneEntityCfg("robot")) -> torch.Tensor:
    """Approximate electrical effort as absolute joint mechanical power."""
    asset: Articulation = env.scene[asset_cfg.name]
    torque = getattr(asset.data, "applied_torque", None)
    if torque is None:
        torque = getattr(asset.data, "computed_torque", None)
    if torque is None:
        return torch.zeros(env.num_envs, device=env.device)
    return torch.sum(torch.abs(torque * asset.data.joint_vel), dim=1)
