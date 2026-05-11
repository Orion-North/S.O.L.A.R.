from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Any


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


@dataclass(frozen=True)
class Observation:
    status: dict[str, Any]
    image_jpeg: bytes | None = None
    captured_at: float = field(default_factory=time.time)
    status_latency_ms: float | None = None

    @property
    def mode(self) -> str:
        return str(self.status.get("mode", "unknown"))

    @property
    def emergency_stop(self) -> bool:
        return bool(self.status.get("emergency_stop", False))

    @property
    def torque_enabled(self) -> bool:
        return bool(self.status.get("torque_enabled", False))


@dataclass(frozen=True)
class Action:
    mode: str = "stand"
    vx: float = 0.0
    vy: float = 0.0
    wz: float = 0.0
    speed: float = 0.5
    stride: float = 24.0
    lift: float = 22.0

    def bounded(self) -> "Action":
        return Action(
            mode=self.mode,
            vx=clamp(self.vx, -1.0, 1.0),
            vy=clamp(self.vy, -1.0, 1.0),
            wz=clamp(self.wz, -1.0, 1.0),
            speed=clamp(self.speed, 0.1, 1.0),
            stride=clamp(self.stride, 0.0, 40.0),
            lift=clamp(self.lift, 0.0, 40.0),
        )

    def as_cmd_params(self) -> dict[str, str]:
        action = self.bounded()
        return {
            "mode": action.mode,
            "vx": f"{action.vx:.3f}",
            "vy": f"{action.vy:.3f}",
            "wz": f"{action.wz:.3f}",
            "speed": f"{action.speed:.3f}",
            "stride": f"{action.stride:.1f}",
            "lift": f"{action.lift:.1f}",
        }
