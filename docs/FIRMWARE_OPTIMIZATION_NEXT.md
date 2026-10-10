# Further SOLAR optimization review — 2026-10-10

This review covers the installed `rl-solar-web-ota-reset-http-fix-2026-10-10` firmware. It combines source inspection with read-only measurements on the robot. No movement commands, settings changes, firmware edits, or uploads were performed in this pass. The robot remained in stand with torque off.

## Measurements

Ten requests were sampled for each baseline endpoint. A separate four-second test ran deliberately paced camera and status requests concurrently. All 62 measured responses succeeded, including ten complete JPEGs. These are HTTP round-trip times over the current Wi-Fi connection, not measurements of firmware CPU time, gait frequency, maximum camera FPS, or emergency-stop latency.

| Measurement | Baseline | During camera requests |
| --- | ---: | ---: |
| Fast status median round-trip | 36.4 ms | 62.9 ms |
| Fast status slowest sampled response | 59.9 ms | 132.5 ms |
| IMU JSON median round-trip | 48.0 ms | Not measured |
| IMU binary median round-trip | 42.8 ms | Not measured |
| IMU JSON median body size | 505.5 bytes | — |
| IMU binary body size | 48 bytes | — |
| JPEG median round-trip | — | 118.9 ms |

Binary IMU bodies are about 90.5% smaller, but they omit JSON calibration details. Their sampled latency improvement was modest; packet size alone is not the main observed cost. Free heap was 162,656 bytes before and 162,032 afterward. That small short-term difference does not establish a leak. Response header counts remained bounded.

Raw measurements: `output/firmware-performance-review.json`. Measurement script: `output/review_live_performance.py`.

## Best next changes

1. **Remove repeated mode allocation with a small change.** `solar_main.ino:1416` constructs and destroys `String mode` every gait iteration. Move the snapshot String outside the loop and reserve enough capacity for the longest accepted mode. This preserves every mode comparison, trajectory, interpolation rate, and pulse conversion while avoiding repeated allocations. Reserving `cmd_mode` capacity at setup also avoids reallocations on mode transitions. An enum can follow later, but is a much broader edit.

2. **Bound and synchronize debug logging.** `solar_main.ino:649` appends recovery messages to `sysDebug` indefinitely from the IMU task. `/debug` copies it from the HTTP task while holding a lock that the writer does not take. A bounded log with its own synchronization prevents unbounded heap growth and concurrent String mutation. Capture a log snapshot under that lock, then format/send it after releasing it. Test repeated simulated recoveries and concurrent debug reads.

3. **Benchmark TCP response latency.** Small responses currently send the header and body separately (`solar_main.ino:730`, `:846`, `:897`). The installed WiFiServer defaults TCP_NODELAY to off; only the camera worker enables it. Try TCP_NODELAY for control/telemetry connections and compare equivalent before/after samples. The measured tens-of-milliseconds latency is consistent with several possible network costs; it does not prove Nagle is responsible. If response writes are combined, avoid adding large nested stack buffers. Keep authentication, CORS, content lengths, raw-header clearing, and `/ota` behavior intact.

4. **Reuse identical gait calculations.** `solar_main.ino:1617` calculates trigonometry independently for four legs even though there are only two diagonal phases. Compute cosine and swing lift once per phase and reuse the results, preserving the existing math types and operation order. Verify all twelve target angles against the current implementation over the entire phase range, forward/backward/turning commands, and allowed stride/lift values. This reduces repeated work without changing the gait schedule or servo pulse mapping.

5. **Use binary telemetry selectively in clients.** The existing binary endpoint saves about 90.5% of the IMU body bytes in this sample. Clients can use it for the live motion display and fetch JSON calibration metadata less often, while retaining fast JSON during calibration. This saves serialization and parsing work as well as bandwidth. Do not substitute binary blindly: it lacks the calibration-state fields and requires version-aware decoding.

6. **Shorten settings locks and avoid redundant NVS writes.** `solar_main.ino:2256` parses input and writes flash while holding the global motion lock. Validate first, identify values that actually changed, and persist outside the motion lock. Preserve save acknowledgement only on success. Require unique leg mappings and finite offsets. A versioned record would make multi-value saves more coherent, but needs explicit migration tests so existing calibration is preserved.

