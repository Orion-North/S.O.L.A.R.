from __future__ import annotations

import argparse
import json
import os
import time
from pathlib import Path
from typing import Sequence

from .client import RobotClient, RobotHttpError
from .messages import Observation, RlServoAction
from .policy import (
    CautiousSearchPolicy,
    HoldStillPolicy,
    Policy,
    RslRlImuSpeedPolicy,
    RslRlNoImuSolarChargePolicy,
    RslRlSolarChargePolicy,
    SolarSeekingPolicy,
)


IMU_RL_POLICIES = {"imu-speed-rl", "solar-charge-imu-rl"}
SERVO_RL_POLICIES = {*IMU_RL_POLICIES, "solar-charge-rl"}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run off-board SOLAR policy inference.")
    parser.add_argument("--robot-url", default=os.getenv("SOLAR_ROBOT_URL", "http://solar.local"))
    parser.add_argument("--api-token", default=os.getenv("SOLAR_API_TOKEN", ""))
    parser.add_argument("--duration", type=float, default=30.0, help="Seconds to run. Use 0 for unlimited.")
    parser.add_argument("--rate-hz", type=float, default=0.0, help="Policy loop rate. Use 0 for policy default.")
    parser.add_argument("--timeout", type=float, default=2.0, help="HTTP timeout in seconds.")
    parser.add_argument(
        "--policy",
        choices=[
            "cautious-search",
            "hold-still",
            "solar-seek",
            "imu-speed-rl",
            "solar-charge-rl",
            "solar-charge-imu-rl",
        ],
        default="cautious-search",
    )
    parser.add_argument("--checkpoint", default="", help="Path to a trained RSL-RL checkpoint for trained policies.")
    parser.add_argument(
        "--solar-sunny-voltage",
        type=float,
        default=1.0,
        help="Panel voltage considered bright enough to stop searching.",
    )
    parser.add_argument(
        "--solar-drop-voltage",
        type=float,
        default=0.15,
        help="Voltage drop from recent best that triggers re-search.",
    )
    parser.add_argument("--rl-output-scale", type=float, default=0.35, help="Hardware safety scale for RL servo deltas.")
    parser.add_argument("--rl-max-tilt-deg", type=float, default=35.0, help="Stand down if abs(roll/pitch) exceeds this.")
    parser.add_argument("--rl-max-imu-age-ms", type=float, default=200.0, help="Stand down if IMU telemetry is stale.")
    parser.add_argument("--start-torque", action="store_true", help="Enable servo torque before running.")
    parser.add_argument("--with-camera", action="store_true", help="Attach JPEG frames to observations.")
    parser.add_argument("--enable-motion", action="store_true", help="Actually send movement requests.")
    parser.add_argument("--estop-on-exit", action="store_true", help="Send /estop when the runner exits.")
    parser.add_argument("--log-jsonl", default="", help="Append observation/action records to this JSONL file.")
    return parser


def make_policy(
    name: str,
    checkpoint: str = "",
    output_scale: float = 0.35,
    solar_sunny_voltage: float = 1.0,
    solar_drop_voltage: float = 0.15,
) -> Policy:
    if name == "hold-still":
        return HoldStillPolicy()
    if name == "solar-seek":
        return SolarSeekingPolicy(sunny_voltage=solar_sunny_voltage, voltage_drop=solar_drop_voltage)
    if name == "imu-speed-rl":
        return RslRlImuSpeedPolicy(checkpoint or None, output_scale=output_scale)
    if name == "solar-charge-rl":
        return RslRlNoImuSolarChargePolicy(checkpoint or None, output_scale=output_scale)
    if name == "solar-charge-imu-rl":
        return RslRlSolarChargePolicy(checkpoint or None, output_scale=output_scale)
    return CautiousSearchPolicy()


def observe(client: RobotClient, with_camera: bool, with_imu: bool) -> Observation:
    started = time.perf_counter()
    if with_imu:
        status, imu = client.observation_binary()
        latency_ms = (time.perf_counter() - started) * 1000.0
        imu_latency_ms = 0.0
    else:
        status = client.status(fast=True)
        latency_ms = (time.perf_counter() - started) * 1000.0
        imu = None
        imu_latency_ms = None

    image = None
    if with_camera:
        try:
            image = client.capture()
        except RobotHttpError as exc:
            if exc.status != 429:
                raise

    return Observation(
        status=status,
        imu=imu,
        image_jpeg=image,
        status_latency_ms=latency_ms,
        imu_latency_ms=imu_latency_ms,
    )


def log_event(log_file, observation: Observation, params: dict[str, str], motion_enabled: bool) -> None:
    if log_file is None:
        return

    record = {
        "captured_at": observation.captured_at,
        "status_latency_ms": observation.status_latency_ms,
        "imu_latency_ms": observation.imu_latency_ms,
        "status": observation.status,
        "imu": observation.imu,
        "action": params,
        "motion_enabled": motion_enabled,
        "image_jpeg_bytes": len(observation.image_jpeg or b""),
    }
    log_file.write(json.dumps(record, separators=(",", ":")) + "\n")
    log_file.flush()


