# PteronautOS

> **Firmware for servo-driven ornithopters** — a fork of ExpressLRS 4.x running on ESP8285 PWM receivers.
> The vehicle is a biomechanical pterosaur with flapping wings and a crest rudder.
> CRSF radio in → waveform + gyro stabilization → PWM servos out.

---

## Table of Contents

1. [Architecture](#1-architecture)
2. [Hardware — PWMP7 Receiver](#2-hardware--pwmp7-receiver)
3. [Build & Flash](#3-build--flash)
4. [Ornithopter — Wing Waveform Engine](#4-ornithopter--wing-waveform-engine)
5. [Zephyrus — Gyro Stabilization](#5-zephyrus--gyro-stabilization)
6. [ServoOutput — PWM Layer](#6-servooutput--pwm-layer)
7. [Configuration Reference](#7-configuration-reference)
8. [WiFi WebUI](#8-wifi-webui)
9. [Build Pipeline](#9-build-pipeline)
10. [Failsafe & Robustness](#10-failsafe--robustness)
11. [Tuning Guide](#11-tuning-guide)
12. [File Map](#12-file-map)

---

## 1. Architecture

```
┌──────────┐   CRSF    ┌──────────┐   channels   ┌─────────────┐   PWM µs   ┌──────────────┐
│  Radio   │ ────────→ │ rx_main  │ ───────────→ │ Ornithopter  │ ────────→ │ ServoOutput  │
│ (TX16S)  │           │ (ELRS)   │              │ mixer+wave   │           │ PWM 50-160Hz │
└──────────┘           └──────────┘              └──────┬───────┘           └──────┬───────┘
                                                       │                          │
                                                       │ gyroRudderCorrection     │
                                                       │ (µs offset)              │
                                               ┌───────┴────────┐                 │
                                               │   Zephyrus     │                 │
                                               │ MPU6050+AHRS   │                 │
                                               │ Roll+Yaw PID   │                 │
                                               └────────────────┘                 │
                                                                                  │
                                  GPIO0 ──── Left Wing Servo  ←────────────────────┘
                                  GPIO1 ──── Right Wing Servo ←────────────────────┘
                                  GPIO3 ──── Crest Rudder     ←────────────────────┘
                                  GPIO9 ──── Aux 4
                                  GPIO10 ─── Aux 5
                                  GPIO2 ──── I2C SCL (MPU6050)
                                  GPIO5 ──── I2C SDA (MPU6050)
                                  GPIO16 ─── LED (digital)
```

### Module Map

| Module | Directory | Role | Guard |
|--------|-----------|------|-------|
| **Ornithopter** | `src/lib/Ornithopter/` | Waveform generator, channel mixer, servo µs computation | `-D ORNITHOPTER_MODE=1` |
| **Zephyrus** | `src/lib/Zephyrus/` | MPU6050 IMU → Mahony AHRS → dual PID → crest rudder correction | `-D ZEPHYRUS_ENABLED=1` |
| **ServoOutput** | `src/lib/ServoOutput/` | PWM generation synchronized to CRSF tick, failsafe handling | `TARGET_RX` |
| **Target** | `src/targets/pteronautos-rx.ini` | Build configuration, flags, excludes | — |

### Design Principles

- **No heap allocation** — stack and static globals only (`malloc`/`new` prohibited)
- **No blocking** — no `delay()`, everything async on the CRSF loop tick
- **Fail-safe centering** — all servos return to 1500µs on link loss
- **Graceful degradation** — MPU6050 absent → Zephyrus disables silently, no crash
- **Zero external dependencies** — register-level I2C, custom AHRS, custom PID; only `Wire.h` and `Arduino.h`
- **#ifdef guard pattern** — all PteronautOS code compiles to zero-cost no-ops when flags are absent

---

## 2. Hardware — PWMP7 Receiver

### Target Board: Generic 2400 PWMP7

| Property | Value |
|----------|-------|
| **MCU** | ESP8285 @ 80 MHz, 1 MB Flash |
| **Radio** | SX1280 (2.4 GHz) |
| **PWM outputs** | 7 native → 5 usable with I2C gyro |
| **Build target** | `PteronautOS_ESP8285_2400_RX` |
| **Hardware JSON** | `src/hardware/RX/Generic 2400 PWMP7.json` |

### Complete Pin Map

| GPIO | PWMP7 Function | PteronautOS Role | Notes |
|------|---------------|------------------|-------|
| **0** | PWM Ch1 + Button | Left wing servo + Button | Shared OK |
| **1** | PWM Ch2 + UART TX | Right wing servo + Serial TX | Shared OK |
| **2** | PWM Ch7 | **I2C SCL** (MPU6050) | Repurposed — PWM lost |
| **3** | PWM Ch3 + UART RX | Crest rudder servo + Serial RX | Shared OK |
| **4** | SX1280 DIO1 (IRQ) | Radio interrupt | **Hard-reserved** |
| **5** | PWM Ch6 | **I2C SDA** (MPU6050) | Repurposed — PWM lost |
| **9** | PWM Ch4 | Aux servo | Free |
| **10** | PWM Ch5 | Aux servo | Free |
| **12** | SPI MISO | Radio bus | Hard-reserved |
| **13** | SPI MOSI | Radio bus | Hard-reserved |
| **14** | SPI SCK | Radio bus | Hard-reserved |
| **15** | SPI NSS | Radio bus | Hard-reserved |
| **16** | LED | Status LED (digital only) | Reclaimed |
| **17** | VBAT ADC | Voltage monitor | Free |

### MPU6050 Wiring (GY-521 Breakout)

```
GY-521          PWMP7
------          -----
VCC  ─────────  3.3V
GND  ─────────  GND
SDA  ─────────  GPIO 5  (PWM Ch6 pad)
SCL  ─────────  GPIO 2  (PWM Ch7 pad)
```

The GY-521 includes on-board 4.7kΩ I2C pull-up resistors. **No external resistors needed.**

### Boot Safety — Active SCL Probe

Before initializing I2C, the firmware drives SCL LOW, releases to high-impedance, and reads:

| Condition | Pin reads | Result |
|-----------|-----------|--------|
| MPU6050 connected | HIGH (4.7kΩ pull-up) | I2C initializes normally |
| MPU6050 absent | LOW (float/PWM pull-down) | Zephyrus skips I2C entirely |

This means you can **flash without the MPU connected** — the receiver boots normally. Connect the MPU later, power-cycle, and gyro stabilization auto-activates.

---

## 3. Build & Flash

### Prerequisites

- PlatformIO (`pip install platformio`)
- Node.js 18+ (for WebUI build)
- ESP8285 board support (PlatformIO installs automatically)

### Build

```bash
cd src
pio run -e PteronautOS_ESP8285_2400_RX
```

**Current build stats:**

| Resource | Used | Limit | % |
|----------|------|-------|---|
| RAM | 50,864 B | 81,920 B | 62.1% |
| Flash | 553,416 B | 991,216 B | 55.8% |

### First Flash — UART Required

⚠️ **First install from factory ELRS 3.x must be via UART.** Wi-Fi cross-flash fails with `ERROR[4]: Not Enough Space` — the ESP8266 Updater needs both firmwares in flash simultaneously, and PteronautOS (553KB) + ELRS 3.x (480KB) exceeds the 995KB sketch partition.

```bash
# 1. Full chip erase (removes factory LittleFS config)
esptool.py --chip esp8266 --port /dev/cu.usbserial-XXXX --baud 460800 erase_flash

# 2. Write firmware
esptool.py --chip esp8266 --port /dev/cu.usbserial-XXXX --baud 460800 \
  write_flash 0x0 .pio/build/PteronautOS_ESP8285_2400_RX/firmware.bin
```

Or via PlatformIO:

```bash
pio run -e PteronautOS_ESP8285_2400_RX -t upload --upload-port /dev/cu.usbserial-XXXX
```

**Enter bootloader:** Hold the button (GPIO0→GND), power on, release after 1-2 seconds.

**After first UART flash, all subsequent updates work via Wi-Fi.**

### Subsequent Wi-Fi Updates

1. Power receiver — enters WiFi AP mode after 60s without transmitter
2. Connect to `ExpressLRS RX` (password: `expresslrs`)
3. Browse to `http://10.0.0.1`
4. Upload `firmware.bin` via the web UI

---

## 4. Ornithopter — Wing Waveform Engine

### CRSF Channel Map (10 channels)

| Index | Channel | Role | Range |
|-------|---------|------|-------|
| 0 | **Aileron** | Roll steering (differential wing amplitude) | 172–1811 |
| 1 | **Elevator** | Pitch (wing angle bias) | 172–1811 |
| 2 | **Throttle** | Flap frequency + amplitude | 172–1811 |
| 3 | **Rudder** | Yaw (crest rudder deflection) | 172–1811 |
| 4 | **Arm** | Safety switch (>992 = armed) | 172–1811 |
| 5 | Rudder Ferocity | Rudder response curve sharpness | 172–1811 |
| 6 | Stroke Ferocity | Downstroke waveform sharpness | 172–1811 |
| 7 | Return Ferocity | Upstroke waveform sharpness | 172–1811 |
| 8 | Cadence | Frequency fine-tune | 172–1811 |
| 9 | Altitude Hold | (reserved, future) | 172–1811 |

### Flapping Oscillator

The heart of the wing motion is a phase-accumulator oscillator with asymmetric tanh waveshaping:

```
phase += cadence × dt          ← frequency from throttle + cadence channel
rawSin = sin(phase)            ← base sine wave
shaped = tanh(ferocity × rawSin) / tanh(ferocity)   ← asymmetric waveform
```

Where `ferocity` switches between `strokeFerocity` (downstroke, rawSin ≥ 0) and `returnFerocity` (upstroke, rawSin < 0). This produces a biologically plausible wingbeat with:

- **Sharp, powerful downstroke** (high ferocity → near-square wave)
- **Smooth, gradual upstroke** (low ferocity → near-sine)

### Mixer

```
aileronCmd  = aileronNorm  × AILERON_SCALE    (0.04)
elevatorCmd = elevatorNorm × ELEVATOR_SCALE   (0.06)

if flapping:
    angleLeft  = NEUTRAL_ANGLE + shaped × MAGNITUDE_SCALE × amplitude
               + aileronCmd × ANGULAR_MULTIPLIER + elevatorCmd
    angleRight = NEUTRAL_ANGLE + shaped × MAGNITUDE_SCALE × amplitude
               - aileronCmd × ANGULAR_MULTIPLIER + elevatorCmd
else: (glide)
    angleLeft  = NEUTRAL_ANGLE + GLIDE_ANGLE + aileronCmd + elevatorCmd
    angleRight = NEUTRAL_ANGLE + GLIDE_ANGLE - aileronCmd + elevatorCmd

servoLeftUs   = angleToUs(angleLeft)
servoRightUs  = angleToUs(angleRight)
servoRudderUs = RUDDER_CENTER + rudderMix × 500 + gyroRudderCorrection
```

### Key Constants (OrnithopterConfig.h)

| Constant | Value | Description |
|----------|-------|-------------|
| `SERVO_CENTER_US` | 1500 | Neutral servo pulse |
| `SERVO_MIN_US` | 988 | Minimum pulse width |
| `SERVO_MAX_US` | 2012 | Maximum pulse width |
| `FLAP_THRESHOLD_US` | 1040 | Throttle value to start flapping |
| `MAGNITUDE_SCALE` | 0.04 | Waveform amplitude to angle conversion |
| `FEROCITY_MIN` | 1.0 | Soft sine-like waveform |
| `FEROCITY_MAX` | 8.0 | Sharp square-like waveform |
| `AILERON_SCALE` | 0.04 | Roll stick sensitivity |
| `ELEVATOR_SCALE` | 0.06 | Pitch stick sensitivity |
| `RUDDER_MIX_SCALE` | 0.25 | Aileron→rudder bleed for coordinated turns |
| `CYCLE_RATING` | 0.070 | Cadence responsiveness |

---

## 5. Zephyrus — Gyro Stabilization

### Data Flow

```
MPU6050 (0x68)                    Zephyrus::update()
     │                                  │
     ├─ Gyro X,Y,Z (raw int16) ────────→ bias subtraction
     ├─ Accel X,Y,Z (raw int16) ───────→ scale conversion
     │                                  │
     │                          ┌───────┴────────┐
     │                          │  Mahony AHRS   │
     │                          │  Kp=2.0, Ki=0  │
     │                          │  → quaternion  │
     │                          └───────┬────────┘
     │                                  │
     │                    roll°, pitch° (from quat)
     │                    yawRate °/s   (from gyro Z directly)
     │                                  │
     │                          ┌───────┴────────┐
     │                          │  Dual PID      │
     │                          │  Roll:  Kp=1.2 │
     │                          │  Yaw:   Kp=0.8 │
     │                          └───────┬────────┘
     │                                  │
     │                    rollCorrection (+), yawCorrection (+)
     │                                  │
     │                          ┌───────┴────────┐
     │                          │  Rudder Mixer  │
     │                          │  roll×2.5 µs/° │
     │                          │ +yaw×1.5 µs/s  │
     │                          │  clamp ±200µs  │
     │                          └───────┬────────┘
     │                                  │
     │                    gyroRudderCorrection (µs)
     │                                  │
     │                                  ▼
     │                          Ornithopter::_computeMixer()
     │                          → servoRudderUs
```

### Why Yaw-Rate-Only (No Magnetometer)

Mahony AHRS runs with `Ki=0` — no gyro bias estimation. Roll and pitch are corrected by the accelerometer (gravity vector). Yaw is taken directly from gyro Z as **rate** (`°/s`), not angle. A yaw *angle* would drift without a magnetometer, but yaw *rate* is drift-free. The PID dampens unwanted rotation — it doesn't hold heading.

### MPU6050 Settings

| Register | Value | Effect |
|----------|-------|--------|
| GYRO_CONFIG | ±250 °/s | Full-scale range |
| ACCEL_CONFIG | ±2g | Full-scale range |
| CONFIG (DLPF) | 3 | 44 Hz accel, 42 Hz gyro bandwidth |
| SMPLRT_DIV | 0 | 1 kHz sample rate |

### PID Controllers

#### Roll PID (angle stabilization)
| Parameter | Default | Description |
|-----------|---------|-------------|
| Kp | 1.2 | Proportional — strength of correction |
| Ki | 0.05 | Integral — eliminates steady-state offset |
| Kd | 0.15 | Derivative — dampens oscillations |
| Imax | ±30° | Anti-windup clamp |
| Setpoint | 0° | Target: wings level |

#### Yaw PID (rate dampening)
| Parameter | Default | Description |
|-----------|---------|-------------|
| Kp | 0.8 | Proportional |
| Ki | 0.03 | Integral |
| Kd | 0.10 | Derivative |
| Imax | ±20 °/s | Anti-windup clamp |
| Setpoint | 0 °/s | Target: no rotation |

Both use **derivative-on-measurement** (derivative acts on the sensor value, not the error) to avoid derivative kick on setpoint changes.

### Rudder Mixer

```
rudderCorrection = rollCorrection × ROLL_GAIN  (2.5 µs/°)
                 + yawCorrection  × YAW_GAIN   (1.5 µs/(°/s))
clamped to ±RUDDER_CLAMP_US (200 µs)
```

### Auto-Calibration

On first `update()` after boot, the module collects gyro samples while the ornithopter sits motionless:

1. Collect up to 200 samples at 1 kHz (~200 ms)
2. Require 20 consecutive stable samples (no axis changes >2°/s)
3. Per-axis variance check: σ² < 250 → accept bias; otherwise skip axis
4. Max 500 samples before best-effort timeout

Calibration data is **not persisted** — it recalibrates on every power cycle. Place the ornithopter on a flat, vibration-free surface before powering on.

### Performance

| Metric | Value |
|--------|-------|
| `update()` execution | ~444 µs |
| I2C burst read (dominant) | ~400 µs |
| AHRS + PID compute | ~44 µs |
| CPU load at 50 Hz | 2.2% |
| RAM footprint | ~200 B |
| Flash footprint | ~1.5 KB |

### Graceful Failure Modes

| Failure | Behavior |
|---------|----------|
| MPU6050 not detected | `enabled=false`, all corrections zero |
| I2C read timeout (3 ms) | Correction decays at 0.9× per cycle |
| Quaternion NaN | Reset to identity |
| dt > 100 ms | Clamped to prevent integration blowup |
| Radio link down | PID integrators zeroed, corrections zeroed |

---

## 6. ServoOutput — PWM Layer

Located in `src/lib/ServoOutput/devServoOutput.cpp`. The PWM engine:

1. Receives CRSF channel packets via interrupt (`servoNewChannelsAvailable`)
2. On each loop tick, calls `ornithopterUpdate()` then `zephyrusUpdate()`
3. Reads computed `servoLeftUs`, `servoRightUs`, `servoRudderUs` from Ornithopter
4. Writes PWM pulses at configured frequency (50–160 Hz for servos)
5. On failsafe: centers all channels to 1500µs, resets Ornithopter oscillator, zeroes Zephyrus corrections

### Integration Points

| Location | Call | Purpose |
|----------|------|---------|
| Loop tick | `ornithopterUpdate()` | Compute waveform + mixer |
| Loop tick | `zephyrusUpdate()` | Read gyro, compute PID, set `gyroRudderCorrection` |
| Connection | `zephyrusOnLinkUp()` | Reset PID integrators on arm |
| Failsafe | `ornithopterEnterFailsafe()` | Center servos, reset oscillator |
| Failsafe | `zephyrusOnLinkDown()` | Zero corrections, reset integrators |

### Filter Shim Pattern

Both Ornithopter and Zephyrus use a zero-overhead `#ifdef` bridge:

```cpp
// OrnithopterFilter.h
#ifdef ORNITHOPTER_MODE
#include "Ornithopter.h"
extern Ornithopter ornithopter;
static inline void ornithopterUpdate()           { ornithopter.update(); }
static inline void ornithopterEnterFailsafe()     { ornithopter.enterFailsafe(); }
#else
static inline void ornithopterUpdate() {}
static inline void ornithopterEnterFailsafe() {}
#endif
```

When built without `-D ORNITHOPTER_MODE=1`, all functions inline to **nothing** — zero flash, zero RAM, zero cycles.

---

## 7. Configuration Reference

### Build Flags (`src/targets/pteronautos-rx.ini`)

```ini
-D PTERONAUTOS=1           # Project identity flag
-D ORNITHOPTER_MODE=1      # Enable Ornithopter module
-D ZEPHYRUS_ENABLED=1      # Enable Zephyrus gyro module
-D ZEPHYR_I2C_SDA=5        # I2C SDA pin (GPIO5, was PWM Ch6)
-D ZEPHYR_I2C_SCL=2        # I2C SCL pin (GPIO2, was PWM Ch7)
```

### Key Tuning Parameters

All in `OrnithopterConfig.h` and `ZephyrusConfig.h`. See sections 4 and 5 above for full tables.

### PWMP7 Hardware JSON

```json
{
    "radio_dio1": 4,  "radio_miso": 12, "radio_mosi": 13,
    "radio_nss": 15,  "radio_sck": 14,  "led": 16,
    "pwm_outputs": [0,1,3,9,10,5,2],
    "vbat": 17, "vbat_offset": 12, "vbat_scale": 310,
    "vbat_cal_min": 3500, "vbat_cal_max": 25200
}
```

The I2C pin override happens at compile time via `-D ZEPHYR_I2C_SDA=5` and `-D ZEPHYR_I2C_SCL=2`. The hardware JSON retains stock ELRS PWM output definitions for upstream compatibility.

---

## 8. WiFi WebUI

After booting without a transmitter for 60 seconds, the receiver enters WiFi AP mode. Connect to `ExpressLRS RX` (password: `expresslrs`) and browse to `http://10.0.0.1`.

### PteronautOS Branded UI

The PteronautOS WebUI is a feature-gated overlay on the standard ExpressLRS web interface, built with **Lit 3 + Vite 8**. All PteronautOS content is guarded by `FEATURE:PTERONAUTOS` markers — standard ELRS builds are completely unaffected.

| Panel | Content |
|-------|---------|
| **Ornithopter** | Waveform shape visualization, frequency slider (1–15 Hz), L/R amplitude, rudder offset, throttle→frequency mapping |
| **Zephyrus** | MPU6050 connection status, artificial horizon (CSS), roll/yaw PID slider controls, calibration trigger |
| **Servo** | 5-channel PWM table (GPIO 0,1,3,9,10), center/min/max/failsafe per channel |
| **Info** | Ornithopter Mode + Zephyrus Gyro status rows |
| **Hardware Schema** | I2C bus (GPIO2=SCL, GPIO5=SDA) visualization |

### Theme

**Fossil palette**: charcoal `#1a1a1a` body, dark amber/copper header gradient (`#b8860b`→`#8b6914`→`#5c3d0e`), gold `#d4a017` accents. All ELRS blue/green gradients replaced.

Panels are **decorative** — they render sliders and inputs but do not persist to firmware. Telemetry plumbing for live gyro data, PID tuning, and waveform visualization is planned.

### Building the WebUI

```bash
cd src/html
nvm use 22                         # Node 18+ required
npm install
npm run build:pteronautos          # → headers/web-pteronautos-rx-8285.h
```

The build process automatically:
1. Strips all non-Pteronautos feature blocks
2. Injects the branded CSS, panels, logo, and footer
3. Gzip-compresses everything into a C byte array header
4. `copy_html.py` detects `-D PTERONAUTOS=1` in build flags and copies the header into place

When the PteronautOS header isn't available, the build falls back to the standard ELRS web header — the device boots normally with the unthemed UI.

---

## 9. Build Pipeline

```
src/html/
  ├── index.html, app.js, features.js
  ├── src/pages/*.js          (panels)
  ├── src/assets/pteronautos.css
  └── build-plugins/
        ├── feature-blocks-plugin.js   ← strips FEATURE:NOT PTERONAUTOS
        └── esp32-header-plugin.js     ← gzip → C byte array header

        npm run build:pteronautos
                │
                ▼
        headers/web-pteronautos-rx-8285.h
                │
                ▼  (copy_html.py detects -D PTERONAUTOS=1)
        src/include/WebContent.h  →  #include "headers/web-pteronautos-rx-8285.h"
                │
                ▼  (UnifiedConfiguration.py appends PWMP7 hardware JSON)
        .pio/build/PteronautOS_ESP8285_2400_RX/firmware.bin
```

### Key Scripts

| Script | Role |
|--------|------|
| `copy_html.py` | Pre-build hook — copies the correct WebUI header based on build flags |
| `UnifiedConfiguration.py` | Post-build hook — embeds hardware JSON, detects PteronautOS targets |
| `pteronautos_defaults.py` | Injects PteronautOS-specific default options into firmware |

---

## 10. Failsafe & Robustness

### Failsafe Sequence

1. CRSF link lost → `connectionState` → disconnected
2. After `FAILSAFE_ABS_TIMEOUT_MS` (1000 ms) without packet: `servosEnterFailsafe()`
3. All PWM channels → 1500µs (center)
4. `ornithopter.enterFailsafe()` → oscillator reset, angles neutral
5. `zephyrusOnLinkDown()` → PID integrators zeroed, corrections zeroed

### Protection Layers

| Layer | Mechanism |
|-------|-----------|
| **I2C pin safety** | Compile-time `#ifndef` guards + PWM exclusion at init + config defaults set to `somSCL`/`somSDA` |
| **Watchdog** | ESP8266 hardware WDT resets on main loop stall |
| **No heap** | All state is value types — no null pointers, no leaks, no fragmentation |
| **I2C timeout** | 3 ms per read — bus fault can't hang the system |
| **dt clamping** | 0 < dt ≤ 100 ms — prevents AHRS/PID blowup after transient stall |
| **Anti-windup** | Both PID integrators clamped to configurable limits |
| **Graceful MPU absence** | Active SCL probe → skip I2C if no pull-up detected |

---

## 11. Tuning Guide

### First Flight

1. **Set up radio channels** — Aileron, Elevator, Throttle, Rudder, Arm on channels 1–5; assign channels 6–10 to pots/sliders for ferocity/cadence tuning
2. **Set failsafe** — all channels to center (992 for CRSF), Arm low (172)
3. **Arm at zero throttle** — verify wings hold glide position (~96°)
4. **Increase throttle slowly** — wings should start flapping at ~1040µs
5. **Adjust ferocity channels mid-flight** — higher values = sharper downstroke

### Zephyrus PID Tuning

1. **Start with roll only** — set yaw Kp to 0, yaw mixer gain to 0
2. **Hover test** — fly in still air, observe roll oscillations
3. **Increase roll Kp** until mild oscillation, then back off 30%
4. **Add roll Kd** until oscillation dampens
5. **Add roll Ki** — small value to eliminate steady-state drift
6. **Enable yaw** — repeat for yaw rate dampening
7. **Adjust mixer gains** — target 50–150 µs correction in normal flight

### Symptom → Fix

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Slow roll oscillation (1–2 Hz) | Kp too high or Kd too low | Reduce Kp 20%, increase Kd 50% |
| Fast jitter (10+ Hz) | Motor vibration in DLPF passband | Increase DLPF to 4 or 5 |
| Persistent roll offset | Ki too low or Imax too tight | Ki → 0.08, Imax → 40 |
| Rudder twitches on throttle | I2C noise from PWM | Twist I2C wires, 100nF cap at MPU6050 VCC |
| Correction wrong direction | MPU6050 orientation | Negate ROLL_GAIN and YAW_GAIN |
| Calibration never completes | Vibration during startup | Keep still during power-on |

---

## 12. File Map

```
PteronautOS/
├── hermetic.manifest.md              # Project ethos & constraints
├── docs/
│   ├── PTERONAUTOS.md               # ← this document
│   └── en/hardware/elrs-pwm7-esp8285.md  # Hardware-specific reference
├── src/
│   ├── targets/pteronautos-rx.ini    # Build target definition
│   ├── hardware/RX/Generic 2400 PWMP7.json  # Hardware pin definitions
│   ├── include/WebContent.h          # Auto-generated: #include web header
│   ├── lib/
│   │   ├── Ornithopter/
│   │   │   ├── Ornithopter.h         # Class: mixer, channels, servo outputs
│   │   │   ├── Ornithopter.cpp       # Implementation: waveform, mixer, failsafe
│   │   │   ├── OrnithopterConfig.h   # Compile-time constants (channel map, servo limits)
│   │   │   ├── OrnithopterWaveform.h # FlappingOscillator: phase + tanh waveshaping
│   │   │   └── OrnithopterFilter.h   # #ifdef bridge shim → devServoOutput
│   │   ├── Zephyrus/
│   │   │   ├── Zephyrus.h            # Class: MPU6050 driver, AHRS, PID, rudder mixer
│   │   │   ├── Zephyrus.cpp          # Full implementation (~526 lines)
│   │   │   ├── ZephyrusConfig.h      # All tunable constants (I2C, PID, gains)
│   │   │   ├── ZephyrusFilter.h      # #ifdef bridge shim → devServoOutput
│   │   │   └── README.md            # Zephyrus-specific deep reference
│   │   └── ServoOutput/
│   │       └── devServoOutput.cpp    # PWM engine, CRSF tick, failsafe entry
│   ├── python/
│   │   ├── copy_html.py             # Pre-build: select WebUI header
│   │   ├── UnifiedConfiguration.py  # Post-build: embed hardware JSON
│   │   └── pteronautos_defaults.py  # Inject PteronautOS option defaults
│   └── html/                         # WebUI source (Lit 3 + Vite 8)
│       ├── package.json              # "build:pteronautos" script
│       ├── features.js               # FEATURE:PTERONAUTOS gate logic
│       ├── src/
│       │   ├── app.js               # Router, brand header, logo
│       │   ├── pages/
│       │   │   ├── ornithopter-panel.js
│       │   │   ├── zephyrus-panel.js
│       │   │   └── servo-panel.js
│       │   ├── assets/pteronautos.css # Fossil amber/charcoal theme
│       │   └── components/elrs-footer.js  # Pterosaur ASCII art
│       └── build-plugins/
│           ├── feature-blocks-plugin.js  # Strips non-Pteronautos content
│           └── esp32-header-plugin.js    # gzip → C header
```

---

## 13. Telemetry & Data

### CRSF Attitude Telemetry (to TX)

When `ZEPHYRUS_ENABLED=1` and the RC link is active, `rx_main.cpp` injects
`CRSF_FRAMETYPE_ATTITUDE` frames every 100ms into the OTA downlink. EdgeTX/OpenTX
natively displays these as a horizon indicator on the telemetry screen.

| Field | Units | Source |
|-------|-------|--------|
| Pitch | radians × 10000 | `zephyrus.pitchDeg` → radians |
| Roll | radians × 10000 | `zephyrus.rollDeg` → radians |
| Yaw | radians × 10000 | `zephyrus.yawRate` (°/s) → rad/s |

- Requires: connected RC link (no telemetry in WiFi mode)
- Rate: ~10Hz (every 100ms, throttled by DataDlSender availability)
- CRC: DVB-S2 (polynomial 0xD5) via `GENERIC_CRC8`

### WebUI Live State Endpoint

`GET http://10.0.0.1/pteronautos/state` returns live JSON with:

```json
{
  "zephyrus": {
    "enabled": true,
    "calibrated": true,
    "roll_deg": 1.5,
    "pitch_deg": -3.2,
    "yaw_rate": 0.8,
    "roll_correction": 12.4,
    "yaw_correction": -5.1,
    "rudder_correction": 7.3
  },
  "ornithopter": {
    "enabled": true,
    "link_up": true,
    "servo_left_us": 1520,
    "servo_right_us": 1480,
    "servo_rudder_us": 1507,
    "voice_throttle": 1200,
    "voice_cadence": 992,
    "voice_aileron": 992,
    "voice_elevator": 992,
    "voice_rudder": 992
  }
}
```

- When Zephyrus/Ornithopter are disabled, their `enabled` field is `false`
- All values are live (read from global `zephyrus` / `ornithopter` objects)
- Accessible from any browser on the WiFi network, no auth required
- Used by the branded WebUI panels for live data display (when npm build is active)

### Planned: CRSF Parameter Protocol

Future work will register Ornithopter/Zephyrus parameters via
`CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY` / `PARAMETER_READ` / `PARAMETER_WRITE`,
enabling real-time PID/waveform tuning from EdgeTX LUA scripts and the WebUI.

---

*PteronautOS — Fly Natural. Control Precise.*