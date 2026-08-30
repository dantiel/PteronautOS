# Stabilization System — Ondas, SSFF, Aeroelastic PID & Waveform

> **Magnum Opus Complete** — Zephyrus→Ornithopter raw P/I/D term exposure, cadence/ferocity/balance
> modulation, trapezoidal wave shaping with PD-blend, stroke-synchronous feed-forward (SSFF),
> anchor damping (k₂), phase-locked resonance, and aeroelastic PID gain modulation for
> PteronautOS biomechanical ornithopters.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Data Flow: Zephyrus → Ornithopter Bridge](#2-data-flow)
3. [Ondas Modulation — Cadence, Ferocity, Balance](#3-ondas-modulation)
4. [Trapezoidal Wave Shaping & PD-Blend](#4-trapezoidal-wave-shaping)
5. [Anchor Damping (k₂)](#5-anchor-damping)
6. [Resonance — Phase-Locked Lock-In Amplifier](#6-resonance)
7. [SSFF — Stroke-Synchronous Feed-Forward](#7-ssff)
8. [Aeroelastic PID Gain Modulation](#8-aeroelastic-pid)
9. [Gearbox Kernel Stabilization](#9-gearbox-kernel)
10. [WebUI — Stabilization Panel](#10-webui)
11. [Configuration Endpoints](#11-configuration-endpoints)
12. [Compile-Time Constants](#12-compile-time-constants)
13. [Tuning Guide](#13-tuning-guide)
14. [Build Targets](#14-build-targets)
15. [Known Limitations](#15-known-limitations)
16. [File Map](#16-file-map)

---

## 1. Architecture Overview

```
┌──────────────┐                         ┌──────────────────────────┐
│  Zephyrus   │   raw pitch P/I/D       │      Ornithopter          │
│  MPU6050+AHRS│ ──── pitchPTerm ──────→ │  _computeServoMixer()     │
│  Pitch PID   │ ──── pitchITerm ──────→ │                           │
│              │ ──── pitchDTerm ──────→ │  Cadence P→Phase Advance→ kGainMod   (osc.)
│              │ ──── pitchErrorRate ──→ │  Ferocity PD→Dwell ────→ ferocitySignal
│              │                         │  Balance I→Asymmetry ──→ iBias      (stroke)
│              │                         │  SSFF ─────────────────→ stroke bias (next half-stroke)
│              │                         │  Resonance ────────────→ _resonanceAccum
│              │  aeroGainScale          │  Aeroelastic ──────────→ gain scalar (glide/flap)
│              │ ←────────────────────── │  Anchor k₂ ────────────→ _osc.anchorGain
└──────────────┘                         └──────────────────────────┘
```

The stabilization system operates in the **flapping waveform kernel** (`_computeServoMixer()`)
and **gearbox kernel** (`_computeGearboxMixer()`). All modulation is guarded by
`#ifdef ZEPHYRUS_ENABLED` — when Zephyrus is absent from the build, modulation compiles
to zero-cost no-ops.

### Signal Chain

```
MPU6050 raw data
    → Mahony AHRS (quaternion → roll°, pitch°, yawRate °/s)
    → Pitch PID controller (setpoint 0°, derivative-on-measurement)
    → 4 raw terms exposed: P, I, D, errorRate
    → ZephyrusFilter.h bridges to ornithopter.gyroPitch*
    → _computeServoMixer() applies three modulation paths:
        1. Ondas P → oscillator spring constant (phase advance/retard)
        2. Ondas D+I → ferocity modulation + stroke asymmetry bias
        3. SSFF → half-stroke error accumulation → next stroke bias
    → Aeroelastic gain scalar applies glide/flap coefficient to all paths
```

---

## 2. Data Flow — Zephyrus → Ornithopter Bridge

### Step 1: Raw Term Exposure (Zephyrus.cpp)

After the pitch PID computes correction in `Zephyrus::update()`, four raw terms are stored:

```cpp
// Zephyrus.cpp:746-749 — Nigredo P/I/D exposure
pitchPTerm    = ZEPHYR_PID_PITCH_KP * pitchErr;    // Raw proportional
pitchITerm    = _pidPitch.integrator;                // Accumulated error
pitchDTerm    = ZEPHYR_PID_PITCH_KD * _pidPitch.lastDerivative;  // Low-pass filtered D
pitchErrorRate = _pidPitch.lastDerivative;           // °/s — for SSFF accumulation
```

These are public fields on the `Zephyrus` class (`Zephyrus.h:33-37`).

### Step 2: Bridge Shim (ZephyrusFilter.h)

Each `zephyrusUpdate()` call (invoked at 250Hz from `devServoOutput.cpp`) copies the raw
terms directly onto `ornithopter`:

```cpp
// ZephyrusFilter.h:31-34
ornithopter.gyroPitchPTerm     = zephyrus.pitchPTerm;
ornithopter.gyroPitchITerm     = zephyrus.pitchITerm;
ornithopter.gyroPitchDTerm     = zephyrus.pitchDTerm;
ornithopter.gyroPitchErrorRate = zephyrus.pitchErrorRate;
```

### Step 3: Ornithopter Consumption (Ornithopter.h)

Seven fields receive the bridge data (`Ornithopter.h:52-56`):

| Field | Type | Zephyrus Source | Role in Modulation |
|-------|------|----------------|-------------------|
| `gyroPitchPTerm` | float | `pitchPTerm` | Ondas P → Phase Advance |
| `gyroPitchITerm` | float | `pitchITerm` | Ondas I → Asymmetry Bias |
| `gyroPitchDTerm` | float | `pitchDTerm` | Ondas D → Ferocity Modulation |
| `gyroPitchErrorRate` | float | `pitchErrorRate` | SSFF accumulation |
| `gyroRudderCorrection` | float | `rudderCorrection` | Crest rudder µs offset |
| `gyroAileronCorrection` | float | `rollCorrection` | Gearbox: roll PID → leg ailerons |
| `gyroElevatorCorrection` | float | `pitchCorrection` | Gearbox: pitch PID → leg elevons |

---

## 3. Ondas Modulation — Three-Channel Breath-Pause

*Ondas* (Portuguese: "waves") is a three-channel modulation system that maps the Zephyrus
pitch PID's raw P, I, and D terms onto three orthogonal dimensions of the flapping waveform.
It creates a proprioceptive feedback loop where the gyro's sense of pitch directly shapes
the wing motion.

### 3.1 Cadence — P→Phase Advance (`kGainMod`, was `ondasGain`)

**What it does:** Compresses or decompresses the current stroke phase based on the pitch
proportional term. A positive pitch error (nose up) advances the phase, effectively
shortening the current half-stroke. A negative error (nose down) retards the phase.

**Location:** `Ornithopter.cpp`

```cpp
_osc.kGainMod = 1.0f + gyroPitchPTerm * aeroGainScale * cadenceGain * 0.00005f;
if (_osc.kGainMod < 0.5f) _osc.kGainMod = 0.5f;
if (_osc.kGainMod > 2.0f) _osc.kGainMod = 2.0f;
```

**Clamping:** `[0.5, 2.0]` — phase can be at most doubled or halved.

**Tuning range:** 0–100 (WebUI slider), default 20.

### 3.2 Ferocity — PD-Blend Dwell Ratio (`ferocitySignal`, was `ondasGain2`/`dMod`)

**What it does:** Blends P-term and D-term into a ferocity signal that controls the
trapezoidal waveform dwell ratio. Replaces the old D-only `dMod` additive bias.

**Location:** `Ornithopter.cpp`

```cpp
float ferocitySignal = (gyroPitchPTerm * ferocityPGain * 0.00015f
                      + gyroPitchDTerm * ferocityDGain * 0.0003f) * aeroGainScale;
if (ferocitySignal > 0.5f) ferocitySignal = 0.5f;
if (ferocitySignal < -0.5f) ferocitySignal = -0.5f;
```

**Clamping:** `[-0.5, 0.5]` — ferocity signal added to both stroke and return ferocity.

**Tuning range:** Ferocity D 0–100 (default 20), Ferocity P 0–100 (default 0).

### 3.3 Balance — I→Asymmetry (`iBias`, was `ondasGain3`)

**What it does:** Shifts the stroke center bias based on the accumulated pitch integral
term. A positive I term biases the stroke downward, negative biases upward.

**Location:** `Ornithopter.cpp`

```cpp
float iBias = gyroPitchITerm * aeroGainScale * balanceGain * 0.0001f;
if (iBias > 3.0f) iBias = 3.0f;
if (iBias < -3.0f) iBias = -3.0f;
```

**Clamping:** `[-3.0, 3.0]` ferocity units. **Tuning range:** 0–100 (default 10).

### 3.4 Ferocity Application

```cpp
float strokeFer = _crsfToFloat(voiceStrokeFerocity, ORNI_FEROCITY_MIN, ORNI_FEROCITY_MAX)
                  + dMod + iBias + _ssffFerocityUpBias;
float returnFer = _crsfToFloat(voiceReturnFerocity, ORNI_FEROCITY_MIN, ORNI_FEROCITY_MAX)
                  + dMod - iBias + _ssffFerocityDownBias;
```

The pilot's ferocity commands (CRSF channels 6 and 7) provide the baseline. Ondas D+I
and SSFF biases are additive offsets — the pilot always has authority over ferocity range
`[1.0, 8.0]`.

---

## 4. Trapezoidal Wave Shaping & PD-Blend

**What it does:** Replaces the old tanh-based waveform shaping with a trapezoidal
dwell + cos ramp from OrniFlight (`pid.c:688-776`). The waveform has a flat dwell
region at ±1.0 amplitude, connected by cosine ramps.

### 4.1 Algorithm

**Location:** `OrnithopterWaveform.h` — `FlappingOscillator::shapeWave()`

```
f ∈ [1.0, 8.0], d = f/8.0, dh = d/2
if |rawSin| ≥ (1-dh):
    return ±1.0           // flat dwell at full amplitude
else:
    t = |rawSin|/(1-d)    // map [0, 1-d] → [0, 1]
    ramp = cos((1-t)*π)   // cos ramp: cos(π)→cos(0) = −1→+1
    return sign·ramp
```

When ferocity is low (f≈1), d≈0.125 — nearly sinusoidal with minimal dwell.
When ferocity is high (f≈8), d=1.0 — full square wave with maximum dwell.

### 4.2 PD-Blend Ferocity Signal

Instead of the old D-only `dMod` additive bias, the new system blends P and D PID
terms into a unified ferocity signal:

```cpp
ferocitySignal = (gyroPitchPTerm * ferocityPGain * 0.00015f
                + gyroPitchDTerm * ferocityDGain * 0.0003f) * aeroGainScale;
```

The blended signal is clamped to [-0.5, 0.5] and added to both stroke and return
ferocity. When `ferocityPGain = 0` (default), the behavior is D-only — backward
compatible with the old `ondasGain2`/`dMod` path.

---

## 5. Anchor Damping (k₂)

**What it does:** Provides variable oscillator damping. The old code used a hardcoded
`constexpr float kDamp = 10.0f`. The new system exposes this as a tunable parameter:
`k₂ = 10.0 + anchorGain`. At `anchorGain = 0`, damping is tight (k₂=10, rapid
convergence to target cadence). At higher values, damping increases — up to k₂=110
at anchorGain=100.

**Location:** `OrnithopterWaveform.h` — `FlappingOscillator::advance()`

```cpp
_osc.anchorGain = anchorGain;
// In advance():
float kDamp = 10.0f + anchorGain;
float error = kGain * kGainMod * cadenceTarget - kDamp * cadence;
```

**Tuning range:** 0–100 (WebUI), default 0 (k₂=10 tight). Higher = tighter lock
on target cadence, lower responsiveness to perturbation.

---

## 6. Resonance — Phase-Locked Lock-In Amplifier

**What it does:** A lock-in amplifier that accumulates pitch error rate multiplied by
`sin(oscillator phase)`, creating a phase-synchronous pump that reinforces or dampens
the stroke at exactly the right moment. The accumulator has a leaky decay with
τ = 0.15 seconds.

**Location:** `Ornithopter.cpp` — after `shapeWave()` calls

```cpp
if (resonanceGain > 0.0f) {
    _resonanceAccum += gyroPitchErrorRate * sinf(_osc.phase) * resonanceGain * 0.01f * dt;
    _resonanceAccum *= expf(-dt / 0.15f);   // leaky integrator τ=0.15s
    clamp(_resonanceAccum, -2.0f, 2.0f);
}
```

The accumulated value is injected into `strokeFer` and `returnFer` for the next frame.
When `resonanceGain = 0` (default), resonance is disabled entirely.

**Theory:** The product `errorRate × sin(phase)` is maximal when the pitch error
derivative is in phase with the wing motion — this is exactly when a correction
is most effective. The leaky integrator prevents runaway accumulation.

---

## 7. SSFF — Stroke-Synchronous Feed-Forward

**What it does:** SSFF accumulates the pitch error rate (`pitchErrorRate` in °/s) over
each half-stroke. At the zero-crossing of the flapping sine wave (when the wings reverse
direction), it computes the mean error and applies a bias to the **next** half-stroke's
ferocity.

This is fundamentally different from PID — it's feed-forward, correcting the *next*
stroke based on what happened during the *current* stroke.

### 4.1 Zero-Cross Detection

**Location:** `Ornithopter.cpp:161-163`

```cpp
if ((_prevFlappingSin >= 0.0f && rawWave < 0.0f) ||
    (_prevFlappingSin < 0.0f && rawWave >= 0.0f)) {
```

Detects both upward (positive→negative) and downward (negative→positive) zero-crossings.
Each zero-cross marks the end of one half-stroke and the beginning of the next.

### 4.2 Bias Computation

**Location:** `Ornithopter.cpp:164-173`

```cpp
if (_ssffAccumCount > 0 && ssffGain > 0.0f) {
    float meanError = _ssffAccumError / (float)_ssffAccumCount;
    float bias = meanError * ssffGain * 0.00001f;
    if (bias > 2.0f) bias = 2.0f;
    if (bias < -2.0f) bias = -2.0f;
    if (rawWave >= 0.0f) { _ssffFerocityDownBias = bias; }
    else                { _ssffFerocityUpBias   = bias; }
} else if (ssffGain <= 0.0f) {
    // Purificatio: zero stale biases when SSFF disabled
    _ssffFerocityUpBias   = 0.0f;
    _ssffFerocityDownBias = 0.0f;
}
```

**Key behaviors:**
- Bias is clamped to `[-2.0, 2.0]` ferocity units
- **Up-bias** (`_ssffFerocityUpBias`): applied during downstroke (corrects for error
  accumulated during the previous upstroke)
- **Down-bias** (`_ssffFerocityDownBias`): applied during upstroke (corrects for error
  accumulated during the previous downstroke)
- When `ssffGain == 0`, both biases are zeroed — the Purificatio fix prevents stale
  biases from persisting when SSFF is disabled via WebUI

### 4.3 Accumulation

```cpp
_ssffAccumError += gyroPitchErrorRate;
_ssffAccumCount++;
```

Every call to `_computeServoMixer()` accumulates the current `pitchErrorRate`. The
accumulator resets at each zero-cross, starting fresh for the next half-stroke.

**Overflow safety:** At 250Hz update rate, a 32-bit int accumulator would require
~24 days of continuous flapping to overflow — not a practical concern.

---

## 8. Aeroelastic PID Gain Modulation

**What it does:** Dynamically scales all Ondas and SSFF modulation by a coefficient
that depends on whether the ornithopter is flapping or gliding.

### 5.1 Servo Kernel (Waveform)

**Location:** `Ornithopter.cpp:128-130`

```cpp
#ifdef ZEPHYRUS_ENABLED
    aeroGainScale = isFlapping ? (aeroFlapCoeff * 0.01f) : (aeroGlideCoeff * 0.01f);
#endif
```

**Hysteresis:** Flapping detection uses hysteresis (`ORNI_FLAP_HYSTERESIS_US = 50`)
to prevent rapid toggling when throttle hovers near the threshold.

**Effect:** Every Ondas term (`kGainMod`, `dMod`, `iBias`) is multiplied by
`aeroGainScale`, making the modulation 0–1× effective based on the 0–100 coefficient.

### 5.2 Gearbox Kernel

**Location:** `Ornithopter.cpp:267-269`

```cpp
bool motorRunning = armed && (throttleNorm > 0.1f);
aeroGainScale = motorRunning ? (aeroFlapCoeff * 0.01f) : (aeroGlideCoeff * 0.01f);
```

In the gearbox kernel, `aeroGainScale` is applied to the elevator PID correction
via `ZephyrusFilter.h:43`:

```cpp
ornithopter.gyroElevatorCorrection = zephyrus.pitchCorrection
    * ZEPHYR_GEARBOX_PITCH_GAIN * ornithopter.aeroGainScale;
```

**One-frame lag:** `aeroGainScale` is computed in `_computeGearboxMixer()` but applied
in `zephyrusUpdate()` which runs *before* the gearbox mixer. This means the gearbox
elevator correction uses the previous frame's gain scale — a 4ms lag at 250Hz. This is
negligible for mechanical servos (τ ≫ 4ms).

### 5.3 Default Values

| Coefficient | Default | Range | When Active |
|-------------|---------|-------|-------------|
| `aeroGlideCoeff` | 40 | 0–100 | Wings still (glide / motor off) |
| `aeroFlapCoeff` | 40 | 0–100 | Wings flapping (motor running) |

---

## 9. Gearbox Kernel Stabilization

The gearbox kernel (`_computeGearboxMixer()`) applies Zephyrus PID corrections
differently from the servo waveform kernel:

### Servo Kernel
- **Rudder only:** `rudderCorrection = roll + yaw` combined → crest rudder
- **Ondas/SSFF:** Full waveform modulation on flapping wings

### Gearbox Kernel
- **Rudder:** `rudderCorrection = yaw` only (roll/pitch go to leg servos)
- **Leg Ailerons (V-tail):** Roll PID → `gyroAileronCorrection` applied to V-tail
  surfaces with elevon mixing
- **Leg Elevons (V-tail/elevator):** Pitch PID → `gyroElevatorCorrection` ×
  `aeroGainScale` applied to V-tail/elevator surfaces
- **Traditional tail:** Separate elevator servo receives pitch PID directly

PID corrections are clamped to `±ZEPHYR_GEARBOX_CLAMP_US` (250µs) before mixing.

---

## 10. WebUI — Stabilization Panel

The ornithopter panel (`ornithopter-panel.lithaml`, `ornithopter-panel.coffee`)
includes two stabilization sections, conditionally visible when Zephyrus is enabled:

### 7.1 Ondas Modulation Section

```
┌─ Stabilization — Ondas Modulation ──────────────────────┐
│ Ondas P→Phase Advance  [========|============] 20       │
│   Pitch P-term compresses/decompresses stroke phase     │
│ Ondas D→Ferocity       [========|============] 20       │
│   Pitch D-term sharpens/flattens waveform               │
│ Ondas I→Asymmetry      [====|================] 10       │
│   Pitch I-term shifts stroke center bias                │
│ SSFF Gain              [|====================] 0        │
│   Stroke-synchronous feed-forward (0=disabled)          │
└─────────────────────────────────────────────────────────┘
```

### 7.2 Aeroelastic PID Section

```
┌─ Aeroelastic PID Gain Modulation ───────────────────────┐
│ Glide Coefficient  [============|============] 40 %     │
│   Pitch PID gain scale during glide (wings still)       │
│ Flap Coefficient   [============|============] 40 %     │
│   Pitch PID gain scale during active flapping           │
└─────────────────────────────────────────────────────────┘
```

### 7.3 i18n Keys

15 new i18n keys added in `src/html/src/locales/en.js`:

| Key | Label |
|-----|-------|
| `ornithopter.stabilization.title` | Stabilization — Ondas Modulation |
| `ornithopter.stabilization.desc` | Zephyrus gyro-driven waveform modulation... |
| `ornithopter.stabilization.cadence_gain` | Cadence — P→Phase Advance |
| `ornithopter.stabilization.ferocity_d_gain` | Ferocity D — D→Dwell |
| `ornithopter.stabilization.ferocity_p_gain` | Ferocity P — P→Dwell |
| `ornithopter.stabilization.balance_gain` | Balance — I→Asymmetry |
| `ornithopter.stabilization.anchor_gain` | Anchor — k₂ Damping |
| `ornithopter.stabilization.resonance_gain` | Resonance — Phase Lock |
| `ornithopter.stabilization.ssff_gain` | SSFF Gain |
| `ornithopter.stabilization.ssff_off` | (disabled) |
| `ornithopter.aeroelastic.title` | Aeroelastic PID Gain Modulation |
| `ornithopter.aeroelastic.desc` | Dynamically scales Zephyrus pitch PID response... |
| `ornithopter.aeroelastic.glide_coeff` | Glide Coefficient |
| `ornithopter.aeroelastic.flap_coeff` | Flap Coefficient |

### 7.4 Config Persistence

Values are saved via POST to `/pteronautos/config` with the `_saveConfig()` method:

```coffeescript
_saveConfig: ->
  body = JSON.stringify {
    # ... existing fields ...
    cadence_gain: @cadenceGain
    ferocity_d_gain: @ferocityDGain
    balance_gain: @balanceGain
    ferocity_p_gain: @ferocityPGain
    anchor_gain: @anchorGain
    resonance_gain: @resonanceGain
    ssff_gain: @ssffGain
    aero_glide_coeff: @aeroGlideCoeff
    aero_flap_coeff: @aeroFlapCoeff
  }
  fetch '/pteronautos/config', ...
```

---

## 11. Configuration Endpoints

### GET `/pteronautos/state`

Returns stabilization fields in the `ornithopter` JSON block (when Zephyrus enabled):

```json
{
  "ornithopter": {
    "cadence_gain": 20.0,
    "ferocity_d_gain": 20.0,
    "balance_gain": 10.0,
    "ferocity_p_gain": 0.0,
    "anchor_gain": 0.0,
    "resonance_gain": 0.0,
    "ssff_gain": 0.0,
    "aero_glide_coeff": 40.0,
    "aero_flap_coeff": 40.0,
    "aero_gain_scale": 0.4,
    "gyro_rudder_correction_us": -12.5,
    "gyro_aileron_correction_us": 0.0,
    "gyro_elevator_correction_us": 3.2
  }
}
```

`aero_gain_scale` is the **computed** value (read-only).

### POST `/pteronautos/config`

Accepts partial updates — only provided fields are changed:

```json
{
  "cadence_gain": 30,
  "ferocity_d_gain": 15,
  "anchor_gain": 5
}
```

**Purificatio clamping:** All fields clamped to `[0.0, 100.0]` server-side:

```cpp
// devWIFI.cpp
if (json["cadence_gain"].is<float>())
    ornithopter.cadenceGain = std::max(0.0f, std::min(100.0f, (float)json["cadence_gain"]));
```

---

## 12. Compile-Time Constants

All in `OrnithopterConfig.h`:

| Constant | Default | Description |
|----------|---------|-------------|
| `ORNI_CADENCE_GAIN` | 20 | P→Phase Advance default |
| `ORNI_FEROCITY_D_GAIN` | 20 | D→Dwell (PD-blend) default |
| `ORNI_BALANCE_GAIN` | 10 | I→Asymmetry default |
| `ORNI_FEROCITY_P_GAIN` | 0 | P→Dwell (PD-blend) default |
| `ORNI_ANCHOR_GAIN` | 0 | k₂ damping delta default |
| `ORNI_RESONANCE_GAIN` | 0 | Phase-lock resonance default |
| `ORNI_SSFF_GAIN` | 0 | SSFF disabled by default |
| `ORNI_AERO_GLIDE_COEFF` | 40 | Glide gain modulation default |
| `ORNI_AERO_FLAP_COEFF` | 40 | Flap gain modulation default |

All are initialized in the `Ornithopter` constructor from these `#defines`.

---

## 13. Tuning Guide

### Startup Sequence

1. **Set all gains to 0** — verify the ornithopter flies stably with manual control only
2. **Enable Zephyrus** — verify crest rudder stabilization works (roll+yaw PID → rudder)
3. **Dial in Cadence (P→Phase Advance)** — start at 10, increase slowly. Watch for:
   - *Too high:* Jerky wing motion, phase hunting
   - *Too low:* No noticeable effect
   - *Good:* Wings feel "connected" to pitch — nose-up produces faster upstroke
4. **Add Ferocity D (D→Dwell)** — start at 10:
   - *Too high:* Waveform oscillates between sharp and soft
   - *Good:* Rapid pitch changes produce sharper strokes, smooth flight stays sinusoidal
5. **Optional: Ferocity P (P→Dwell)** — start at 5, blends P into dwell ratio alongside D
6. **Balance (I→Asymmetry) — last, cautiously** — start at 5:
   - This is the most destabilizing channel. Keep low.
   - *Too high:* Stroke bias oscillates, wings feel lopsided
   - *Good:* Persistent pitch drift is subtly countered
7. **SSFF** — start at 5, only after Cadence/Ferocity are tuned:
   - *Too high:* Over-correction, wing flutter on each half-stroke
   - *Good:* Noticeable reduction in pitch oscillation amplitude
7. **Aeroelastic coefficients** — tune last:
   - During glide: adjust `aeroGlideCoeff` so PID doesn't over-correct while wings are still
   - During flap: adjust `aeroFlapCoeff` so modulation is authoritative but not aggressive

### Safety Rule

> **Always test at low throttle first.** Ondas modulation directly alters wing motion.
> Aggressive modulation can produce unexpected thrust vectors. Start conservative, increase
> incrementally.

---

## 14. Build Targets

Two build targets, both include the full stabilization system:

| Target | `-D` Flags | Use Case |
|--------|-----------|----------|
| `PteronautOS_ESP8285_2400_RX` | `ORNITHOPTER_MODE=1 ZEPHYRUS_ENABLED=1` | 2–4 wing ornithopter with crest rudder |
| `PteronautOS_ESP8285_2400_RX_GEARBOX` | Same + `ORNITHOPTER_GEARBOX=1 MIXER_PROFILE=5` | Motor-driven ornithopter with V-tail/elevator |

Both targets compile with all stabilization code. The gearbox target additionally
computes `aeroGainScale` for motor-on/motor-off and applies PID corrections to
leg ailerons/elevons.

### Build Stats

| Target | RAM | Flash |
|--------|-----|-------|
| RX | 69.2% | 57.4% |
| RX_GEARBOX | 69.2% | 57.1% |

**RAM note:** 69% exceeds the Hermetic Manifest's 65% guideline. This is a pre-existing
architectural constraint from the ExpressLRS WiFi stack + Ornithopter + Zephyrus modules,
not from the stabilization additions.

---

## 15. Known Limitations

### 15.1 No Flash Persistence

All stabilization fields live in RAM only. On power cycle, values revert to
`OrnithopterConfig.h` defaults. This requires integration with the ExpressLRS
config persistence subsystem — deferred to future work.

**Workaround:** Set desired defaults in `OrnithopterConfig.h` before building,
or adjust via WebUI after each boot.

### 15.2 One-Frame Aeroelastic Lag (Gearbox)

In the gearbox kernel, `aeroGainScale` computed in `_computeGearboxMixer()` is
applied to elevator correction in the *next* frame's `zephyrusUpdate()` — a
4ms delay at 250Hz. Negligible for mechanical servos (τ ≫ 4ms).

### 15.3 Ondas Sign Asymmetry

The P→Phase Advance modulation is intentionally sign-dependent: positive pitch
error advances phase, negative retards. This is by design — the waveform
"breathes" differently for nose-up vs nose-down. However, it means the response
is not symmetric, which can feel different when climbing vs diving.

### 15.4 Pitch-Only Modulation

Currently only the pitch axis modulates the waveform. Roll and yaw PID terms
are not bridged for Ondas/SSFF. Roll affects only the crest rudder (servo kernel)
or leg ailerons (gearbox kernel).

### 15.5 Zephyrus Runtime-Disabled Safety

When Zephyrus is compiled in but the MPU6050 is not detected (`enabled=false`):
- All PID terms are zero
- SSFF accumulation produces zero bias (0 × gain = 0)
- Stabilization sections are hidden in WebUI
- This is graceful — no effect on manual flight

### 15.6 Trapezoidal shapeWave at Maximum Ferocity (f=8.0)

When ferocity reaches maximum (f=8.0, dwell ratio d=1.0), the entire waveform is
flat ±1.0 — a pure square wave with no cos ramp. A `d >= 0.999f` guard prevents
division by zero in the ramp computation path (`Validatio` fix, July 2026).
This is a flight safety concern: at maximum ferocity, all waveform shaping nuance
is lost. Pilots should stay below f=7.0 for biologically plausible wing motion.

### 15.7 NaN Propagation Guards (Validatio)

Three layers of NaN defense prevent corrupted MPU6050 data from reaching servo PWM:

| Layer | Location | Guard |
|-------|----------|-------|
| Mahony AHRS | `Zephyrus::_mahonyUpdate()` | `norm < 1e-6f` → reset quaternion |
| PID compute | `Zephyrus::_pidCompute()` | `isnan(dt) \|\| isnan(error)` → return 0.0f |
| P/I/D exposure | `Zephyrus::update()` | `isnan()` on all 3 inputs → zero all 4 terms |
| Clamp guards | `Ornithopter::_computeServoMixer()` | `!(x >= limit)` pattern catches NaN |

The clamp guards use inverted-comparison pattern (`!(x >= limit)` instead of `x < limit`)
because IEEE 754 NaN comparisons always return `false` — a NaN value would bypass a
`x < limit` check but gets caught by `!(x >= limit)` which evaluates to `!(false) = true`.

### 15.8 Resonance Accumulator Glide Persistence

When flapping stops (glide), the resonance accumulator (`_resonanceAccum`) is neither
decayed nor zeroed — it retains its last value until the next flap cycle. Benign during
glide (accumulator not consumed), but if the aircraft glides for minutes and then resumes
flapping, the stale accumulator injects a sudden transient bias. The leaky integrator's
τ=0.15s decay means this bias dissipates within ~1 second of resumed flapping.

### 15.9 Deferred Fields: API Asymmetry

The four deferred fields (`ferocityRollGain`, `ferocityYawGain`, `warpGain`, `warpYawGain`)
are exposed in GET `/pteronautos/state` (always 0.0) but silently dropped by POST
`/pteronautos/config`. The WebUI's `saveConfig()` serializes these fields, but the
firmware ignores them. Harmless for now (deferred), but architecturally inconsistent —
future activation should add POST handlers.

### 15.10 i18n: English-Only Stabilization Labels

Stabilization i18n keys exist only in `en.js` (17 keys). All 10 sibling locale files
(ar, de, es, fr, hi, ja, ko, pt, ru, zh) fall back to English labels via the i18n
engine's `locale → en` fallback chain. This is intentional — technical terms like
"cadence", "ferocity", "trapezoidal dwell", and "phase-locked lock-in amplifier" resist
translation and are best kept in the source language for precision. The fallback is
transparent to users.

---

## 16. File Map

```
src/lib/Zephyrus/
├── Zephyrus.h              # pitchPTerm/ITerm/DTerm/ErrorRate public fields (L33-37)
├── Zephyrus.cpp            # Raw term storage after _pidCompute() (L746-760)
│                           #   includes Validatio NaN guards on all 4 exposed terms
├── ZephyrusConfig.h        # PID gains, rudder/gearbox mix gains, clamp limits
├── ZephyrusFilter.h        # Bridge shim: zephyrus.* → ornithopter.gyroPitch* (L31-34)

src/lib/Ornithopter/
├── Ornithopter.h            # 13 Zephyrus-dependent fields + 7 new Ondas fields (L45-73)
├── Ornithopter.cpp          # _computeServoMixer(): Cadence/Ferocity/Balance/SSFF/
│                           #   Resonance/Aeroelastic + Validatio NaN-safe clamps (L111-258)
│                           # _computeGearboxMixer(): Aeroelastic only (L258-324)
├── OrnithopterConfig.h      # ORNI_CADENCE_GAIN, ORNI_FEROCITY_D_GAIN, ORNI_BALANCE_GAIN,
│                           #   ORNI_FEROCITY_P_GAIN, ORNI_ANCHOR_GAIN, ORNI_RESONANCE_GAIN,
│                           #   ORNI_SSFF_GAIN, ORNI_AERO_*_COEFF + 4 deferred defines
├── OrnithopterWaveform.h    # FlappingOscillator: trapezoidal shapeWave() + anchorGain
│                           #   member + Validatio d>=0.999f div/0 guard

src/lib/WIFI/
├── devWIFI.cpp              # GET /pteronautos/state: exposes 10 stab fields (L746-753)
│                           # POST /pteronautos/config: clamps 6 active fields (L808-813)

src/html/src/pages/
├── ornithopter-panel.coffee # 6 @properties (cadence, ferocity_d, balance, ferocity_p,
│                           #   anchor, resonance) + ssff + aero_glide/flap
├── ornithopter-panel.lithaml# Stabilization (8 sliders) + Aeroelastic (L194-260)

src/html/src/locales/
├── en.js                    # 17 i18n keys for stabilization labels (English only;
│                           #   10 sibling locales fall back to en)

src/targets/
├── pteronautos-rx.ini       # Build flags: -D ZEPHYRUS_ENABLED=1
```