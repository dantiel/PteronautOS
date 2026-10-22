# Gearbox Mixers & Glide Controller

> The non-flapping half of the Ornithopter kernel: motor + tail surfaces (and, on
> the Z-Wing, split wing surfaces) driven by a dantiel-style attitude-holding
> glide controller. Every mixer here is a **complete** mixer — wing and tail
> surfaces are never split into separate mixes, because that would fragment pin
> allocation across two kernels.

---

## 1. The glide controller (gearbox kernel only)

The gearbox kernel has no oscillator. In its place a **glide controller** closes
an attitude + rate loop over the tail, the same shape as the dantiel
`arduino-flight-controller` glide branch: motor off at low throttle, tail
surfaces hold the glide attitude.

```
CRSF sticks ──► pilot command (rate/attitude setpoint)
                  │
                  ▼
            tail mix (complete) ──► PWM
                  ▲
Zephyrus AHRS ──► attitude + rate correction (PID)
```

The controller sums two terms on the **same axis** (µs):

| axis | rate damper (fast) | attitude level-hold (slow) | source |
|------|--------------------|----------------------------|--------|
| roll  | `gyroAileronCorrection` | `gyroRollAttitudeCorrection × ZEPHYR_GEARBOX_ATT_GAIN` | `ZephyrusFilter.h` |
| pitch | `gyroElevatorCorrection` | `gyroPitchAttitudeCorrection × ZEPHYR_GEARBOX_ATT_GAIN` | `ZephyrusFilter.h` |
| yaw   | `gyroRudderCorrection` | — (no magnetometer) | `ZephyrusFilter.h` |

