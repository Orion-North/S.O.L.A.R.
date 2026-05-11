from __future__ import annotations

import math
import time
from typing import Protocol

from .messages import Action, Observation


class Policy(Protocol):
    def reset(self) -> None:
        ...

    def act(self, observation: Observation) -> Action:
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


class HoldStillPolicy:
    def reset(self) -> None:
        pass

    def act(self, observation: Observation) -> Action:
        return Action(mode="stand")
