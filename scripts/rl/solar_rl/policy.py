from __future__ import annotations

import math
import time
from pathlib import Path
from typing import Protocol

from .messages import Action, Observation, RlServoAction


class Policy(Protocol):
    def reset(self) -> None:
        ...

    def act(self, observation: Observation) -> Action | RlServoAction:
        ...


class CautiousSearchPolicy:
    """Hardware-safe placeholder policy with the same API as a trained model."""

    def __init__(
        self,
        speed: float = 0.35,
        turn_rate: float = 0.35,
        forward_seconds: float = 4.0,
        turn_seconds: float = 1.2,
    ) -> None:
        self.speed = speed
        self.turn_rate = turn_rate
        self.forward_seconds = forward_seconds
        self.turn_seconds = turn_seconds
        self.started_at = time.monotonic()

    def reset(self) -> None:
        self.started_at = time.monotonic()

    def act(self, observation: Observation) -> Action:
        if observation.emergency_stop or not observation.torque_enabled:
            return Action(mode="stand", speed=self.speed)

        cycle = self.forward_seconds + self.turn_seconds
        phase = math.fmod(time.monotonic() - self.started_at, cycle)

        if phase < self.forward_seconds:
            return Action(mode="walk", vx=0.45, wz=0.0, speed=self.speed)

        return Action(mode="walk", vx=0.0, wz=self.turn_rate, speed=self.speed)


class SolarSeekingPolicy:
    """Search for brighter light using panel voltage as a simple reward signal."""

    def __init__(
        self,
        speed: float = 0.30,
        turn_rate: float = 0.30,
        sunny_voltage: float = 1.0,
        voltage_drop: float = 0.15,
        voltage_deadband: float = 0.03,
        forward_seconds: float = 3.0,
        turn_seconds: float = 1.4,
    ) -> None:
        self.speed = speed
        self.turn_rate = turn_rate
        self.sunny_voltage = sunny_voltage
        self.voltage_drop = voltage_drop
        self.voltage_deadband = voltage_deadband
        self.forward_seconds = forward_seconds
        self.turn_seconds = turn_seconds
        self.fallback = CautiousSearchPolicy(speed=speed, turn_rate=turn_rate)
        self.reset()

    def reset(self) -> None:
        self.started_at = time.monotonic()
        self.best_voltage: float | None = None
        self.smoothed_voltage: float | None = None
        self.last_voltage: float | None = None
        self.fallback.reset()

    def _update_voltage(self, voltage: float) -> float:
        if self.smoothed_voltage is None:
            self.smoothed_voltage = voltage
        else:
            self.smoothed_voltage = self.smoothed_voltage * 0.7 + voltage * 0.3

        if self.best_voltage is None or self.smoothed_voltage > self.best_voltage:
            self.best_voltage = self.smoothed_voltage
        return self.smoothed_voltage

    def act(self, observation: Observation) -> Action:
        if observation.emergency_stop or not observation.torque_enabled:
            return Action(mode="stand", speed=self.speed)

        voltage = observation.solar_voltage_v
        if voltage is None:
            return self.fallback.act(observation)

        smoothed = self._update_voltage(voltage)
        best = self.best_voltage if self.best_voltage is not None else smoothed
        dropped_from_best = smoothed < best - self.voltage_drop
        improving = self.last_voltage is None or smoothed >= self.last_voltage - self.voltage_deadband
        self.last_voltage = smoothed

        if smoothed >= self.sunny_voltage and not dropped_from_best:
            return Action(mode="stand", speed=self.speed)

        cycle = self.forward_seconds + self.turn_seconds
        phase = math.fmod(time.monotonic() - self.started_at, cycle)

        if improving and not dropped_from_best and phase < self.forward_seconds:
            return Action(mode="walk", vx=0.35, wz=0.0, speed=self.speed)

        turn_cycle = int((time.monotonic() - self.started_at) / cycle)
        turn_direction = 1.0 if turn_cycle % 2 == 0 else -1.0
        return Action(mode="walk", vx=0.0, wz=turn_direction * self.turn_rate, speed=self.speed)


class HoldStillPolicy:
    def reset(self) -> None:
        pass

    def act(self, observation: Observation) -> Action:
        return Action(mode="stand")


