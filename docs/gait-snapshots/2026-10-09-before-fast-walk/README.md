# Before the faster walking trial

This snapshot preserves the first smooth-walk trial: 1.6 cycles/second,
40% swing / 60% stance, squared-sine lift, a 0.4-second start ramp, and a
400-degree/second walking interpolation limit. Default stride and lift are
30 degrees. Its source is in `solar_main.ino.txt` and `walking_gait.h.txt`.
The prior IMU headers, configuration and original gait are preserved in the
adjacent `2026-10-09-before-smooth-walk` snapshot.

The exact local OTA rollback binary is
`output/gait-snapshot-before-fast/firmware.bin`, with SHA-256 in `checksum.json`.
Upload it with motors off to restore this trial. The binary is ignored by Git.

The next trial changes cadence to 2.4 cycles/second (417 ms per cycle at
speed 1, versus 625 ms) and the walking interpolation ceiling to
600 degrees/second. This matches the 50% faster trajectory without changing
the standing pose, leg signs, stride, lift, support overlap, startup ramp,
wave, or Windows controls. Actual travel speed and battery clearance need
physical comparison; the faster cycle does not increase standing height.
