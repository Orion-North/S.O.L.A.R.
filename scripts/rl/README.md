# S.O.L.A.R. Host Inference Starter

This folder is for later real-robot policy deployment from the host PC to the
ESP32 HTTP API. It is not the simulator training stack. Motion training lives in
`/simulation` and should come first.

The current loop:

1. Reads robot telemetry from `/status`.
2. Optionally reads camera frames from `/capture`.
3. Converts that into an observation object.
4. Asks a policy for the next action.
5. Sends bounded `/cmd` requests back to the robot, or `/rl` servo-action requests for trained policies.

The default policy is a cautious scripted motion policy. It is intentionally
not a trained RL model yet; it gives us the same interface a trained policy
will use later, while keeping early hardware tests predictable.

The `solar-seek` policy is also scripted. It reads `solar_panel_voltage_v` from
`/status`, moves while the voltage is low, and stands when the panel voltage is
at or above `--solar-sunny-voltage`. If voltage drops from the recent best by
`--solar-drop-voltage`, it turns and searches again.

The trained IMU speed policy runs off-board on the host PC. The ESP32 should not
run the neural net; it should stream IMU data, accept bounded `/rl` action
packets, interpolate servo targets, and enforce watchdog/safety behavior.

The trained `solar-charge-rl` policy uses the IMU observation plus
`solar_panel_voltage_v`. It must be trained with
`simulation/train_solar_charge.ps1`; it is not compatible with the older
`solar_flat_imu_speed` checkpoints because the observation size is different.

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

To dry-run the solar-voltage seeking policy:

```powershell
python -m solar_rl.runner --robot-url http://solar.local --policy solar-seek --duration 30 --solar-sunny-voltage 1.0
```

To dry-run the latest trained IMU policy with real robot telemetry:

```powershell
.\run_imu_speed_policy.ps1 -RobotUrl http://solar.local -Duration 15 -LogJsonl ..\..\output\rl-imu-dry-run.jsonl
```

To dry-run the latest trained solar-charge policy:

```powershell
.\run_solar_charge_policy.ps1 -RobotUrl http://solar.local -Duration 15 -LogJsonl ..\..\output\rl-solar-charge-dry-run.jsonl
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

For the trained IMU policy, start tethered with a low output scale:

```powershell
.\run_imu_speed_policy.ps1 -RobotUrl http://solar.local -StartTorque -EnableMotion -Duration 10 -OutputScale 0.15 -EstopOnExit
```

For the trained solar-charge policy, use the same safety posture:

```powershell
.\run_solar_charge_policy.ps1 -RobotUrl http://solar.local -StartTorque -EnableMotion -Duration 10 -OutputScale 0.15 -EstopOnExit
```

Increase `--rl-output-scale` gradually only after the robot stays stable. The
trained scale is represented by `1.0`; real hardware should not start there.

## Solar Voltage Telemetry

The firmware exposes optional panel voltage in `/status` as
`solar_panel_voltage_v`. It is disabled by default. To enable it, copy
`firmware/solar_main/secrets.h.example` to `secrets.h` and set:

```cpp
#define SOLAR_PANEL_ADC_PIN -1  // replace -1 with your unused ADC1 pin
#define SOLAR_PANEL_VOLTAGE_DIVIDER 2.0f
```

Use a resistor divider so the ADC pin never sees more than 3.3 V, and set the
divider ratio to `panel_voltage / adc_voltage`. Prefer ADC1-capable pins; ADC2
pins are unreliable while Wi-Fi is active.

## Next RL Steps

- Use `--log-jsonl` runs to compare command timing, latency, and future reward
  signals before training a model.
- Train the solar-aware policy with `simulation/train_solar_charge.ps1`. Its
  reward includes simulated panel voltage, charging rest posture, movement
  penalties while charging, and joint-power penalties.
- Keep the neural policy on the host. The ESP32 receives normalized action
  packets through `/rl` and maps them to logical servo targets.
- Keep real-hardware inference bounded and watchdog-friendly. The ESP32
  already falls back if commands stop arriving, but the host should still send
  conservative commands.
- Log solar/battery observations and use them as future reward signals once
  locomotion is stable.
