from __future__ import annotations

import argparse
import statistics
import sys
import time
from pathlib import Path
from typing import Callable


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts" / "rl"))

from solar_rl.client import RobotClient, RobotHttpError  # noqa: E402


def p95(values: list[float]) -> float:
    if len(values) < 2:
        return values[0] if values else 0.0
    return statistics.quantiles(values, n=20)[18]


def time_call(fn: Callable[[], object]) -> float:
    started = time.perf_counter()
    fn()
    return (time.perf_counter() - started) * 1000.0


def summarize(label: str, values: list[float]) -> None:
    print(
        f"{label}: avg_ms={statistics.mean(values):.3f} "
        f"p95_ms={p95(values):.3f} min_ms={min(values):.3f} max_ms={max(values):.3f}"
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Benchmark SOLAR ESP32-compatible HTTP API latency.")
    parser.add_argument("--robot-url", default="http://127.0.0.1:8081")
    parser.add_argument("--api-token", default="")
    parser.add_argument("--loops", type=int, default=200)
    parser.add_argument("--timeout", type=float, default=2.0)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.loops <= 0:
        raise SystemExit("--loops must be greater than 0")

    client = RobotClient(args.robot_url, api_token=args.api_token, timeout=args.timeout)
    try:
        client.ping()
        try:
            client.get_text("/torque", {"state": 1})
        except RobotHttpError as exc:
            if exc.status not in (403, 423):
                raise

        status_fast: list[float] = []
        status_full: list[float] = []
        obs_bin: list[float] = []
        rl_cmd: list[float] = []
        action_params = {"a": ",".join("0" for _ in range(12)), "scale": "0.2"}

        for _ in range(args.loops):
            status_fast.append(time_call(lambda: client.status(fast=True)))
            status_full.append(time_call(lambda: client.status(fast=False)))
            obs_bin.append(time_call(client.observation_binary))
            rl_cmd.append(time_call(lambda: client.rl(action_params)))

        print(f"target={client.base_url} loops={args.loops}")
        summarize("status_fast", status_fast)
        summarize("status_full", status_full)
        summarize("obs_binary", obs_bin)
        summarize("rl_command", rl_cmd)
        summarize("combined_hot_path", obs_bin + rl_cmd)
    finally:
        client.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
