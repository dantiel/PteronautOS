# Zephyrus Gyro Module

**Crest Rudder Stabilization for PteronautOS** — an autonomous gyro-correction layer that dampens roll oscillations and yaw drift on servo-driven ornithopters using the onboard MPU6050 IMU. No external flight controller required.

---

## Architecture

```
┌──────────────┐    I²C (400kHz)    ┌───────────────────┐
│   MPU6050    │ ── raw accel/gyro ─→│  MPU6050 Driver   │
│   (0x68)     │ ←── register cfg  ──│  (register-level)  │
└──────────────┘                    └────────┬──────────┘
                                             │ int16 × 6
                                             ▼
                                    ┌───────────────────┐
                                    │  Bias Calibration │
                                    │  (200 samples,    │
                                    │   σ² < 250)       │
                                    └────────┬──────────┘
                                             │ float × 6 (°/s, g)
                                             ▼
               ┌─────────────────────────────────────────────┐
               │         Mahony AHRS Filter                  │
               │  Accel → quaternion correction (roll/pitch) │
               │  Gyro Z → raw yaw rate (drift-free)         │
               │  Kp=2.0, Ki=0.0 (yaw-rate-only)            │
               └─────────────────┬───────────────────────────┘
                                 │ float roll°, pitch°, yawRate °/s
                                 ▼
               ┌─────────────────────────────────────────────┐
               │          Dual PID Controllers               │
               │  Roll PID: setpoint=0°, Kp=1.2, Ki=0.05,    │
               │            Kd=0.15, Imax=±30°               │
               │  Yaw PID:  setpoint=0°/s, Kp=0.8, Ki=0.03,  │
               │            Kd=0.10, Imax=±20°/s             │
               │  Anti-windup + derivative-on-measurement    │
               └─────────────────┬───────────────────────────┘
                                 │ rollCorrection (+), yawCorrection (+)
                                 ▼
               ┌─────────────────────────────────────────────┐
               │         Crest Rudder Mixer                  │
               │  rudderCorrection =                          │
               │    rollCorrection × 2.5 µs/°                │
               │  + yawCorrection × 1.5 µs/(°/s)             │
               │    clamped to ±200 µs                       │
               └─────────────────┬───────────────────────────┘
                                 │ float gyroRudderCorrection (µs)
                                 ▼
               ┌─────────────────────────────────────────────┐
               │       Ornithopter._computeMixer()            │
               │  servoRudderUs = center + pilotMix           │
               │                 + gyroRudderCorrection       │
               └─────────────────────────────────────────────┘
```

### Key Design Decisions

| Decision | Rationale |
|---|---|
| **Independent from Ornithopter** | Zephyrus only shares a `float` field — no coupling, easy to disable/remove |
| **Gyro Z for yaw rate (not yaw angle)** | No magnetometer needed; yaw rate doesn't drift — only yaw angle would |
| **Filter Shim pattern** (`ZephyrusFilter.h`) | Same zero-overhead `#ifdef` guard as `OrnithopterFilter.h` — compiles to nothing without `-D ZEPHYRUS_ENABLED` |
| **Register-level MPU6050 driver** | Zero external dependencies — no MPU6050/AHRS/PID libraries |
| **Graceful degradation** | MPU6050 absent → `enabled=false`, all corrections stay zero, no crash |
| **Lazy begin** | `begin()` called on first `update()` — handles late I²C init gracefully |

---

## File Structure

```
lib/Zephyrus/
├── ZephyrusConfig.h      (54 lines)  Compile-time constants — I²C, MPU6050, AHRS, PID, mixer
├── Zephyrus.h            (85 lines)  Class declaration — public API, private state
├── Zephyrus.cpp          (526 lines) Full implementation — I²C driver, AHRS, PID, calibration
├── ZephyrusFilter.h       (35 lines) Bridge shim — drop-in integration for devServoOutput.cpp
└── README.md            (this file) Documentation
```

**Total:** ~700 lines of functional code, <200 bytes RAM footprint.

