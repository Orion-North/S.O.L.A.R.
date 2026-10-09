## Variant: Modular Lab Studio // Engineering & Calibration Workbench

### Design Stance
A research workbench and developer IDE designed for gait tuning, inverse-kinematics debugging, and sensor calibration. Prioritizes fine-grained slider control over all 12 servos, real-time waveform oscilloscopes for attitude/torque data, and modular dockable cards.

### Key Choices
- **Layout**: 3-column lab console (Interactive joint angle & calibration sliders -> Primary optical stream + live IMU waveform oscilloscope -> Locomotion presets & diagnostic event bus).
- **Color**: Neutral obsidian slate (`#0b0d14`), elevated card surfaces (`#181d2a`), electric violet (`#8b5cf6`), telemetry cyan (`#06b6d4`), and crisp emerald (`#10b981`).
- **Typography**: Clean modern UI sans stack paired with monospace numbers for high tabular precision.
- **Interaction**: Live sliders for 12-DOF servo angles with instant degree readouts, real-time animated oscilloscope plotting pitch/roll phase waveforms, stance height adjustment, and detailed bus logs.

### Trade-offs
- **Strong at**: Servomotor calibration, zero-position trimming, gait tuning, diagnostics, and algorithm development.
- **Weak at**: Less visual reticle immersion than the Tactical HUD for fast-paced remote piloting.

### Best For
Bench testing, robotics research, kinematics tuning, servo zeroing, and hardware-in-the-loop debugging.