7. **Add measurements before changing timing again.** Status reports `gait_hz:50` as a constant (`solar_main.ino:2075`), despite the restored task sleeping after its work. Add actual cycle duration, worst-case lock wait, I2C error counts, stack high-water marks, and minimum/largest free heap block. Keep these low-rate diagnostics; do not insert serial printing or dynamic allocation into the gait loop. These measurements will show whether CPU, bus contention, Wi-Fi, or memory is the limiting factor.

## Reliability work to prioritize alongside optimization

- Phase resets and gait phase updates still span HTTP and gait tasks without consistent locking. Full target-frame commits with command generations would prevent old iterations from overwriting newer commands. This needs state-transition regression tests, rather than an incidental performance refactor.
- Charge-rest power-down still acts on a cached flag and start time (`solar_main.ino:1681`). Recheck the current state and operation generation before disabling torque, so a cancelled rest cannot turn off a later command.
- The global motion lock remains held through hardware-I2C writes. The installed default hardware transaction timeout is 50 ms. Instrument lock time and failures before selecting a shorter timeout or changing bus ownership.
- `/i2c` scans both buses synchronously across 126 addresses each. Cache diagnostics or restrict full scans to an explicit diagnostic operation; a disconnected bus can make this much more expensive than ordinary telemetry.
- Web OTA does not handle `UPLOAD_FILE_ABORTED` explicitly. Add an upload lifecycle state and cleanup so cancellation cannot leave misleading success flags or a pending Update session. Preserve the existing authenticated upload and dual-slot partition layout, and test a later successful upload after cancellation.
- Task/mutex creation and `pwm.begin()` are not checked before controls become available. Driver/task readiness should be observable, with torque requests rejected when actuator control is unavailable.

The preferred first implementation is mode-buffer reuse plus bounded synchronized logging, followed by an isolated TCP latency experiment. Leave the restored movement cadence, pulse conversion, exact settled-target writes, and web OTA partition layout intact.

## Implemented and deployed: 2026-10-10

Firmware `rl-solar-web-ota-bounded-log-2026-10-10` is running at the verified local address `192.168.1.91`. The gait task now reserves and reuses its mode snapshot String. Comparison against the pre-change source confirms the rest of the complete gait task is identical. Command mode capacity is also reserved once during setup.

The debug log now retains at most 2,048 bytes in fixed storage, dropping oldest bytes when full. A dedicated mutex serializes appends and snapshots; `/debug` snapshots the log before taking the motion state mutex. Snapshot capacity is reserved before locking. Mutex/allocation failures produce a diagnostic fallback. This adds roughly 2 KB of fixed storage in exchange for bounded log memory; it is not a claim of lower total RAM usage. Native tests exercise randomized truncation, oversized messages, 40,000 concurrent appends with snapshots, and failure handling.

TCP_NODELAY is enabled on accepted HTTP connections after `server.begin()`, which otherwise resets the setting. Camera transfer behavior, API formats, authentication, OTA handlers and dual-slot partition layout are preserved. The servo reset regression suite passes, including all sixteen calibrated neutral targets and retry after a complete failed-write cycle. No physical emote test was run; live checks kept motors off.

The 997,056-byte image was uploaded through authenticated web OTA and the running version was verified after reboot. The OTA page remained accessible after 45 mixed telemetry requests with the normal HTTP header limit. Another 100 `/debug` polls returned bounded, consistent boot logs. Previous deployed firmware remains in `SOLAR-WebOTA-Emote-Reset-Fix.bin`; the new image is `SOLAR-WebOTA-Optimized.bin`.

Equivalent paced samples immediately before/after this upload:

| Median round-trip | Before | After |
| --- | ---: | ---: |
| Fast status | 34.3 ms | 30.3 ms |
| IMU JSON | 35.6 ms | 33.6 ms |
| IMU binary | 35.6 ms | 26.9 ms |
| Camera JPEG | 79.5 ms | 74.2 ms |
| Fast status during camera | 42.0 ms | 32.8 ms |

All 80 before and 84 after requests succeeded; all 18/19 camera frames were valid JPEGs. Each idle endpoint has only ten samples, and Wi-Fi conditions vary: these results suggest a modest improvement but do not establish causality or worst-case latency. The comparison includes the small memory/logging changes, so it does not isolate TCP_NODELAY alone. After-upload heap was 161,132 bytes initially and 161,004 after the performance sample; this short check does not establish long-term heap stability. Data: `output/firmware-before-bounded-log-2026-10-10/performance.json`, `output/optimized-performance.json`, and `output/optimized-debug-poll-result.json`.