### Dependencies

- `Wire.h` (Arduino I²C library)
- `<cstdint>`
- `<Arduino.h>` (for `micros()`, `delay()`)

No external libraries. No MPU6050, AHRS, or PID libraries needed.

---

## Configuration Reference

All constants are in `ZephyrusConfig.h`. Tune these to match your ornithopter's dynamics.

### I²C Bus

| Constant | Default | Description |
|---|---|---|
| `ZEPHYR_I2C_SDA` | `4` | GPIO pin for I²C data (ESP8285) |
| `ZEPHYR_I2C_SCL` | `5` | GPIO pin for I²C clock (ESP8285) |
| `ZEPHYR_I2C_CLOCK` | `400000` | I²C bus speed (400 kHz fast mode) |
| `ZEPHYR_I2C_TIMEOUT_US` | `3000` | Read timeout in microseconds |
| `ZEPHYR_MPU_ADDR` | `0x68` | MPU6050 I²C address (AD0 low) |

### MPU6050 Sensor

| Constant | Default | Description |
|---|---|---|
| `ZEPHYR_ACCEL_SCALE` | `2` | Accelerometer range: 2, 4, 8, or 16g |
| `ZEPHYR_GYRO_SCALE` | `250` | Gyroscope range: 250, 500, 1000, or 2000 °/s |
| `ZEPHYR_DLPF_CFG` | `3` | DLPF bandwidth (0-6): 3 = 44 Hz accel, 42 Hz gyro |
| `ZEPHYR_SMPLRT_DIV` | `0` | Sample rate = 1 kHz / (1 + div) = 1 kHz |

### Mahony AHRS

| Constant | Default | Description |
|---|---|---|
| `ZEPHYR_MAHONY_KP` | `2.0` | Proportional gain — strength of accel correction |
| `ZEPHYR_MAHONY_KI` | `0.0` | Integral gain — set to zero for yaw-rate-only mode |

> **Note:** `Ki=0` disables gyro bias estimation — the yaw rate comes directly from gyro Z and doesn't drift over time. Only a yaw *angle* estimate would drift without magnetometer correction.

### PID Controllers

#### Roll PID (angle stabilization, setpoint = 0°)

| Constant | Default | Description |
|---|---|---|
| `ZEPHYR_PID_ROLL_KP` | `1.2` | Proportional gain |
| `ZEPHYR_PID_ROLL_KI` | `0.05` | Integral gain (eliminates steady-state error) |
| `ZEPHYR_PID_ROLL_KD` | `0.15` | Derivative gain (dampens oscillations) |
| `ZEPHYR_PID_ROLL_IMAX` | `30.0` | Integrator anti-windup clamp (±°) |

#### Yaw PID (rate dampening, setpoint = 0°/s)

| Constant | Default | Description |
|---|---|---|
| `ZEPHYR_PID_YAW_KP` | `0.8` | Proportional gain |
| `ZEPHYR_PID_YAW_KI` | `0.03` | Integral gain |
| `ZEPHYR_PID_YAW_KD` | `0.10` | Derivative gain |
| `ZEPHYR_PID_YAW_IMAX` | `20.0` | Integrator anti-windup clamp (±°/s) |

### Crest Rudder Mixer

| Constant | Default | Description |
|---|---|---|
| `ZEPHYR_RUDDER_ROLL_GAIN` | `2.5` | µs per degree of roll correction |
| `ZEPHYR_RUDDER_YAW_GAIN` | `1.5` | µs per °/s of yaw rate correction |
| `ZEPHYR_RUDDER_CLAMP_US` | `200` | Hard clamp on total gyro correction (±µs) |

### Calibration

| Constant | Default | Description |
|---|---|---|
| `ZEPHYR_CALIB_SAMPLES` | `200` | Minimum samples before calibration can complete |
| `ZEPHYR_CALIB_VARIANCE_MAX` | `250` | Reject axis if gyro variance exceeds this |
| `ZEPHYR_CALIB_STABLE_COUNT` | `20` | Consecutive stable samples required |
| `ZEPHYR_CALIB_MAX_SAMPLES` | `500` | Timeout — accept best-effort after this many |

