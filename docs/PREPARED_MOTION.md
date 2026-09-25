# EP2-prepared motion / RP2040 execution

This is an early-alpha replacement for the reduced MUSHIN v1 flight path.
Update both firmwares together. Runtime and flashing still share one UART;
see [EP2 wiring](EP2_COMPANION.md).

## Ownership

The EP2 runs the pilot mixer, arming logic, profile selection, throttle/frequency
coupling, thrust expo, transient stick-rate effects, both wings' ferocity and
amplitude differences, glide/flap offsets, calibration, and gearbox mixing.
It prepares dwell boundaries, reciprocal ramp lengths, pointed-table coordinates,
and the shared asymmetric reversal boundary. In a MUSHIN build it does **not**
advance the wing oscillator or sample the waveform. No waveform tables are
needed by the EP2's live flight path.

The RP2040 owns the only running wingbeat clock. Core 0 parses the shared UART,
assembles recipes, handles USB and samples the local gyro. Core 1 evaluates the
prepared curves and applies the existing local rudder correction independently
of core-0 activity. PWM hardware generates the pulses. The two cores exchange
one immutable snapshot through a release/acquire mailbox; there is no queue of
future strokes and neither core waits for a packet or mailbox slot.

The standalone firmware and companion use the same canonical waveform tables
and mathematics in `sketches/yoshimitsu/src/MotionWaveform*`. The prepared evaluator
is tested against the original shape evaluator, including extreme asymmetric
ferocity, independent skew, plateau and rounded-pyramidal blends.

## Timing and wire budget

- One 126-byte recipe is explicitly encoded little-endian (including float32),
  split into three private CRSF `0x81` frames. Total wire size: 147 bytes.
- At 420000 baud / 8N1 this takes 3.5 ms. A 5 ms publication ceiling allows at
  most 200 recipes/s (29.4 kB/s); the EP2's 3 ms preparation tick normally yields
  at most about 167/s. Whole-frame backpressure may reduce this further.
- Linked operation does not also transmit redundant raw RC frames. Before
  linking, raw RC remains available for explicitly selected MUSHIN-OFF use.
- Partial recipes never become active. Generation, order, length, 30 ms assembly
  timeout and finite/range checks are verified before publication.
- Core 1 targets one motion evaluation per 1 ms, based on elapsed local time.
  Receiving a new recipe changes motion parameters, not phase. Missing packets
  do not pause the oscillator. The USB `STATUS` command reports maximum worker
  computation time and missed deadlines for bench measurements.
- Core 0 schedules the existing yaw sensor acquisition at 1 ms intervals using
  400 kHz I2C and a 2 ms transaction timeout. Other consumers use the cached
  measurement instead of starting extra sensor transactions.

These are configured rates, **not measured worst-case hardware guarantees**.
Two cores still share memory/flash. Do not write EEPROM/flash while flying;
the persistent MUSHIN mode toggle is rejected in KINCHO/MANJI and requires a
bench stance such as NSS or MEDITATION.

## Servo output and safety

RP2040 uses hardware PWM, replacing the installed Servo library's fixed 50 Hz
refresh. `YOSHI_SERVO_HZ` defaults to 333, matching the standalone PteronautOS
setting; configure 50–333 Hz for the actual servo's specified input interface.
The 0.042 s/60° transit rating alone does not establish supported input frequency.
Both GPIOs belonging to a PWM slice share its frequency. Do not share these
slices with unrelated PWM users.

Calibration retains integer-degree mapping and the 500–2500 µs absolute envelope.
Prepared centres/amplitudes may differ by one degree at floating-point truncation
boundaries because their algebra is factored; bench-check calibration before flight.
The complete four-wing and four-output gearbox maps are available on RP2040;
configure enough physical servo pins for the selected model. Recipes requiring
unconfigured actuator indices are rejected rather than silently truncated.

EP2 stops renewing recipes when RC data becomes older than 100 ms, on RF/model
loss or when the mixer is disabled (explicit virtual-stick bench mode is separate).
RP2040 enforces its own 100 ms recipe timeout even if core 0 stalls. STOP and
stance changes invalidate pending recipes. Wings/surfaces centre at 1500 µs;
identified motor outputs idle at 1000 µs. Companion mode does not automatically
fall back to the unrelated raw-RC servo mixer. Mode changes and reflashing are
bench operations, not in-flight controls.

## Deliberate limits / remaining work

This implements **pilot-waveform offload**, not full Zephyrus migration. The
existing Yoshimitsu gyro implementation is a local yaw correction, not the
standalone Mahony AHRS, full PID system, phase-debt harmonizer, resonance or SSFF.
Those need a separately tested port and their own configuration contract. No
gyro stream is sent to the EP2 for real-time processing.

No new mechanical speed/acceleration limiter is silently inserted. Servo-speed
amplitude preparation matches the existing model; extreme waveforms may still
be physically impossible or undersampled. The old simplified v1 executor remains
in source for regression tests, but the EP2 flight sender no longer selects it.
ESP32-S3 uses cooperative prepared evaluation for now, not this RP2040 worker.

## Verification

Run host parity, preparer, transport, core-worker and regression tests:

```sh
bash tools/test_prepared_motion.sh
```

Build the RP2040 example against the repository library:

```sh
arduino-cli compile --fqbn rp2040:rp2040:rpipico \
  --library sketches/yoshimitsu sketches/yoshimitsu/examples/Yoshimitsu_Default
```

Before flight, use a logic analyser to measure PWM period/pulse widths, both
wings' timing, deadline misses, and stopping under RC/UART loss and I2C faults.
Repeat under maximum waveform load and USB traffic. Host tests and successful
compilation cannot establish actual gyro bandwidth, mechanical response or
flight safety.
