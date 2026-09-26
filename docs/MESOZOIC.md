# MESOZOIC — The Bare Brain (rate → wing-beat, continuously)

> Design for the CPU-economical PteronautOS stabilizer. One primitive integer
> PID per axis, running **continuously** (every loop tick), folding its output
> into the wing waveform. No AHRS, no lock-in, no feed-forward, no stroke-gate
> — only the haltere reflex.

---

## 1. The one idea

An ornithopter's wing **is** the actuator. There are no ailerons, no elevator,
no rudder surface — the wing *is* the surface. But the correction does **not**
need to wait for a stroke reversal: pitch (centre shift) is a pure offset that
takes effect instantly, and roll/yaw (amplitude/ferocity) bend the current
half-stroke toward a new reversal point. There is no hard "commitment" between
two reversals.

The mesozoic brain therefore runs its three PIDs **continuously** (every loop
tick) and folds the output into the waveform parameters each tick. The CPU
saving does **not** come from gating the PID to the stroke — that gate is what
breaks slow flapping and glide — it comes from *dropping Mahony* (§2). A rate
damper should be *faster* than the plant, not *locked* to it.

```
   gyro rate (raw)           PID (cheap, continuous)
   roll-rate  →  PD  →  differential flap AMPLITUDE   (L/R)
   pitch-rate →  PD  →  symmetric flap CENTRE shift
   yaw-rate   →  P   →  differential FEROCITY (drag damping)
                              │
                              ▼  applied every tick (folded into waveform)
                          →  the current half-stroke already bends
```

The PID is the **modulation**; the wing beat is the **carrier**. You do not
fight the beat — you retune it. That is "symphonizing the ancient wing beats."

---

## 1b. Slow flapping & glide — the degenerate regimes

The damper runs **every tick**, so it degrades gracefully, but the *actuator
mapping* must switch by regime — otherwise the correction pushes into a surface
that no longer exists:

| Regime | Roll | Pitch | Yaw |
|---|---|---|---|
| **Flapping** | diff. **amplitude** | sym. **centre** | diff. **ferocity** |
| **Glide** | diff. **centre** (aileron) | sym. **centre** (elevator) | *none* (no rudder) |

In glide the wings freeze into a fixed glider: there is no amplitude and no
ferocity to modulate, so roll must come from **differential incidence** (the
same channel that was "centre" in flapping, now split left/right) and pitch from
**symmetric incidence**. Yaw has no actuator in glide — set `wingYawGain = 0`
there; the pilot's tail/dihedral carries the turn.

The one stroke-*related* problem that *does* remain is **flap-induced body
rock**: wing inertia shakes the airframe at the flap frequency, polluting the
gyro *rate* signal (not just the accel attitude — the rate itself). Fix it with
a **notch/low-pass tracked to flap frequency**, engaged only while flapping. In
slow flap the notch widens and the floor rate drops naturally; in glide the
notch is off and the full-rate damper runs on the incidence surfaces.

---

## 2. Why rate-only (drop Mahony)

The expensive part of the current stack is **not** the PID — it is Mahony AHRS
(`Zephyrus.cpp:720`): quaternion normalisation, accelerometer fusion, and
`quatToEuler` (`atan2`/`acos`) every 4 ms tick.

A bird stabilises primarily through **rate damping**, not absolute attitude
levelling. Attitude comes from vision/vestibular; the haltere reflex (pure
angular velocity) is the common denominator of insect flight and is dirt cheap:

```
rate = (raw − bias) · scale      // three multiplies; no sinf, no atan2, no quaternion
```

Two further truths make rate-only the *honest* choice for a flapping bird:

1. **Accelerometer attitude is corrupt under flapping** — wing vibration is not
   the gravity vector, so the level reference (`_accelRefRoll/Pitch`) is noise
   fed straight into the angle PIDs.
2. **A rate damper is acro/horizon-free** — it kills the tumble but does not
   pretend to level (which would demand the very attitude signal that is
   corrupt). The pilot and the bird's own dihedral do the levelling; the brain
   only stops the fall.

So the brain is a **rate damper**, not a leveller. That is the mesozoic common
sense: damp the disturbance, don't chase a phantom horizon.

---

## 3. The minimal math (fixed-point, no transcendental)

Gyro scale is `131 LSB/(°/s)` (±250 dps, MPU6050 default — `ZEPHYR_GYRO_SCALE`,
`MUSHIN_GYRO_SCALE_LSB`). Everything stays integer. Three instances: roll,
pitch, yaw.

```cpp
struct MesoPid { int32_t i; int16_t lastErr, lastD; };

// err: rate LSB (target = 0). kp/kd/ki: Q12 gains. imax: integrator clamp.
// dtMs: loop period. Returns correction LSB (maps into the wing formulas below).
inline int16_t mesoPid(MesoPid &p, int16_t err,
                       int16_t kp, int16_t kd, int16_t ki,
                       int32_t imax, uint8_t dtMs)
{
    // derivative-on-measurement with a one-pole LPF (τ ≈ 8·dt)
    int32_t d   = (int32_t)(err - p.lastErr) / dtMs;
    int32_t lp  = (d * 8 + (int32_t)p.lastD * 5);
    p.lastD     = (int16_t)(lp >> 3);

    // integrator with anti-windup
    p.i += (int32_t)err * ki;
    if (p.i >  imax) p.i =  imax;
    if (p.i < -imax) p.i = -imax;
    p.lastErr = err;

    return (int16_t)(((int32_t)err * kp + p.i + (int32_t)p.lastD * kd) >> 12);
}
```