class RslRlImuSpeedPolicy:
    """Host-side loader for the Isaac Lab IMU speed policy."""

    actor_input_dim = 27
    actor_hidden_dims = (256, 128, 128)
    cycle_time_s = 0.65
    checkpoint_label = "IMU speed"
    run_name = "solar_flat_imu_speed"

    def __init__(self, checkpoint: str | Path | None = None, output_scale: float = 0.35) -> None:
        try:
            import torch
            from torch import nn
        except ImportError as exc:
            raise RuntimeError(
                f"The trained {self.checkpoint_label} RL policy requires PyTorch. "
                "Run this with external/IsaacLab/.venv/Scripts/python.exe "
                "or install torch in the active Python environment."
            ) from exc

        self.torch = torch
        self.nn = nn
        self.checkpoint = Path(checkpoint) if checkpoint else self._latest_checkpoint()
        self.output_scale = output_scale
        self.started_at = time.monotonic()
        self.prev_action = [0.0] * 12
        self.actor = self._load_actor(self.checkpoint)
        self.actor.eval()

    @classmethod
    def _latest_checkpoint(cls) -> Path:
        repo_root = Path(__file__).resolve().parents[3]
        run_root = repo_root / "external" / "IsaacLab" / "logs" / "rsl_rl" / cls.run_name
        checkpoints = sorted(run_root.glob("*/model_*.pt"), key=lambda path: path.stat().st_mtime)
        if not checkpoints:
            raise FileNotFoundError(f"No {cls.checkpoint_label} checkpoints found under {run_root}")
        return checkpoints[-1]

    def _load_actor(self, checkpoint: Path):
        if not checkpoint.exists():
            raise FileNotFoundError(checkpoint)
        data = self.torch.load(checkpoint, map_location="cpu")
        dims = [self.actor_input_dim, *self.actor_hidden_dims, 12]
        layers = []
        for index, (input_dim, output_dim) in enumerate(zip(dims, dims[1:])):
            layers.append(self.nn.Linear(input_dim, output_dim))
            if index < len(dims) - 2:
                layers.append(self.nn.ELU())
        actor = self.nn.Sequential(*layers)

        actor_state = {}
        linear_index = 0
        for module_index, module in enumerate(actor):
            if isinstance(module, self.nn.Linear):
                actor_state[f"{module_index}.weight"] = data["actor_state_dict"][f"mlp.{linear_index}.weight"]
                actor_state[f"{module_index}.bias"] = data["actor_state_dict"][f"mlp.{linear_index}.bias"]
                linear_index += 2
        actor.load_state_dict(actor_state)
        return actor

    def reset(self) -> None:
        self.started_at = time.monotonic()
        self.prev_action = [0.0] * 12

    @staticmethod
    def _clamp(value: float, low: float, high: float) -> float:
        return max(low, min(high, value))

    @staticmethod
    def _list3(value, fallback: tuple[float, float, float]) -> list[float]:
        if not isinstance(value, (list, tuple)) or len(value) < 3:
            return [fallback[0], fallback[1], fallback[2]]
        return [float(value[0]), float(value[1]), float(value[2])]

    def _build_observation(self, observation: Observation) -> list[float]:
        imu = observation.imu or {}
        accel_g = self._list3(imu.get("accel_g"), (0.0, 0.0, 1.0)) if imu.get("accel_ready", False) else [0.0, 0.0, 1.0]
        gyro_dps = self._list3(imu.get("gyro_dps"), (0.0, 0.0, 0.0)) if imu.get("gyro_ready", False) else [0.0, 0.0, 0.0]

        gyro = [
            self._clamp(math.radians(value), -8.0, 8.0) * 0.25
            for value in gyro_dps
        ]

        norm = math.sqrt(sum(value * value for value in accel_g)) or 1.0
        projected_gravity = [-(value / norm) for value in accel_g]

        accel_ms2 = [
            self._clamp(value * 9.80665, -30.0, 30.0) * 0.05
            for value in accel_g
        ]

        elapsed = time.monotonic() - self.started_at
        phase = (elapsed / self.cycle_time_s % 1.0) * (2.0 * math.pi)
        phase_terms: list[float] = []
        for harmonic in (1, 2, 3):
            harmonic_phase = phase * harmonic
            phase_terms.extend((math.sin(harmonic_phase), math.cos(harmonic_phase)))

        return [*gyro, *projected_gravity, *self.prev_action, *accel_ms2, *phase_terms]

    def act(self, observation: Observation) -> RlServoAction:
        if observation.emergency_stop or not observation.torque_enabled or observation.imu is None:
            self.prev_action = [0.0] * 12
            return RlServoAction(tuple(self.prev_action), output_scale=0.0)

        obs = self.torch.tensor(self._build_observation(observation), dtype=self.torch.float32).unsqueeze(0)
        with self.torch.no_grad():
            action = self.actor(obs).squeeze(0).tolist()
        self.prev_action = [self._clamp(float(value), -1.0, 1.0) for value in action]
        return RlServoAction(tuple(self.prev_action), output_scale=self.output_scale)


class RslRlSolarChargePolicy(RslRlImuSpeedPolicy):
    """Host-side loader for the solar-charge RL policy."""

    actor_input_dim = 28
    checkpoint_label = "solar charge"
    run_name = "solar_flat_solar_charge"

    def _build_observation(self, observation: Observation) -> list[float]:
        base_obs = super()._build_observation(observation)
        solar_voltage = observation.solar_voltage_v
        normalized_voltage = 0.0 if solar_voltage is None else self._clamp(solar_voltage, 0.0, 1.25)
        return [*base_obs, normalized_voltage]


class RslRlNoImuSolarChargePolicy(RslRlImuSpeedPolicy):
    """Host-side loader for the no-IMU solar-charge RL policy."""

    actor_input_dim = 17
    cycle_time_s = 0.75
    checkpoint_label = "no-IMU solar charge"
    run_name = "solar_flat_solar_charge_no_imu"

    def _build_observation(self, observation: Observation) -> list[float]:
        elapsed = time.monotonic() - self.started_at
        phase = (elapsed / self.cycle_time_s % 1.0) * (2.0 * math.pi)
        phase_terms: list[float] = []
        for harmonic in (1, 2):
            harmonic_phase = phase * harmonic
            phase_terms.extend((math.sin(harmonic_phase), math.cos(harmonic_phase)))

        solar_voltage = observation.solar_voltage_v
        normalized_voltage = 0.0 if solar_voltage is None else self._clamp(solar_voltage, 0.0, 1.25)
        return [*self.prev_action, *phase_terms, normalized_voltage]

    def act(self, observation: Observation) -> RlServoAction:
        if observation.emergency_stop or not observation.torque_enabled:
            self.prev_action = [0.0] * 12
            return RlServoAction(tuple(self.prev_action), output_scale=0.0)

        obs = self.torch.tensor(self._build_observation(observation), dtype=self.torch.float32).unsqueeze(0)
        with self.torch.no_grad():
            action = self.actor(obs).squeeze(0).tolist()
        self.prev_action = [self._clamp(float(value), -1.0, 1.0) for value in action]
        return RlServoAction(tuple(self.prev_action), output_scale=self.output_scale)
