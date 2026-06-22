import isaaclab.envs.mdp as base_mdp
from isaaclab.managers import ObservationTermCfg as ObsTerm
from isaaclab.managers import RewardTermCfg as RewTerm
from isaaclab.managers import SceneEntityCfg
from isaaclab.managers import TerminationTermCfg as DoneTerm
from isaaclab.sensors import ImuCfg
from isaaclab.utils import configclass

from solar_lab.tasks.locomotion.velocity import mdp as solar_mdp

from .rough_env_cfg import SolarRoughEnvCfg


@configclass
class SolarFlatEnvCfg(SolarRoughEnvCfg):
    def __post_init__(self):
        super().__post_init__()

        self.rewards.flat_orientation_l2.weight = -2.5
        self.rewards.feet_air_time.weight = 0.25

        self.scene.terrain.terrain_type = "plane"
        self.scene.terrain.terrain_generator = None
        self.scene.height_scanner = None
        self.observations.policy.height_scan = None
        self.curriculum.terrain_levels = None


class SolarFlatEnvCfg_PLAY(SolarFlatEnvCfg):
    def __post_init__(self) -> None:
        super().__post_init__()

        self.scene.num_envs = 50
        self.scene.env_spacing = 2.0
        self.observations.policy.enable_corruption = False
        self.events.base_external_force_torque = None
        self.events.push_robot = None


@configclass
class SolarFlatNoFeedbackEnvCfg(SolarFlatEnvCfg):
    def __post_init__(self) -> None:
        super().__post_init__()

        # Deployable with the current robot: no IMU, no joint encoders, no measured base velocity.
        self.observations.policy.base_lin_vel = None
        self.observations.policy.base_ang_vel = None
        self.observations.policy.projected_gravity = None
        self.observations.policy.joint_pos = None
        self.observations.policy.joint_vel = None
        self.observations.policy.phase = ObsTerm(
            func=solar_mdp.phase_clock, params={"cycle_time": 0.85, "harmonics": 2}
        )
        self.observations.policy.enable_corruption = False

        self.actions.joint_pos.scale = 0.085
        self.commands.base_velocity.heading_command = False
        self.commands.base_velocity.rel_standing_envs = 0.10
        self.commands.base_velocity.ranges.lin_vel_x = (0.0, 0.35)
        self.commands.base_velocity.ranges.lin_vel_y = (0.0, 0.0)
        self.commands.base_velocity.ranges.ang_vel_z = (0.0, 0.0)
        self.commands.base_velocity.ranges.heading = None

        self.events.base_external_force_torque = None
        self.events.push_robot = None
        self.events.add_base_mass.params["mass_distribution_params"] = (-0.02, 0.04)

        self.rewards.track_lin_vel_xy_exp.weight = 2.0
        self.rewards.track_ang_vel_z_exp.weight = 0.10
        self.rewards.feet_air_time.weight = 0.03
        self.rewards.action_rate_l2.weight = -0.03
        self.rewards.dof_acc_l2.weight = -8.0e-7


@configclass
class SolarFlatNoFeedbackEnvCfg_PLAY(SolarFlatNoFeedbackEnvCfg):
    def __post_init__(self) -> None:
        super().__post_init__()

        self.scene.num_envs = 50
        self.scene.env_spacing = 2.0


