# Before the four-beat crawl trial

This snapshot preserves the fast diagonal trot: 2.4 cycles/second,
40% swing / 60% stance, squared-sine lift, a 0.4-second amplitude ramp,
30-degree default stride/lift, and a 600-degree/second walking interpolation
ceiling. Sources are `solar_main.ino.txt` and `walking_gait.h.txt`.

The exact local rollback binary is
`output/gait-snapshot-before-crawl/firmware.bin`, with SHA-256 in `checksum.json`.
Upload it with motors off to restore this version. The binary is ignored by Git.
Shared IMU sources/configuration remain preserved in the first gait snapshot.

The crawl trial lifts one leg at a time, in rear-left, front-left, rear-right,
front-right order. Its 22% swing / 78% stance duty leaves 12% of the cycle
with all feet commanded down. Each leg completes 1.2 cycles/second at speed
1, producing 4.8 individual steps/second. Peak lift is capped at 18 degrees;
stride, steering signs, standing pose, startup ramp and wave remain unchanged.
The boot math test checks all four legs for non-overlapping lift, continuity,
phase wrapping and amplitude bounds. These checks establish commanded
trajectories only: they do not measure actual foot contact, servo speed,
battery clearance or physical stability. Compare those on the same surface
and restore the snapshot if the crawl drags feet or works worse.

Trial result: the user reported very little actual movement. The crawl was
rejected and the exact fast-trot firmware above restored. Its commanded
trajectory checks did not establish useful propulsion on this robot.