All three are clamped to `ZEPHYR_GEARBOX_CLAMP_US` (250 µs). Gains live in
[`ZephyrusConfig.h`](aethercodex://open?path=src/lib/Zephyrus/ZephyrusConfig.h):
`ZEPHYR_GEARBOX_ROLL_GAIN`, `ZEPHYR_GEARBOX_PITCH_GAIN`,
`ZEPHYR_GEARBOX_ATT_GAIN`, `ZEPHYR_GEARBOX_CLAMP_US`.

The gearbox corrections are bridged **unconditionally** (no
`ORNITHOPTER_GEARBOX` gate) so a runtime WebUI profile switch
waveform → gearbox still gets gyro.

---

## 2. Servo functions (`ServoFunc`)

The flat taxonomy — one tag per physical output, indexed by `_f[]`:

| tag | # | meaning |
|-----|---|---------|
| `SF_NONE` | 0 | unassigned |
| `SF_LEFT_WING` / `SF_RIGHT_WING` | 1 / 2 | flapping wing pair (waveform) |
| `SF_RUDDER` | 3 | head/crest rudder |
| `SF_MOTOR` | 4 | thrust motor (gearbox) |
| `SF_VTAIL_LEFT` / `SF_VTAIL_RIGHT` | 5 / 6 | V-tail elevons |
| `SF_ELEVATOR` | 7 | traditional elevator |
| `SF_BACK_LEFT_WING` / `SF_BACK_RIGHT_WING` | 8 / 9 | 4-wing rear pair |
| `SF_WING_AIL_L` / `SF_WING_AIL_R` | 10 / 11 | Z-Wing ailerons (roll) |
| `SF_WING_DRAG_L` / `SF_WING_DRAG_R` | 12 / 13 | Z-Wing split drag rudders (yaw + airbrake) |
| `SF_TAIL_PITCH` | 14 | rotation tail pitch servo |
| `SF_TAIL_ROLL_L` / `SF_TAIL_ROLL_R` | 15 / 16 | rotation tail rotation servos |
| `SF_ELEVON_L` / `SF_ELEVON_R` | 17 / 18 | 2-servo split/elevon tail |
| `SF_TAIL_PITCHROT_L` / `SF_TAIL_PITCHROT_R` | 19 / 20 | merged mechanical tail |

`ORNI_MAX_OUTPUTS = 9` is the widest profile (the Z-Wing) and is kept in sync
with `Motion::outputCount` in `PreparedMotion.h`.

---

## 3. Mixer profiles

`PROFILES[]` in [`OrnithopterConfig.h`](aethercodex://open?path=src/lib/Ornithopter/OrnithopterConfig.h)
maps each profile's PWM indices to functions. The five new gearbox profiles are
complete mixers:

| # | profile | servos | tail type |
|---|---------|--------|-----------|
| 8 | `GEARBOX_1MOT_1RUD_TAIL3` | motor, head rudder, **pitch + 2×rotate** | rotation (3-servo) |
| 9 | `GEARBOX_1MOT_1RUD_ELEVON` | motor, head rudder, **2× elevon** | split (di/anhedral slider) |
| 10 | `GEARBOX_1MOT_1RUD_TAIL11` | motor, head rudder, **pitch + 1×rotate** | rotation (simplified) |
| 11 | `GEARBOX_1MOT_1RUD_TAILMERGED` | motor, head rudder, **2× pitch+rotate** | merged mechanical |
| 12 | `ZWING_9PWM` | motor, head rudder, **4× wing + 3× tail** | rotation + split wings |

Build a fixed profile with `-D MIXER_PROFILE=N`; switch at runtime with the
WebUI (`setOrnithopterProfile`).

---

## 4. Tail physics

### Rotation tails (profiles 8, 10) and the yaw inversion

A rotation tail has a pitch servo and rotation servo(s). The rotation axis is
**roll**. Yaw is not a separate surface — it rides the rotation differential,
gated by the tail's live pitch deflection:

```
differential = rollUs + yawUs · (pitchUs/500) · tailYawCoupling
```

Consequences (all verified by `tools/mushin_test/test_tail_mixers.cpp`):

- **Neutral pitch** → rotation is pure roll, **zero yaw**.
- **Pitched** → rotation also yaws.
- **Negative elevator** → the yaw sign **inverts** (yaw ∝ pitch × rotation).

`tailYawCoupling` (0–100, default 50) scales how much yaw may ride the rotation.

### Merged tail (profile 11)

A linkage merges pitch and rotation so **both servos** contribute to both,
distributing the tail load:

```
servo_L = centre + pitchUs + differential
servo_R = centre + pitchUs − differential
```

Same negative-elevator yaw inversion.

### Split/elevon tail (profile 9) — the dihedral slider

Pitch = common mode, roll = differential. The dihedral/anhedral angle sets how
much yaw rides the roll differential:

```
differential = rollUs + yawUs · tailDihedralMix
```

`tailDihedralMix` (−100…+100) maps the surface angle to a yaw coupling:

| value | geometry | effect |
|-------|----------|--------|
| 0 | flat | pure elevon — no yaw |
| +100 | dihedral | yaw rides the roll differential |
| −100 | anhedral | yaw rides it inverted |

The correct magnitude depends on the surface **size and angle**, which is why it
is a runtime slider rather than a compile-time constant.

---

## 5. Z-Wing 9-PWM (profile 12)

9 outputs: motor + head rudder + 4 wing surfaces (2 per wing: aileron + split
drag rudder) + 3-servo rotation tail.

Channel map (PWM index → function):

| idx | function | role |
|-----|----------|------|
| 0 | `SF_MOTOR` | throttle |
| 1 | `SF_RUDDER` | head rudder (yaw) |
| 2 | `SF_WING_AIL_L` | left aileron (roll) |
| 3 | `SF_WING_DRAG_L` | left split drag rudder (yaw + airbrake) |
| 4 | `SF_WING_AIL_R` | right aileron (roll) |
| 5 | `SF_WING_DRAG_R` | right split drag rudder (yaw + airbrake) |
| 6 | `SF_TAIL_PITCH` | tail pitch |
| 7 | `SF_TAIL_ROLL_L` | tail rotation L |
| 8 | `SF_TAIL_ROLL_R` | tail rotation R |

Authority matrix (fractions of the ±500 µs full deflection — the "fine tuned"
constants, tune per airframe):

| surface | axis | coefficient |
|---------|------|-------------|
| ailerons | roll | 0.75 |
| tail rotation | roll | 0.35 |
| drag rudders | yaw | 0.60 |
| drag rudders | airbrake | 300 µs max (common) |
| head rudder | yaw | `rudderYawWeight` (65) |
| tail rotation | yaw | `tailYawCoupling` × pitchNorm |

**Airbrake** rides CH6 (the flap-frequency channel, otherwise unused in the
gearbox kernel) mapped 0…1: retracted → full spread. The drag rudders receive
it as a *common* deflection (`+airbrake` on both), yaw as a *differential*
(`±yaw`).

The 9-channel wire contract is `Motion::outputCount = 9` in
[`PreparedMotion.h`](aethercodex://open?path=sketches/yoshimitsu/src/PreparedMotion.h)
(126 → 132-byte packet). The RP2040/ESP32-S3 companion's `SERVO_COUNT_MAX` is
9; the ninth pin defaults to `-1` (opt-in — assign `SERVO_PIN_9` per board).

---

## 6. Channel mapping (CRSF)

| channel | gearbox meaning |
|---------|-----------------|
| CH1 (idx 0) | aileron / roll |
| CH2 (idx 1) | elevator / pitch |
| CH3 (idx 2) | throttle → motor |
| CH4 (idx 3) | rudder / yaw |
| CH5 (idx 4) | arm |
| CH6 (idx 5) | airbrake (Z-Wing) — flap frequency when flapping |
| CH7 (idx 6) | flight profile selector |

---

## 7. Tuning summary

- `tailDihedralMix` (−100…+100) — split-tail yaw↔roll coupling (surface angle).
- `tailYawCoupling` (0–100) — rotation-tail pitch-gated yaw authority.
- `rudderYawWeight` / `rudderRollWeight` — head-rudder authority split.
- `motorMinUs` / `motorMaxUs` — throttle idle/full window.
- `ZEPHYR_GEARBOX_ATT_GAIN` — glide level-hold strength (compile-time).
- Z-Wing coefficients — inline in `_gearboxZwing()` (compile-time).

Verification: `bash tools/test_prepared_motion.sh` runs the wire parity,
EP2 preparer, tail-mixer physics, core-phase and muscle/spirit suites.