---

## API Reference

### Class: `Zephyrus`

Singleton: `extern Zephyrus zephyrus;` — instantiated in `Zephyrus.cpp`.

### Public Fields (Read at any time)

| Field | Type | Description |
|---|---|---|
| `enabled` | `bool` | `true` if MPU6050 WHO_AM_I matched and module is active |
| `calibrated` | `bool` | `true` after gyro bias calibration completes successfully |
| `rollDeg` | `float` | Current roll angle in degrees (AHRS output) |
| `pitchDeg` | `float` | Current pitch angle in degrees (AHRS output) |
| `yawRate` | `float` | Current yaw rate in °/s (direct gyro Z, no integration) |
| `rollCorrection` | `float` | Roll PID output (magnitude, sign-based) |
| `yawCorrection` | `float` | Yaw rate PID output (magnitude, sign-based) |
| `rudderCorrection` | `float` | Combined crest rudder offset in µs, clamped to ±200 |

### Public Methods

#### `void begin()`

Initializes the I²C bus, probes the MPU6050 via its WHO_AM_I register, configures scale ranges, DLPF, and sample rate. If the MPU6050 is not detected, sets `enabled=false` and returns silently. Starts auto-calibration on success.

**Called from:** `zephyrusUpdate()` on first invocation (lazy begin).

#### `void update(uint32_t nowUs)`

Main tick function — called every loop iteration in `servosUpdate()`. Performs:

1. Lazy `begin()` if not yet initialized
2. Auto-calibration step (if calibrating, skips AHRS/PID)
3. Burst-read MPU6050 sensors (14-byte block)
4. Convert raw → physical units (°/s, g)
5. Run Mahony AHRS with measured dt
6. Extract roll/pitch from quaternion, yaw rate from gyro Z
7. Compute roll PID (setpoint 0°) + yaw PID (setpoint 0°/s)
8. Compute `rudderCorrection` = roll × `ROLL_GAIN` + yaw × `YAW_GAIN`
9. Clamp to ±`RUDDER_CLAMP_US`

**Called from:** `zephyrusUpdate()` → bridge shim → `devServoOutput.cpp`.

**Performance:** ~444 µs per call. Dominated by I²C burst read (~400 µs).

#### `void onLinkUp()`

Resets both PID integrators. Must be called when the radio link arms, to prevent integrator windup from the disarmed state.

**Called from:** `zephyrusOnLinkUp()` → `devServoOutput.cpp` on connection.

#### `void onLinkDown()`

Zeros all correction outputs, resets PID integrators. Must be called on disarm/failsafe to prevent stale corrections.

**Called from:** `zephyrusOnLinkDown()` → `devServoOutput.cpp` on failsafe.

---

## Integration

### Bridge Shim: `ZephyrusFilter.h`

Mirrors the `OrnithopterFilter.h` pattern exactly:

```cpp
#ifdef ZEPHYRUS_ENABLED
#include "Zephyrus.h"
#include "../Ornithopter/Ornithopter.h"

extern Zephyrus zephyrus;

static inline void zephyrusUpdate() {
    zephyrus.update(micros());
    ornithopter.gyroRudderCorrection = zephyrus.rudderCorrection;
}

static inline void zephyrusOnLinkUp()   { zephyrus.onLinkUp(); }
static inline void zephyrusOnLinkDown() { zephyrus.onLinkDown(); }

#else
static inline void zephyrusUpdate() {}
static inline void zephyrusOnLinkUp() {}
static inline void zephyrusOnLinkDown() {}
#endif
```

Without `-D ZEPHYRUS_ENABLED=1`, all functions compile to empty inlines — **zero overhead**.

### Integration Points in `devServoOutput.cpp`

