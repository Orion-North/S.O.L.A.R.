from pathlib import Path

import isaaclab.sim as sim_utils
from isaaclab.actuators import DCMotorCfg
from isaaclab.assets.articulation import ArticulationCfg


SOLAR_USD_PATH = Path(__file__).resolve().parents[4] / "assets" / "solar" / "solar.usd"


SOLAR_CFG = ArticulationCfg(
    spawn=sim_utils.UsdFileCfg(
        usd_path=str(SOLAR_USD_PATH),
        activate_contact_sensors=True,
        rigid_props=sim_utils.RigidBodyPropertiesCfg(
            disable_gravity=False,
            retain_accelerations=False,
            linear_damping=0.0,
            angular_damping=0.0,
            max_linear_velocity=1000.0,
            max_angular_velocity=1000.0,
            max_depenetration_velocity=1.0,
        ),
        articulation_props=sim_utils.ArticulationRootPropertiesCfg(
            enabled_self_collisions=False,
            solver_position_iteration_count=4,
            solver_velocity_iteration_count=0,
        ),
    ),
    init_state=ArticulationCfg.InitialStateCfg(
        pos=(0.0, 0.0, 0.14),
        joint_pos={
            "FL_hip_joint": -0.77,
            "FR_hip_joint": 0.77,
            "RL_hip_joint": 0.77,
            "RR_hip_joint": -0.77,
            ".*_thigh_joint": 1.03,
            ".*_calf_joint": -1.0472,
        },
        joint_vel={".*": 0.0},
    ),
    soft_joint_pos_limit_factor=0.9,
    actuators={
        "servos": DCMotorCfg(
            joint_names_expr=[".*_hip_joint", ".*_thigh_joint", ".*_calf_joint"],
            effort_limit=0.25,
            saturation_effort=0.25,
            velocity_limit=8.0,
            stiffness=25.0,
            damping=0.5,
            friction=0.01,
        ),
    },
)