def rl_safety_stop(
    observation: Observation, max_tilt_deg: float, max_imu_age_ms: float, require_imu: bool
) -> str | None:
    if observation.emergency_stop:
        return "estop"
    if not observation.torque_enabled:
        return "torque_off"
    if not require_imu:
        return None
    if observation.imu is None:
        return "imu_missing"
    if float(observation.imu.get("age_ms", 999999)) > max_imu_age_ms:
        return "imu_stale"
    if not observation.imu.get("gyro_ready", False):
        return "gyro_missing"
    if observation.imu.get("accel_ready", False):
        roll = abs(float(observation.imu.get("roll_deg", 0.0)))
        pitch = abs(float(observation.imu.get("pitch_deg", 0.0)))
        if max(roll, pitch) > max_tilt_deg:
            return "tilt"
    return None


def run(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.rate_hz == 0:
        args.rate_hz = 50.0 if args.policy in SERVO_RL_POLICIES else 2.0
    if args.rate_hz <= 0:
        raise SystemExit("--rate-hz must be greater than 0")

    client = RobotClient(args.robot_url, api_token=args.api_token, timeout=args.timeout)
    policy = make_policy(
        args.policy,
        checkpoint=args.checkpoint,
        output_scale=args.rl_output_scale,
        solar_sunny_voltage=args.solar_sunny_voltage,
        solar_drop_voltage=args.solar_drop_voltage,
    )
    policy.reset()
    log_file = None
    if args.log_jsonl:
        log_path = Path(args.log_jsonl)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_file = log_path.open("a", encoding="utf-8")

    interval = 1.0 / args.rate_hz
    deadline = None if args.duration == 0 else time.monotonic() + args.duration
    print(f"policy={args.policy} robot={client.base_url} rate_hz={args.rate_hz:g} motion={'on' if args.enable_motion else 'dry-run'}")

    try:
        client.ping()
        if args.start_torque:
            client.get_text("/torque", {"state": 1})
        while deadline is None or time.monotonic() < deadline:
            loop_started = time.monotonic()
            requires_imu = args.policy in IMU_RL_POLICIES
            observation = observe(client, args.with_camera, with_imu=requires_imu)
            action = policy.act(observation).bounded()

            safety_reason = None
            if isinstance(action, RlServoAction):
                safety_reason = rl_safety_stop(
                    observation, args.rl_max_tilt_deg, args.rl_max_imu_age_ms, require_imu=requires_imu
                )
                if safety_reason is not None:
                    action = RlServoAction(tuple(0.0 for _ in range(12)), output_scale=0.0)
                params = action.as_rl_params()
                print(
                    "mode=rl scale={scale} safety={safety} robot_mode={robot_mode} "
                    "status_ms={status_ms:.0f} imu_ms={imu_ms:.0f} solar_v={solar_v}".format(
                        scale=params["scale"],
                        safety=safety_reason or "ok",
                        robot_mode=observation.mode,
                        status_ms=observation.status_latency_ms or 0.0,
                        imu_ms=observation.imu_latency_ms or 0.0,
                        solar_v=f"{observation.solar_voltage_v:.3f}"
                        if observation.solar_voltage_v is not None
                        else "n/a",
                    )
                )
            else:
                params = action.as_cmd_params()
                print(
                    "mode={mode} vx={vx} wz={wz} speed={speed} solar_v={solar_v} "
                    "robot_mode={robot_mode} latency_ms={latency:.0f}".format(
                        mode=params["mode"],
                        vx=params["vx"],
                        wz=params["wz"],
                        speed=params["speed"],
                        solar_v=f"{observation.solar_voltage_v:.3f}" if observation.solar_voltage_v is not None else "n/a",
                        robot_mode=observation.mode,
                        latency=observation.status_latency_ms or 0.0,
                    )
                )

            log_event(log_file, observation, params, args.enable_motion)

            if args.enable_motion:
                if isinstance(action, RlServoAction):
                    if safety_reason is None:
                        client.rl(params)
                    elif safety_reason not in {"estop", "torque_off"}:
                        client.stand()
                else:
                    client.cmd(params)
            else:
                client.ping()

            elapsed = time.monotonic() - loop_started
            if elapsed < interval:
                time.sleep(interval - elapsed)
    except KeyboardInterrupt:
        print("interrupted")
    finally:
        try:
            if args.estop_on_exit:
                client.estop()
            elif args.enable_motion:
                try:
                    client.stand()
                except RobotHttpError as exc:
                    if exc.status not in (409, 423):
                        raise
        finally:
            if log_file is not None:
                log_file.close()

    return 0


def main() -> None:
    raise SystemExit(run())


if __name__ == "__main__":
    main()