| Line | Mechanism | Effect |
|---|---|---|
| 10 | `#include "../Zephyrus/ZephyrusFilter.h"` | Conditionally includes module |
| 230 | `zephyrusUpdate()` before `ornithopterUpdate()` | Sets `gyroRudderCorrection` before mixer runs |
| 160 | `zephyrusOnLinkDown()` in `servosEnterFailsafe()` | Zero corrections on failsafe |
| 357 | `zephyrusOnLinkUp()` in `event()` | Reset integrators on arm |

### Integration in `Ornithopter`

**`Ornithopter.h`** — guarded field:

```cpp
#ifdef ZEPHYRUS_ENABLED
    float gyroRudderCorrection;
#endif
```

**`Ornithopter.cpp`** — applied in `_computeMixer()`:

```cpp
servoRudderUs = (uint16_t)((float)ORNI_RUDDER_CENTER_US + rudderMix * 500.0f
#ifdef ZEPHYRUS_ENABLED
                           + gyroRudderCorrection
#endif
                          );
```

### Build Configuration

In `targets/pteronautos-rx.ini`:

```ini
build_flags =
    ...
    -D ZEPHYRUS_ENABLED=1
    -Ilib/Zephyrus
```

---

## Calibration

### Procedure (Automatic)

1. Power on the ornithopter and place it motionless on a flat surface
2. The module collects up to 200 gyro samples (≈200 ms at 1 kHz)
3. Each sample is checked for stability — if any axis changes >2°/s from the previous, the counter resets
4. After 20 consecutive stable samples AND ≥200 total samples:
   - Per-axis variance is computed: σ² = mean(x²) − mean(x)²
   - Axes with σ² < 250 are accepted; bias is set to the mean
   - Axes above the threshold keep bias at 0 (noisy axis, skip)
5. If 500 samples pass without stability, calibration aborts with a best-effort result

### Sign Convention

> ⚠️ **Must be verified in physical flight test.** The MPU6050 axis orientation relative to the ornithopter frame determines whether positive roll produces positive or negative rudder correction. If the rudder deflects in the wrong direction during flight, negate `ZEPHYR_RUDDER_ROLL_GAIN` and `ZEPHYR_RUDDER_YAW_GAIN`.

### Recalibration

The module calibrates once at power-on. To recalibrate, reboot the receiver. Calibration data is lost on power cycle — there is no persistent storage (by design: simplicity and RAM conservation).

---

## Tuning Guide

### General Approach

1. **Start with roll PID only** — set `ZEPHYR_PID_YAW_KP=0` and `ZEPHYR_RUDDER_YAW_GAIN=0`
2. **Hover test** — fly in still air, observe roll oscillations
3. **Tune Kp first** — increase until mild oscillation, then back off by 30%
4. **Add Kd** — increase until oscillation dampens, but before motor noise amplifies
5. **Add Ki** — small value to eliminate steady-state drift; keep `IMAX` tight
6. **Enable yaw PID** — repeat steps for yaw rate dampening
7. **Tune mixer gains** — adjust `ROLL_GAIN`/`YAW_GAIN` so correction is ~50-150 µs in normal flight

### Symptoms and Remedies

| Symptom | Likely Cause | Fix |
|---|---|---|
| Slow roll oscillation (1-2 Hz) | Kp too high or Kd too low | Reduce Kp by 20%, increase Kd by 50% |
| Fast jitter (10+ Hz) | Motor vibration in DLPF passband | Increase `ZEPHYR_DLPF_CFG` to 4 or 5 (lower cutoff) |
| Persistent roll offset | Ki too low or Imax too tight | Increase Ki to 0.08, Imax to 40 |
| Rudder twitches on throttle | I²C noise from motor PWM | Twist I²C wires, add 100nF cap at MPU6050 VCC |
| Correction opposite direction | MPU6050 orientation vs. ornithopter frame | Negate `ROLL_GAIN` and `YAW_GAIN` |
| Calibration never completes | Vibration during startup | Increase `CALIB_MAX_SAMPLES` or `CALIB_VARIANCE_MAX` |

---

## Performance

**Measured on ESP8285 (80 MHz):**

