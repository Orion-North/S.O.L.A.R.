# Walking gait snapshot

Saved before the smooth-walk trial on 2026-10-09. The original gait uses a
50% swing / 50% stance diagonal trot at 2 cycles per second at speed 1,
sinusoidal lift, 30-degree default stride and clearance. This snapshot also
preserves the working front-right wave with the 65-degree back-right brace.

`solar_main.ino.txt`, the two IMU headers, and `platformio.ini.txt` preserve
the firmware sources/configuration. Secrets are deliberately excluded.
`panel-main.js.txt` preserves the controls before forward/back became digital.

The exact pre-change OTA binary is saved locally at
`output/gait-snapshot-before-update/firmware.bin`, with its SHA-256 in
`checksum.json`. The output directory is ignored by Git. Upload that binary
with motors off to restore the previous firmware immediately. To rebuild,
copy the snapshot sources back to their matching paths, retaining the local
`secrets.h`, then run PlatformIO. Restoring firmware alone preserves the
new full-strength controller directions, which are in the Windows panel.

The trial keeps the same leg signs, peak stride/clearance, offsets and wave.
It uses 40% swing / 60% stance (20% of each cycle with all feet down), squared
sine lift with zero vertical velocity at contact, 1.6 cycles/second, and a
0.4-second amplitude ramp from stand. A boot-only mathematical check covers
trajectory continuity, amplitude bounds, reverse travel, phase wrapping,
and non-overlapping diagonal lift. Physical improvement needs a supervised
comparison on the same surface; successful compilation is not proof of
better walking or stability.
