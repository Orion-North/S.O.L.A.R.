## Variant: Restrained Blueprint // Aerospace Telemetry

### Design Stance
A precision ground-station telemetry console inspired by aerospace test benches, JPL telemetry monitors, and clean technical blueprints. Eliminates neon sci-fi clutter in favor of high-density technical clarity, leg contact phase awareness, and 12-DOF joint observability.

### Key Choices
- **Layout**: 3-column aerospace console (Telemetry & Pose -> Primary Optical Reticle & Sequencer -> Locomotion & 12-DOF Kinematic Array).
- **Color**: Deep slate obsidian background (`#090d13`), hairline grid (`#1e293b`), telemetry cyan (`#38bdf8`), radar emerald (`#10b981`), restrained safety amber (`#f59e0b`), and clear crimson (`#ef4444`) for emergency interlocks.
- **Typography**: Crisp monospace data labels (`JetBrains Mono` stack) paired with clean geometric hierarchy.
- **Interaction**: Live attitude artificial horizon, real-time foot contact phase indicator (FL/FR/RL/RR), interactive 12-joint position bars, guarded physical E-Stop, and WASD vector controls.

### Trade-offs
- **Strong at**: Deep technical visibility into robot physical dynamics (IMU angles, foot contact, servo load/angles, cadence tuning).
- **Weak at**: High data density might feel intimidating for casual joypad teleoperation.

### Best For
Laboratory testing, gait research, calibration sessions, and precision engineering runs.