| Metric | Value |
|---|---|
| `update()` execution time | ~444 µs |
| CPU load at 50 Hz update rate | 2.2% |
| CPU load at 200 Hz update rate | 8.8% |
| I²C burst read (dominant cost) | ~400 µs |
| AHRS + PID computation | ~44 µs |
| RAM footprint (Zephyrus class) | ~200 bytes |
| Flash footprint (compiled) | ~1.5 KB |

**End-to-end latency** (gyro disturbance → rudder correction): 1-2 update cycles (10-40 ms depending on servo update rate).

---

## Security & Robustness

| Property | Mechanism |
|---|---|
| **No null pointer derefs** | All state is value types (stack/static) — no `new`, no `malloc` |
| **No buffer overflows** | Fixed-size buffers only (14-byte I²C, 3-element arrays) |
| **I²C timeout** | 3 ms timeout per read — prevents infinite hang on bus fault |
| **Graceful MPU6050 absence** | `_mpuInit()` returns false → `enabled=false` → all paths guarded |
| **Graceful I²C flight failure** | `_mpuReadSensors()` failure → correction decays at 0.9× per cycle |
| **Anti-windup PID** | Both integrators clamped to configurable `IMAX` |
| **Quaternion NaN guard** | Norm < 1e⁻⁶ → reset to identity quaternion |
| **Calibration timeout** | Max 500 samples — prevents infinite loop in vibrating environments |
| **dt clamping** | 0 < dt ≤ 100 ms — prevents integration blowup after stall |
| **#ifdef integration guards** | `ZEPHYRUS_ENABLED` + `ORNITHOPTER_MODE` — module compiles to zero without flags |

---

## Troubleshooting

### Module never activates (`enabled` stays `false`)

1. Check I²C wiring: SDA → GPIO4, SCL → GPIO5
2. Verify MPU6050 has 3.3V power and GND
3. Check MPU6050 AD0 pin is LOW (address 0x68)
4. Add 4.7kΩ pull-up resistors on SDA/SCL if missing

### Calibration never completes

1. Keep ornithopter absolutely still during power-on
2. If vibrating surface is unavoidable, increase `ZEPHYR_CALIB_MAX_SAMPLES` to 1000
3. Increase `ZEPHYR_CALIB_VARIANCE_MAX` to 500 for noisier sensors

### No visible rudder correction

1. Verify `gyroRudderCorrection` is non-zero (add debug logging in `zephyrusUpdate()`)
2. Check that `ZEPHYRUS_ENABLED=1` is in build flags
3. Verify MPU6050 orientation — tilting the ornithopter should change `rollDeg`

### Rudder jitter with throttle

1. Increase DLPF: set `ZEPHYR_DLPF_CFG` to 4 (21 Hz) or 5 (10 Hz)
2. Add a 100 nF capacitor across MPU6050 VCC and GND
3. Twist the I²C wires (SDA + SCL) to reduce EMI coupling

---

## Disabling the Module

To build without Zephyrus:

1. Remove `-D ZEPHYRUS_ENABLED=1` from `targets/pteronautos-rx.ini`
2. Optionally remove `-Ilib/Zephyrus`

All `zephyrusUpdate()`, `zephyrusOnLinkUp()`, and `zephyrusOnLinkDown()` calls compile to empty inlines. The `gyroRudderCorrection` field in Ornithopter is guarded by `#ifdef ZEPHYRUS_ENABLED`. Zero flash/RAM penalty.

---

## Credits

All names, structures, algorithms, and commentary are original work for PteronautOS. No external AHRS, MPU6050, or PID libraries were used — just register-level I²C via Arduino's `Wire.h`.

**Algorithm foundations:**
- **Mahony AHRS** — S. O. H. Madgwick, "An efficient orientation filter for inertial and inertial/magnetic sensor arrays" (2010)
- **PID anti-windup** — Clamping integrator with derivative-on-measurement (Åström & Hägglund)
- **MPU6050 register map** — InvenSense MPU-6000/MPU-6050 Product Specification Rev 3.4
