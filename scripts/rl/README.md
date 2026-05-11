# S.O.L.A.R. Host Inference Starter

This folder is for later real-robot policy deployment from the host PC to the
ESP32 HTTP API. It is not the simulator training stack. Motion training lives in
`/simulation` and should come first.

The current loop:

1. Reads robot telemetry from `/status`.
2. Optionally reads camera frames from `/capture`.
3. Converts that into an observation object.
4. Asks a policy for the next action.
5. Sends bounded `/cmd` requests back to the robot.

The default policy is a cautious scripted motion policy. It is intentionally
not a trained RL model yet; it gives us the same interface a trained policy
will use later, while keeping early hardware tests predictable.

## Run In Dry-Run Mode

From this folder:

```powershell
python -m solar_rl.runner --robot-url http://solar.local --duration 15
```

Dry-run mode prints actions but does not send movement commands.

To collect early telemetry/action traces:

```powershell
python -m solar_rl.runner --robot-url http://solar.local --duration 30 --log-jsonl ..\..\output\rl-dry-run.jsonl
```

## Enable Motion

Only use this after the robot is on the floor, calibrated, powered safely, and
you are ready to hit emergency stop:

```powershell
python -m solar_rl.runner --robot-url http://solar.local --enable-motion --duration 30
```

If your firmware API token is enabled:

```powershell
python -m solar_rl.runner --robot-url http://solar.local --api-token YOUR_TOKEN --enable-motion
```

## Next RL Steps

- Use `--log-jsonl` runs to compare command timing, latency, and future reward
  signals before training a model.
- Train a motion policy in `/simulation`, then swap `CautiousSearchPolicy` for a model
  policy that returns the same `Action` object.
- Keep real-hardware inference bounded and watchdog-friendly. The ESP32
  already falls back if commands stop arriving, but the host should still send
  conservative commands.
- Add solar/battery observations later, after locomotion works.