@configclass
class SolarFlatImuSpeedEnvCfg(SolarFlatEnvCfg):
    def __post_init__(self) -> None:
        super().__post_init__()

        self.scene.imu = ImuCfg(prim_path="{ENV_REGEX_NS}/Robot/base_link")

        self.observations.policy.base_lin_vel = None
        self.observations.policy.base_ang_vel = ObsTerm(
            func=base_mdp.imu_ang_vel,
            params={"asset_cfg": SceneEntityCfg("imu")},
            clip=(-8.0, 8.0),
            scale=0.25,
        )
        self.observations.policy.projected_gravity = ObsTerm(
            func=base_mdp.imu_projected_gravity,
            params={"asset_cfg": SceneEntityCfg("imu")},
        )
        self.observations.policy.imu_lin_acc = ObsTerm(
            func=base_mdp.imu_lin_acc,
            params={"asset_cfg": SceneEntityCfg("imu")},
            clip=(-30.0, 30.0),
            scale=0.05,
        )
        self.observations.policy.velocity_commands = None
        self.observations.policy.joint_pos = None
        self.observations.policy.joint_vel = None
        self.observations.policy.phase = ObsTerm(
            func=solar_mdp.phase_clock, params={"cycle_time": 0.65, "harmonics": 3}
        )
        self.observations.policy.enable_corruption = False

        self.actions.joint_pos.scale = 0.18
        self.commands.base_velocity.heading_command = False
        self.commands.base_velocity.rel_standing_envs = 0.0
        self.commands.base_velocity.ranges.lin_vel_x = (0.0, 0.0)
        self.commands.base_velocity.ranges.lin_vel_y = (0.0, 0.0)
        self.commands.base_velocity.ranges.ang_vel_z = (0.0, 0.0)
        self.commands.base_velocity.ranges.heading = None
        self.commands.base_velocity.debug_vis = False

        self.events.base_external_force_torque = None
        self.events.push_robot = None
        self.events.add_base_mass.params["mass_distribution_params"] = (-0.02, 0.04)

        self.rewards.track_lin_vel_xy_exp = RewTerm(
            func=solar_mdp.forward_velocity_b,
            weight=30.0,
            params={"max_reward": 2.0},
        )
        self.rewards.forward_speed_metric = RewTerm(func=solar_mdp.forward_velocity_b_unclipped, weight=0.01)
        self.rewards.track_ang_vel_z_exp = RewTerm(func=solar_mdp.yaw_rate_l2, weight=-0.08)
        self.rewards.lateral_velocity_l2 = RewTerm(func=solar_mdp.lateral_velocity_l2, weight=-0.20)
        self.rewards.reverse_velocity_l2 = RewTerm(func=solar_mdp.reverse_velocity_l2, weight=-1.0)
        self.rewards.lin_vel_z_l2.weight = -0.8
        self.rewards.ang_vel_xy_l2.weight = -0.08
        self.rewards.flat_orientation_l2.weight = -1.0
        self.rewards.feet_air_time.weight = 0.08
        self.rewards.dof_torques_l2.weight = -0.0001
        self.rewards.dof_acc_l2.weight = -1.0e-7
        self.rewards.action_rate_l2.weight = -0.004

        self.terminations.bad_orientation = DoneTerm(
            func=base_mdp.bad_orientation,
            params={"limit_angle": 1.0, "asset_cfg": SceneEntityCfg("robot")},
        )
        self.terminations.low_base = DoneTerm(
            func=base_mdp.root_height_below_minimum,
            params={"minimum_height": 0.055, "asset_cfg": SceneEntityCfg("robot")},
        )


@configclass
class SolarFlatImuSpeedEnvCfg_PLAY(SolarFlatImuSpeedEnvCfg):
    def __post_init__(self) -> None:
        super().__post_init__()

        self.scene.num_envs = 50
        self.scene.env_spacing = 2.0


@configclass
class SolarFlatSolarChargeEnvCfg(SolarFlatImuSpeedEnvCfg):
    def __post_init__(self) -> None:
        super().__post_init__()

        solar_params = {
            "sunny_voltage": 1.0,
            "dark_voltage": 0.15,
            "sun_center_x": 1.2,
            "sun_center_y": 0.0,
            "sun_radius": 0.75,
        }

        self.observations.policy.solar_voltage = ObsTerm(
            func=solar_mdp.solar_panel_voltage,
            params={"normalize_by": 1.0, **solar_params},
            clip=(0.0, 1.25),
        )

        self.actions.joint_pos.scale = 0.22

        self.rewards.track_lin_vel_xy_exp = RewTerm(
            func=solar_mdp.solar_search_velocity_reward,
            weight=4.0,
            params={"target_voltage": 0.80, "max_reward": 0.8, **solar_params},
        )
        self.rewards.forward_speed_metric = RewTerm(func=solar_mdp.forward_velocity_b_unclipped, weight=0.005)
        self.rewards.solar_charge = RewTerm(
            func=solar_mdp.solar_charge_reward,
            weight=6.0,
            params={"target_voltage": 0.80, **solar_params},
        )
        self.rewards.solar_rest = RewTerm(
            func=solar_mdp.solar_rest_reward,
            weight=10.0,
            params={"target_voltage": 0.80, "sit_height": 0.085, "height_tolerance": 0.035, **solar_params},
        )
        self.rewards.solar_charging_motion_l2 = RewTerm(
            func=solar_mdp.solar_charging_motion_l2,
            weight=-2.0,
            params={"target_voltage": 0.80, **solar_params},
        )
        self.rewards.joint_power_l1 = RewTerm(func=solar_mdp.joint_power_l1, weight=-0.002)
        self.rewards.track_ang_vel_z_exp = RewTerm(func=solar_mdp.yaw_rate_l2, weight=-0.04)
        self.rewards.lateral_velocity_l2.weight = -0.25
        self.rewards.reverse_velocity_l2.weight = -0.6
        self.rewards.feet_air_time.weight = 0.02
        self.rewards.dof_torques_l2.weight = -0.0004
        self.rewards.dof_acc_l2.weight = -2.0e-7
        self.rewards.action_rate_l2.weight = -0.012

        self.terminations.low_base = DoneTerm(
            func=base_mdp.root_height_below_minimum,
            params={"minimum_height": 0.045, "asset_cfg": SceneEntityCfg("robot")},
        )


