# Firmware regression tests

Run from the repository root on Windows with Visual Studio 2022 Build Tools:

```powershell
.\.venv-pio\Scripts\python.exe scripts/tests/test_servo_reset.py
cmd.exe /d /c output\run_servo_reset_test.cmd
```

The generator extracts the actual servo write and interpolation functions from
the current firmware and compiles them against a fake PWM driver. It checks
exact final pulses, deadband behavior, retry after I2C failure, calibrated
neutral recovery for all motors, and emergency-stop/torque-off inhibition.
Generated files remain under the ignored `output` directory. The test never
contacts or moves a robot and cannot verify mechanical servo positions.

Run the bounded debug-log tests with the same build tools:

```powershell
.\.venv-pio\Scripts\python.exe scripts/tests/test_debug_log.py
cmd.exe /d /c output\run_debug_log_test.cmd
```

These compile the actual log headers with host String and mutex adapters,
checking randomized truncation, oversized messages, concurrent appends and
snapshots, and safe handling of allocation failures. Generated files stay in
the ignored `output` directory.