Cost per axis per tick: ~4 multiplies + 2 shifts. No `sinf`, no `cosf`, no
`atan2`, no float division. The output is consumed **every loop tick** and
folded into the waveform parameters — never gated to the stroke.

---

## 4. Exact code mapping — what stays, what goes

The mesozoic **output layer already exists** in `Ornithopter.cpp` and is wired
to `wingRollGain / wingPitchGain / wingYawGain` (0–100) and the `ZEPHYR_WING_*`
scales in `ZephyrusConfig.h:115-122`. We keep exactly this — and only this:

| Axis | Existing formula (keep) | Modulation |
|---|---|---|
| pitch | `gyroPitchCorrection · (wingPitchGain·0.01) · ZEPHYR_WING_PITCH_CENTER_SCALE` (`:337`) | symmetric centre shift (deg) |
| roll  | `gyroRollCorrection · (wingRollGain·0.01) · ZEPHYR_WING_ROLL_AMP_SCALE` (`:629`) | differential amplitude fraction |
| yaw   | `gyroYawCorrection · (wingYawGain·0.01) · ZEPHYR_WING_YAW_FER_SCALE` (`:536`) | differential ferocity |

**What is deleted (or gated out)** — the eight-storey ONDAS cathedral that the
mesozoic brain does not need:

| Component | Location | Verdict |
|---|---|---|
| Mahony AHRS + `quatToEuler` + accel fusion | `Zephyrus.cpp:472,720` | **drop** → raw rate + bias |
| Resonance lock-in `sinf(_osc.phase)` | `Ornithopter.cpp` (resonance) | drop |
| SSFF accumulation (`gyroPitchErrorRate`) | `Ornithopter.cpp:549-573` | drop |
| Cadence (P→phase advance), Balance (I→asymmetry) | ONDAS layer | drop |
| PD-blend ferocity (`ferocitySignal`) | ONDAS layer | drop |
| Aeroelastic gain scalar | ONDAS layer | drop |
| Anchor k₂ | ONDAS layer | drop |
| Slew, antigravity, elevator-rate transients | `Ornithopter.cpp` | drop |

The **source** of the three corrections changes: instead of
`ZephyrusFilter.h:34-36` copying Mahony-PID outputs, a tiny rate-only brain
produces them. The three wing formulas above are untouched — they are already
the right actuator mapping.

---

## 5. The strip-gate

Do **not** delete the ONDAS stack from history — it is the high ground and the
reference. Gate it behind a compile flag so the bare brain is the default and
the cathedral remains inspectable:

```
-DMESOZOIC_ONLY     # default on the ESP8285 PWM target
```

When `MESOZOIC_ONLY` is defined:
- `Zephyrus.cpp` compiles the raw-rate path + `mesoPid`, **not** Mahony.
- `ZephyrusFilter.h` bridges `rollRateCorr / pitchRateCorr / yawRateCorr`
  instead of the attitude PIDs + raw P/I/D terms.
- `Ornithopter.cpp` compiles out resonance, SSFF, aeroelastic, cadence/balance,
  anchor, slew, antigravity — leaving the three wing formulas + the pilot mix.

---

## 6. Shared canonical header (ESP8285 ⇄ RP2040)

The codebase already shares "canonical waveform mathematics" between the
ESP8285 spirit and the RP2040 muscle (`OrnithopterWaveform.h` →
`MotionWaveform.h`). The mesozoic brain follows the same pattern: one header
`Mesozoic.h` holding `MesoPid` + `mesoPid` + the three wing-formula scale
constants, included by **both** targets.

- **Standalone ESP8285 (PWMP7)**: local MPU6050 → raw rate → `mesoPid` → the
  three wing formulas → PWM. The gyro loop is where the CPU pain is.
- **RP2040 muscle (MUSHIN/EP2)**: gyro + servo live on the muscle, so the tight
  loop (rate → wing) lives there too; the ESP8285 remains the wave planner and
  only forwards the intent. This removes the UART telemetry latency from the
  control loop.

One canonical fixed-point math, two placements. No duplicated constants.

---

## 7. Definition of Done

1. `Mesozoic.h` exists: `MesoPid` + `mesoPid` + the three `ZEPHYR_WING_*` scale
   constants, integer-only, header-only, no float division.
2. `-DMESOZOIC_ONLY` on the ESP8285 PWM target strips Mahony + resonance + SSFF
   + aeroelastic + cadence/balance + anchor + slew/antigravity; the three wing
   formulas + pilot mix remain.
3. `ZephyrusFilter.h` bridges rate corrections (not attitude) under the flag.
4. **CPU budget**: gyro tick is raw-rate + 3× `mesoPid` — target < 2 µs/tick on
   ESP8285 @ 80 MHz (vs. the Mahony + 3-PID + slew path it replaces).
5. **Correctness probe**: inject a constant rate disturbance → the matching
   wing perturbation appears **within the same loop tick** (no stroke-gate);
   zero rate → zero perturbation (no wind-up, no drift). A separate probe
   confirms the flap-frequency notch only engages while flapping.
6. Native `pio test` target compiles `Mesozoic.h` + a rate-loop unit test
   (disturbance → per-tick output, wind-up clamp, NaN-free).
7. WebUI gains `wingRollGain / wingPitchGain / wingYawGain` keep working
   unchanged (0 disables an axis) — the pilot surface does not move.

---

## 8. The one sentence

*Take the gyro rate, damp it with three integer PIDs running every tick, and
fold the result into the wing waveform — the saving is rate-only (dropping
Mahony), never gating the PID to the stroke.*