@configclass
class SolarFlatSolarChargeEnvCfg_PLAY(SolarFlatSolarChargeEnvCfg):
    def __post_init__(self) -> None:
        super().__post_init__()

        self.scene.num_envs = 50
        self.scene.env_spacing = 2.0


@configclass
class SolarFlatSolarChargeNoImuEnvCfg(SolarFlatNoFeedbackEnvCfg):
    def __post_init__(self) -> None:
        super().__post_init__()

        solar_params = {
            "sunny_voltage": 1.0,
            "dark_voltage": 0.15,
            "sun_center_x": 1.2,
            "sun_center_y": 0.0,
            "sun_radius": 0.75,
        }

        self.episode_length_s = 12.0
        self.observations.policy.velocity_commands = None
        self.observations.policy.phase = ObsTerm(
            func=solar_mdp.phase_clock, params={"cycle_time": 0.75, "harmonics": 2}
        )
        self.observations.policy.solar_voltage = ObsTerm(
            func=solar_mdp.solar_panel_voltage,
            params={"normalize_by": 1.0, **solar_params},
            clip=(0.0, 1.25),
        )

        self.actions.joint_pos.scale = 0.18
        self.commands.base_velocity.heading_command = False
        self.commands.base_velocity.rel_standing_envs = 0.0
        self.commands.base_velocity.ranges.lin_vel_x = (0.0, 0.0)
        self.commands.base_velocity.ranges.lin_vel_y = (0.0, 0.0)
        self.commands.base_velocity.ranges.ang_vel_z = (0.0, 0.0)
        self.commands.base_velocity.ranges.heading = None
        self.commands.base_velocity.debug_vis = False

        # With no heading/IMU observation, start roughly facing the solar patch.
        self.events.reset_base.params["pose_range"] = {
            "x": (-0.15, 0.15),
            "y": (-0.20, 0.20),
            "yaw": (-0.35, 0.35),
        }

        self.rewards.track_lin_vel_xy_exp = RewTerm(
            func=solar_mdp.solar_search_velocity_reward,
            weight=5.0,
            params={"target_voltage": 0.85, "max_reward": 0.7, **solar_params},
        )
        self.rewards.forward_speed_metric = RewTerm(func=solar_mdp.forward_velocity_b_unclipped, weight=0.005)
        self.rewards.solar_charge = RewTerm(
            func=solar_mdp.solar_charge_reward,
            weight=7.0,
            params={"target_voltage": 0.85, **solar_params},
        )
        self.rewards.solar_rest = RewTerm(
            func=solar_mdp.solar_rest_reward,
            weight=8.0,
            params={"target_voltage": 0.85, "sit_height": 0.085, "height_tolerance": 0.035, **solar_params},
        )
        self.rewards.solar_charging_motion_l2 = RewTerm(
            func=solar_mdp.solar_charging_motion_l2,
            weight=-2.5,
            params={"target_voltage": 0.85, **solar_params},
        )
        self.rewards.joint_power_l1 = RewTerm(func=solar_mdp.joint_power_l1, weight=-0.002)
        self.rewards.track_ang_vel_z_exp = RewTerm(func=solar_mdp.yaw_rate_l2, weight=-0.04)
        self.rewards.lateral_velocity_l2 = RewTerm(func=solar_mdp.lateral_velocity_l2, weight=-0.20)
        self.rewards.reverse_velocity_l2 = RewTerm(func=solar_mdp.reverse_velocity_l2, weight=-0.8)
        self.rewards.feet_air_time.weight = 0.03
        self.rewards.dof_torques_l2.weight = -0.0004
        self.rewards.dof_acc_l2.weight = -2.0e-7
        self.rewards.action_rate_l2.weight = -0.010

        self.terminations.low_base = DoneTerm(
            func=base_mdp.root_height_below_minimum,
            params={"minimum_height": 0.045, "asset_cfg": SceneEntityCfg("robot")},
        )


@configclass
class SolarFlatSolarChargeNoImuEnvCfg_PLAY(SolarFlatSolarChargeNoImuEnvCfg):
    def __post_init__(self) -> None:
        super().__post_init__()

        self.scene.num_envs = 50
        self.scene.env_spacing = 2.0
