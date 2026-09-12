# MUSHIN v1 — The No-Mind Bridge

> **A shared-compute protocol between PteronautOS (the spirit) and YOSHIMITSU (the muscle).**
> The spirit plans the wave; the muscle strikes. Instead of streaming finished servo µs,
> PteronautOS streams *wave parameters* over a 57600-baud two-wire UART — and the RP2040
> reconstructs phase + waveform locally, per tick, on hardware-exact clocks with no
> WiFi/lwIP jitter. A version handshake keeps a v0 µs path alive for mixed-generation links.

---

## Table of Contents

1. [Architecture](#1-architecture)
2. [Wire Protocol](#2-wire-protocol)
3. [Version Handshake](#3-version-handshake)
4. [Spirit — PteronautOS / ESP8285](#4-spirit--pteronautos--esp8285)
5. [Muscle — YOSHIMITSU / RP2040](#5-muscle--yoshimitsu--rp2040)
6. [Fixed-Point Math Reference](#6-fixed-point-math-reference)
7. [KINCHO Laws (No-Gyro Layer)](#7-kincho-laws-no-gyro-layer)
8. [Crest / Rudder Control](#8-crest--rudder-control)
9. [Failsafe & Graceful Degradation](#9-failsafe--graceful-degradation)
10. [Build & Configuration](#10-build--configuration)
11. [Test Harnesses](#11-test-harnesses)
12. [Extending the Protocol](#12-extending-the-protocol)
13. [File Map](#13-file-map)

---

## 1. Architecture

MUSHIN (無心 — "no-mind") splits the ornithopter control loop in two. The *spirit*
(PteronautOS, an ESP8285 ExpressLRS receiver) still runs the full mixer — throttle,
cadence, ferocity, skew, glide detection — but no longer writes PWM when the muscle is
present. Instead it serializes the **post-mix wave parameters** and ships them over a
dedicated UART. The *muscle* (YOSHIMITSU, an RP2040) owns the servos, advances a
**phase accumulator** on its own timer, and evaluates the waveform locally every ~1 kHz.

```
┌─────────────────────────┐   CRSF   ┌──────────────────────┐  MUSHIN UART  ┌─────────────────────────┐  PWM  ┌──────────────┐
│  Radio (TX16S)          │ ────────▶ │  PteronautOS (spirit)│ ─────────────▶│  YOSHIMITSU (muscle)    │ ─────▶│  wing L / R  │
│  stick → CRSF channels  │           │  ESP8285 @ 80 MHz    │  57600 baud   │  RP2040 @ 133 MHz       │       │  crest rudder│
└─────────────────────────┘           │  Ornithopter mixer    │  [0x9B] frame │  local wave core        │       └──────────────┘
                                      │  + MUSHIN framing     │ ◀─────────────│  + Zephyrus PID (crest) │
                                      └──────────────────────┘  announce/tele └─────────────────────────┘
```

**Why not stream µs?** At 20 Hz wingbeat with fast flapping, the bit-banged
SoftwareSerial bridge (~2.6 ms/frame) caps the effective update rate — the servo µs
trajectory stutters. By sending *parameters* (frequency, amplitude, ferocity, skew,
slew) and letting the muscle integrate phase on a 64-bit accumulator, the bridge becomes
**latency-tolerant**: it only needs to deliver a parameter update a few times per stroke.
The muscle renders the smooth waveform at full servo resolution.

The bridge is the same two wires as the flasher: in the *converter* stances (KINCHO,
MANJI) they speak MUSHIN; in MEDITATION they carry esptool's SLIP untouched.

---

## 2. Wire Protocol

Every frame shares one envelope — no dynamic memory, no framing overhead beyond three
header bytes and a trailing xor:

```
[0x9B][len][type][payload … len bytes][xor]
                       xor = len ^ type ^ payload[0] ^ … ^ payload[len-1]
```

| Type | Name      | Direction         | Meaning                              |
|------|-----------|-------------------|--------------------------------------|
| 0x01 | INTENT    | spirit → muscle   | v1 wave params (11 B) or v0 µs (2n B) |
| 0x02 | ANNOUNCE  | muscle → spirit   | version + posture heartbeat (1 Hz)   |
| 0x03 | TELEMETRY | muscle → spirit   | gyro rate + PID correction (1 Hz)    |

`MUSHIN_MAX_PAY = 16` caps any payload; a frame declaring `len > 16` is discarded at the
length byte.

### 2.1 INTENT v1 — 11-byte parameter frame

`MUSHIN_INTENT_V1_LEN = 11`. All multi-byte fields are little-endian.

| Bytes    | Field        | Type  | Range      | Meaning                                   |
|----------|--------------|-------|------------|-------------------------------------------|
| 0..1     | throttle     | u16   | 0..1000    | `throttlePct × 1000` (1000 = full)        |
| 2        | flapFreq     | u8    | 10..200    | deci-Hz (`freqHz × 10`); **0 = cadence decay** |
| 3        | ferocity     | u8    | 0..100     | post-mix stroke/return ferocity (`×12.5 → 0..8`) |
| 4        | skew         | i8    | −100..+100 | post-mix stroke skew % (return = −skew)   |
| 5        | slew         | u8    | 0..255     | `servoSpeed` ms/60° (0 = unlimited)       |
| 6        | stance       | u8    | 0..5       | active flight profile (parsed for record only) |
| 7..8     | setpointRoll | i16   | ±250       | dps stick→rate setpoint (muscle PID)      |
| 9..10    | setpointPitch| i16   | ±250       | dps stick→rate setpoint                   |

### 2.2 INTENT v0 — legacy µs frame (fallback)

Payload length is even, `n = len/2` uint16 little-endian servo µs values, 1:1 in profile
servo order. Kept intact for a v0 spirit or muscle.

### 2.3 ANNOUNCE (6 bytes)

`[version][servoCount][gyro?][stance?][…][linked]`. Byte 0 carries the muscle's protocol
version (`MUSHIN_VER = 1`); the spirit latches it as `_announcedVersion`. Sent at 1 Hz.

### 2.4 TELEMETRY (6 bytes)

`[gyroRate lo][hi][correction lo][hi][linked][version]` — the muscle's raw yaw rate (LSB,
±250 dps full-scale), the µs correction its crest PID applied, its own link view, and its
protocol version.

---

## 3. Version Handshake

The handshake is **stateless and self-healing** — each side independently decides which
path to use from the data already on the wire:

- **Muscle announces** its version in `ANNOUNCE[0]`.
- **Spirit** reads `_announcedVersion`. `≥ 1` → emits v1 parameter frames; `0` → keeps the
  old µs path (`mushinEmitIntents`).
- **Muscle parses by length**: `msLen == 11` → v1 parameters; otherwise an even length →
  v0 µs intents. A v0 spirit is understood without negotiation.

Both generations therefore coexist on the same two wires. The link itself is *not* torn
down during a version change — only the intent encoding switches.

---

## 4. Spirit — PteronautOS / ESP8285

### 4.1 `MushinNoShin` (`src/lib/Mushin/`)

The spirit's half. `MushinNoShin` holds the parser state machine (`MS_IDLE → MS_LEN →
MS_TYPE → MS_PAY → MS_XOR`), the announce/telemetry latch, and two emitters:

- `emitIntents(const uint16_t *us, uint8_t count)` — v0 µs path (unchanged).
- `emitIntentV1(const MushinIntentV1 &p)` — serializes the 11-byte frame with a
  stack-local buffer; no dynamic memory.

`struct MushinIntentV1` mirrors the wire layout exactly (u16 throttle, u8 flapFreq /
ferocity / slew / stance, i8 skew, i16 setpointRoll / setpointPitch).

The ANNOUNCE handler latches the version **before** the servo count:

```cpp
_announcedVersion = _buf[0];
_announcedServos  = _buf[1];
_linked = true;
```

### 4.2 Bridge serial + cadence dividers

| Platform    | Serial                    | TX / RX pins   | `MUSHIN_PARAM_DIVIDER` | `MUSHIN_INTENT_DIVIDER` |
|-------------|---------------------------|----------------|------------------------|-------------------------|
| ESP8285     | `SoftwareSerial`          | GPIO10 / GPIO9 | 6 (~55 Hz)             | 3 (~111 Hz)             |
| ESP32       | `Serial1` (hardware UART) | —              | 1                      | 1                       |

The v1 frame is 15 bytes = 150 bits ≈ 2.6 ms bit-banged at 57600 — hence the heavier
`MUSHIN_PARAM_DIVIDER = 6`. The v0 µs divider stays at 3 for its own fallback cadence.

### 4.3 Post-mix parameter caching (`Ornithopter`)

`Ornithopter` exposes seven post-mix mirrors, updated only in the flapping branch of
`_computeServoMixer()` (after the differential ferocity/skew is resolved):

```cpp
float lastThrottlePct;   // 0..1 post-mix (0 in glide)
float lastFlapHz;        // actual flap frequency (0 in glide)
float lastStrokeFer;     // 0..8 downstroke ferocity (left wing)
float lastReturnFer;     // 0..8 upstroke ferocity
float lastStrokeSkew;    // ±100 symmetric stroke skew
float lastReturnSkew;    // ±100 symmetric return skew
bool  lastFlapping;      // true = flapping branch ran this tick
```

The glide branch zeroes `lastFlapping`, `lastThrottlePct` and `lastFlapHz` so the muscle
decays its local cadence to rest.

### 4.4 `mushinEmitServoIntents()` (`devServoOutput.cpp`)

Called from `servosUpdate()` when the bridge is linked. It branches on the announced
version:

- **v0** → builds a µs array from `PROFILE.funcMap` / `ornithopter.funcValue()` and calls
  `mushinEmitIntents`.
- **v1** → packs `MushinIntentV1`:
  - `throttle = clamp(lastThrottlePct × 1000 + 0.5, 0, 1000)`
  - `flapFreq = lastFlapping ? clamp(lastFlapHz × 10 + 0.5, 10, 200) : 0`
  - `ferocity = clamp(lastStrokeFer × 12.5 + 0.5, 0, 100)`
  - `skew = clamp(lastStrokeSkew, −100, 100)`, `slew = clamp(servoSpeed, 0, 255)`
  - `stance = clamp(activeFlightProfile, 0, 5)`
  - roll/pitch stick→rate: `norm = (voice − 172) / 819.5 − 1`, a ±0.02 deadband to keep a
    centred stick commanding *hold*, then `dps = norm × 250`.

The divider lives inside `mushinEmitIntentV1` (the wrapper), not in `devServoOutput`.

---

## 5. Muscle — YOSHIMITSU / RP2040

All MUSHIN code lives in `sketches/yoshimitsu/src/Yoshimitsu.h` under the `MUSHIN` block.
No `float`, no `sinf`/`cosf`, no `malloc`, no `delay()` — the RP2040 M0+ ethos.

### 5.1 Parser + parameter state

`mushinParse()` feeds the same `MS_*` state machine. On a xor-clean INTENT it branches on
length:

- `msLen == MUSHIN_INTENT_V1_LEN` → parse `mushinParam` **with forged-value clamps**
  (`throttle ≤ MUSHIN_THROTTLE_MAX = 1000`, `|setRoll/setPitch| ≤ MUSHIN_SETPOINT_MAX =
  250`), set `mushinV1 = 1`, and mark `mushinParamDirty = 1` on a v0→v1 transition so the
  wave core re-seeds its clocks (no dt jump, no velocity-clamp kick).
- otherwise (even length) → parse v0 µs into `mushinIntent[]`, `mushinV1 = 0`.

The `stance` byte is parsed **for the record only** — the muscle's own CRSF `stance` stays
authoritative for MANJI-vs-KINCHO PID selection.

### 5.2 Wave-core state

```cpp
static uint32_t mwLastUs;      // last tick timestamp (µs)
static uint32_t mwLastApplyUs; // 1 kHz rate gate
static uint32_t mwClampUs;     // velocity-clamp timestamp (µs)
static int32_t  mwCadence;     // Q16 rad/s approach value (unity-gain damped)
static int64_t  mwPhaseAcc;    // 64-bit phase accumulator, Q16 rad·µs
static int32_t  mwIterm;       // crest PID I accumulator (LSB·tick)
static uint16_t mwWingL = 1500, mwWingR = 1500;  // velocity-clamp anchors
static uint8_t  mwEasing;      // damped failsafe in progress
static uint32_t mwUnlinkMs;    // easing start (ms)
```

### 5.3 Cosine LUT + interpolation

`mushinCosLut[256]` holds `cos(2π·i/256)` in **Q14** (`16384 = 1.0`), stored as an
ordinary `const` array (no `PROGMEM` needed on RP2040/ESP32).

`mushinCosQ14(phaseQ16)` indexes by the top 8 bits and interpolates linearly over the
**full low byte** (8-bit fraction):

```cpp
uint32_t i = (phaseQ16 >> 8) & 0xFF;
uint8_t  f = (uint8_t)(phaseQ16 & 0xFF);
return (int16_t)((lut[i] * (256 - f) + lut[i+1] * f) >> 8);
```

A 2-bit fraction would quantize the cosine's steepest slope to ~0.6% — audible as a
staircase on the wings — so the full byte is used.

### 5.4 Phase advancement

`mushinWaveTick(nowUs)`:

1. Re-seed clocks if `mushinParamDirty` (first frame after (re)link).
2. `dtUs = clamp(nowUs − mwLastUs, 1, 100000)`.
3. `targetQ16 = flapFreq × MUSHIN_OMEGA_DHZ_Q16` (where `MUSHIN_OMEGA_DHZ_Q16 = 41177`,
   i.e. `0.1 Hz × 2π × 65536`), or `0` during easing.
4. Unity-gain damping `k = 10` mirroring the spirit's `FlappingOscillator`:
   `mwCadence += 10 · (target − cadence) · dtUs / 1e6`; during easing `cadence ×= 9/10`.
5. `mwPhaseAcc += cadence × dtUs` — a 64-bit accumulator in **Q16 rad·µs**, wrapped at
   `MUSHIN_TWO_PI_Q16 × 1e6`. µs-exact phase means slower ticks lose nothing.

### 5.5 Waveform synthesis

`mushinShapeWave(phaseQ16)` is a fixed-point mirror of the spirit's `shapeWave` with
`shapeMix = 0` (plateau + cos, the classic GralhaAzul family):

- **Ferocity** → `f8 = clamp((ferocity×8+50)/100, 0, 8)`, symmetric half-stroke weights
  `wD = wS = 8 − f8` (single ferocity byte → symmetric for v1), giving a limiar at exactly
  π for symmetric strokes.
- **Skew warp** `t' = t + s·t·(1−t)` in Q14, mirrored on the upstroke (`s → −s`),
  endpoints stay pinned.
- **Dwell plateau** `d = f8 × 2007` (Q14, `2007 = 0.98 × 2048`), `dh = d/2`; the plateau
  holds `±16384`, the middle sweeps `cos(π·x)` via the LUT (`thetaQ16 = x·32768`).
- **Return** `descida ? wave : −wave` — the downstroke is the positive half.

---

## 6. Fixed-Point Math Reference

| Constant               | Value      | Meaning                                  |
|------------------------|------------|------------------------------------------|
| `MUSHIN_TWO_PI_Q16`    | 411775     | 2π × 65536 — phase wrap in Q16 rad       |
| `MUSHIN_OMEGA_DHZ_Q16` | 41177      | 0.1 Hz → Q16 rad/s per deci-Hz           |
| `MUSHIN_COS_ONE_Q14`   | 16384      | 1.0 in Q14                                |
| `MUSHIN_COS_LUT_BITS`  | 8          | 256-entry cosine LUT                      |
| `MUSHIN_PID_I_GAIN`    | 4          | Q8 I-term gain (crest PID)                |
| `MUSHIN_PID_I_MAX`     | 4000       | I-accumulator clamp (LSB·tick)            |

Amplitude mapping: `amp = throttle / 2` → throttle 1000 yields **±500 µs** around the
1500 µs centre, inside the 988..2012 PWM window. Left wing is `1500 + amp·wave/16384`,
right wing is mirrored (`3000 − wing`): *code common = physical differential*, the wings
beat against each other.

---

## 7. KINCHO Laws (No-Gyro Layer)

Without a gyro, three laws keep the muscle graceful:

- **Throttle deadband** — `throttle ≤ MUSHIN_TOTBAND_THROTTLE (20)` parks the wings at
  1500 µs rather than flapping feebly.
- **Velocity clamp from slew** — `slew` (ms/60°) becomes a per-tick µs step cap:
  `maxDelta = 333 · dtUs / (slew · 1000)`. `slew = 0` means unlimited. The clamp anchors
  on `mwWingL/R`, so no parameter change can rip a servo.
- **Damped failsafe** — on link loss the wings ease to centre (see §9).

---

## 8. Crest / Rudder Control

The crest rides `servos[GYRO_CORRECTION_SERVO]` (default index 2):

- **KINCHO (no PID)** — the roll rate setpoint maps straight to µs:
  `rudder = 1500 + setRoll · 200 / 250` (±250 dps → ±200 µs).
- **MANJI + gyro (attitude hold)** — the muscle runs its own PID:
  `errLsb = setRoll · GYRO_SCALE_LSB − gyroZRate()`, integrate into `mwIterm` (clamped),
  `corrUs = errLsb · GYRO_GAIN / GYRO_SCALE_LSB + mwIterm · MUSHIN_PID_I_GAIN / 256`,
  clamped ±200 µs, then `rudder = 1500 + corrUs`.

---

## 9. Failsafe & Graceful Degradation

`mushinApply()` gates the core at **1 kHz** (wrap-safe `micros()` delta) — the phase
accumulator is µs-exact, so slower ticks lose nothing; without the gate a free-spinning
loop would burn core 0 on software divisions.

- **Intent stale** (`> MUSHIN_INTENT_STALE_MS = 500`) → if v1, enter **damped failsafe**:
  for `MUSHIN_FAILSAFE_EASE_MS = 250` ms the wings ease to centre under the velocity
  clamp while cadence decays ×0.9/tick, then the link is released and YOSHIMITSU returns
  to its own CRSF muscle. Never a hard tear.
- **Unlink** resets `mushinV1 = 0`, so the next link re-seeds the wave-core clocks via
  `mushinParamDirty` — fresh clocks, no velocity-clamp kick at re-link.
- **Spirit side** — if the heartbeat falls quiet (`> MUSHIN_ANNOUNCE_STALE_MS = 1500`),
  `_linked` clears and PteronautOS returns to its own local PWM + Zephyrus gyro.

The manifest's central-failsafe rule is honoured: the wings always return to 1500 µs.

---

## 10. Build & Configuration

**Spirit target** (`src/targets/pteronautos-rx.ini`):

```ini
-D PTERONAUTOS=1
-D ORNITHOPTER_MODE=1
-D ZEPHYRUS_ENABLED=1
-D MUSHIN_ENABLED=1
```

`pio run -e PteronautOS_ESP8285_2400_RX`

Bridge pins/baud are overridable: `-D MUSHIN_RX_PIN=9 -D MUSHIN_TX_PIN=10 -D MUSHIN_BAUD=57600`.

**Muscle** — YOSHIMITSU is a stock Arduino sketch; the bridge serial is selected by board
(`Serial1`/`Serial2` via `BRIDGE_SERIAL`). MUSHIN boots **ON** (the cheatcode) and is
admin-togglable (`MUSHIN ON/OFF`) via the USB-serial console.

---

## 11. Test Harnesses

Two host harnesses drive the real headers with stubbed Arduino/Serial/Servo/Wire and a
controllable clock (`g_ms`/`g_us`):

| Harness                          | Covers                                                             | Checks |
|----------------------------------|--------------------------------------------------------------------|--------|
| `tools/mushin_test/test_wave.cpp` | cos-LUT interp, phase accumulator + cadence filter, shapeWave (plateau+cos, skew mirror, pinned endpoints), protocol round-trip (v1 + v0 fallback + corrupt xor), KINCHO deadband + velocity clamp, damped failsafe, MANJI crest PID, version handshake, full-flap integration | 1955 |
| `tools/spirit_test/main.cpp`      | v1 framing (15 bytes, field offsets, xor), v0 µs fallback framing   | 61    |

Syntax check (muscle):

```sh
g++ -std=c++17 -fsyntax-only -DARDUINO_ARCH_ESP32S3 -DYOSHI_RP2040=0 \
    -Itools/stub -Isketches/yoshimitsu/src tools/stub/main.cpp
```

Both harnesses ship green (0 failures); their binaries are git-ignored.

---

## 12. Extending the Protocol

- **Asymmetric ferocity / skew** — v1 collapses return ferocity/skew into a single
  `skew`-mirrored byte. To carry `lastReturnFer`/`lastReturnSkew` independently, grow the
  payload (bump `MUSHIN_INTENT_V1_LEN`, add fields to `MushinIntentV1` and `mushinParam`,
  update both parsers' clamps) and bump `MUSHIN_VER` to 2. Keep the length-based parse so
  v1 frames still decode.
- **New stances** — add to `enum Stance`; MUSHIN only runs in KINCHO/MANJI (the
  `mushinPoll` guard). A new converter stance must opt in there.
- **`shapeMix` families** — v1 pins `shapeMix = 0` (GralhaAzul). The pointed family stays
  spirit-only; porting it means adding a mix parameter to the frame.
- **Generating the LUT** — regenerate with:
  `python3 -c "import math; print(', '.join(str(int(round(math.cos(2*math.pi*i/256)*16384))) for i in range(256)))"`

---

## 13. File Map

| Path                                       | Role                                        |
|--------------------------------------------|---------------------------------------------|
| `src/lib/Mushin/MushinNoShin.h`            | spirit: `MushinIntentV1`, class, singletons |
| `src/lib/Mushin/MushinNoShin.cpp`          | spirit: parser, emitters, bridge serial, dividers |
| `src/lib/Ornithopter/Ornithopter.h/.cpp`   | spirit: post-mix parameter caching          |
| `src/lib/ServoOutput/devServoOutput.cpp`   | spirit: `mushinEmitServoIntents()` framing   |
| `src/targets/pteronautos-rx.ini`           | spirit: MUSHIN_ENABLED + pins/baud build flags |
| `sketches/yoshimitsu/src/Yoshimitsu.h`     | muscle: parser, wave core, KINCHO laws, PID  |
| `sketches/yoshimitsu/src/Yoshimitsu_Loadout.h` | muscle: servo/gyro loadout defaults      |
| `tools/mushin_test/test_wave.cpp`          | muscle harness (1955 checks)                |
| `tools/spirit_test/main.cpp`               | spirit harness (61 checks)                  |
| `tools/stub/`                              | Arduino/Serial/Servo/Wire host stubs        |
