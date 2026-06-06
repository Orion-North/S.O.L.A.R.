# S.O.L.A.R. IMU Speed Policy Protocol

This protocol matches the `Solar-Speed-Flat-IMU-v0` training setup. It assumes the robot has an IMU but no joint feedback.

## Control Rate

- Run policy inference at 50 Hz.
- Keep a monotonic phase clock using the controller's local time.
- Servo commands are position targets around the trained default pose.

## Policy Observation Order

The policy input has 27 values:

1. IMU angular velocity, body frame, rad/s: `gyro_x`, `gyro_y`, `gyro_z`, clipped to `[-8, 8]` and scaled by `0.25`.
2. IMU projected gravity vector, body frame: `gravity_x`, `gravity_y`, `gravity_z`.
3. Previous policy action: 12 normalized action values from the prior inference step.
4. IMU linear acceleration, body frame, m/s^2: `accel_x`, `accel_y`, `accel_z`, clipped to `[-30, 30]` and scaled by `0.05`.
5. Phase clock for a `0.65 s` gait cycle: `sin(p)`, `cos(p)`, `sin(2p)`, `cos(2p)`, `sin(3p)`, `cos(3p)`.

## Action Mapping

- Policy output is 12 normalized action values.
- Convert each action to a servo target by adding `0.18 rad * action` to the trained default joint pose.
- A zero action maps to the trained default stance, not to `90 deg` on every servo:
  hips `[-0.77, 0.77, 0.77, -0.77] rad` for `FL`, `FR`, `BL`, `BR`; thighs `1.03 rad`; calves `-1.0472 rad`.
- Clamp every servo target to the real mechanical range before sending it.
- Rate-limit servo target changes on hardware even if the policy asks for a jump.
- On this project, the host sends the 12 normalized values to firmware `/rl`.
- The firmware maps logical legs in order `FL`, `FR`, `BL`, `BR`, with each leg ordered `hip`, `knee`, `foot`.
- The firmware `scale` argument is a real-hardware safety multiplier. Start around `0.15`; `1.0` is the full trained action scale.

## Safety Rules

- Disable motion if roll or pitch exceeds 45 degrees.
- Disable motion if IMU acceleration spikes beyond a limit that indicates impact.
- Start with output scale below 1.0 on hardware, then ramp up after tethered tests.
- Do not use this policy on cluttered terrain; it was trained on flat ground.

## Current Best Checkpoint

Latest retuned validation run:

`external/IsaacLab/logs/rsl_rl/solar_flat_imu_speed/2026-05-13_17-10-21/model_699.pt`

Approximate final forward speed: `0.245 m/s`.

## Host Command

Dry-run:

```powershell
.\scripts\rl\run_imu_speed_policy.ps1 -RobotUrl http://solar.local -Duration 15
```

Tethered low-scale motion test:

```powershell
.\scripts\rl\run_imu_speed_policy.ps1 -RobotUrl http://solar.local -StartTorque -EnableMotion -Duration 10 -OutputScale 0.15 -EstopOnExit
```
