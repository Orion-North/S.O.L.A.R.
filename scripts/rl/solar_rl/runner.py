from __future__ import annotations

import argparse
import json
import os
import time
from pathlib import Path
from typing import Sequence

from .client import RobotClient, RobotHttpError
from .messages import Observation
from .policy import CautiousSearchPolicy, HoldStillPolicy, Policy


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run off-board SOLAR policy inference.")
    parser.add_argument("--robot-url", default=os.getenv("SOLAR_ROBOT_URL", "http://solar.local"))
    parser.add_argument("--api-token", default=os.getenv("SOLAR_API_TOKEN", ""))
    parser.add_argument("--duration", type=float, default=30.0, help="Seconds to run. Use 0 for unlimited.")
    parser.add_argument("--rate-hz", type=float, default=2.0, help="Policy loop rate.")
    parser.add_argument("--timeout", type=float, default=2.0, help="HTTP timeout in seconds.")
    parser.add_argument("--policy", choices=["cautious-search", "hold-still"], default="cautious-search")
    parser.add_argument("--with-camera", action="store_true", help="Attach JPEG frames to observations.")
    parser.add_argument("--enable-motion", action="store_true", help="Actually send /cmd movement requests.")
    parser.add_argument("--estop-on-exit", action="store_true", help="Send /estop when the runner exits.")
    parser.add_argument("--log-jsonl", default="", help="Append observation/action records to this JSONL file.")
    return parser


def make_policy(name: str) -> Policy:
    if name == "hold-still":
        return HoldStillPolicy()
    return CautiousSearchPolicy()


def observe(client: RobotClient, with_camera: bool) -> Observation:
    started = time.perf_counter()
    status = client.status()
    latency_ms = (time.perf_counter() - started) * 1000.0

    image = None
    if with_camera:
        try:
            image = client.capture()
        except RobotHttpError as exc:
            if exc.status != 429:
                raise

    return Observation(status=status, image_jpeg=image, status_latency_ms=latency_ms)


def log_event(log_file, observation: Observation, params: dict[str, str], motion_enabled: bool) -> None:
    if log_file is None:
        return

    record = {
        "captured_at": observation.captured_at,
        "status_latency_ms": observation.status_latency_ms,
        "status": observation.status,
        "action": params,
        "motion_enabled": motion_enabled,
        "image_jpeg_bytes": len(observation.image_jpeg or b""),
    }
    log_file.write(json.dumps(record, separators=(",", ":")) + "\n")
    log_file.flush()


def run(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.rate_hz <= 0:
        raise SystemExit("--rate-hz must be greater than 0")

    client = RobotClient(args.robot_url, api_token=args.api_token, timeout=args.timeout)
    policy = make_policy(args.policy)
    policy.reset()
    log_file = None
    if args.log_jsonl:
        log_path = Path(args.log_jsonl)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_file = log_path.open("a", encoding="utf-8")

    interval = 1.0 / args.rate_hz
    deadline = None if args.duration == 0 else time.monotonic() + args.duration
    print(f"policy={args.policy} robot={client.base_url} motion={'on' if args.enable_motion else 'dry-run'}")

    try:
        client.ping()
        while deadline is None or time.monotonic() < deadline:
            loop_started = time.monotonic()
            observation = observe(client, args.with_camera)
            action = policy.act(observation).bounded()
            params = action.as_cmd_params()

            print(
                "mode={mode} vx={vx} wz={wz} speed={speed} "
                "robot_mode={robot_mode} latency_ms={latency:.0f}".format(
                    mode=params["mode"],
                    vx=params["vx"],
                    wz=params["wz"],
                    speed=params["speed"],
                    robot_mode=observation.mode,
                    latency=observation.status_latency_ms or 0.0,
                )
            )

            log_event(log_file, observation, params, args.enable_motion)

            if args.enable_motion:
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
                client.stand()
        finally:
            if log_file is not None:
                log_file.close()

    return 0


def main() -> None:
    raise SystemExit(run())


if __name__ == "__main__":
    main()
