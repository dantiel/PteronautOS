# PteronautOS

**Open-source firmware for servo-driven ornithopters** — forked and heavily customized from [ExpressLRS](https://github.com/ExpressLRS/ExpressLRS).

PteronautOS replaces the traditional fixed-wing flight controller with an adaptive flapping oscillator built directly into the receiver. No intermediate FC required. The radio link directly drives the wings.

## Features

- **Adaptive Flapping Oscillator** — configurable waveform, amplitude, and frequency per channel
- **Automatic Gliding** — wings lock at neutral below throttle threshold (with hysteresis)
- **Failsafe** — wings return to neutral on link loss, re-arm on reconnect
- **Direct PWM Output** — drives two wing servos + auxiliary channels
- **Minimal Footprint** — non-essential ELRS modules stripped (Telemetry, Baro, VTX, Thermal, Serial1)

## Build

```bash
pio run -e PteronautOS_ESP8285_2400_RX
```

### Current Footprint (ESP8285)

| Resource | Used | Free |
|---|---|---|
| RAM | 50,640 / 81,920 (61.8%) | 31,280 bytes |
| Flash | 545,456 / 991,216 (55.0%) | 445,760 bytes |

## Architecture

```
Radio Packet → ChannelData[16] → servoCalcAllChannels()
                                      ↓
                              Ornithopter.update()  (≈kHz tick)
                                      ↓
                              servoLeftUs / servoRightUs
                                      ↓
                              PWM Output (GPIOs)
```

### Ornithopter Module (`src/lib/Ornithopter/`)

| File | Lines | Purpose |
|---|---|---|
| `Ornithopter.h` | 51 | Core oscillator — state, timing, flapping/gliding logic |
| `Ornithopter.cpp` | 139 | Engine implementation — waveform generation, channel mixing |
| `OrnithopterConfig.h` | 54 | Compile-time defaults — channel mapping, servo limits, scaling |
| `OrnithopterWaveform.h` | 56 | Waveform library — sin, saw, square, custom shapes |
| `OrnithopterFilter.h` | 35 | Integration bridge — `#ifdef ORNITHOPTER_MODE` guards |

### 5 Integration Points in `devServoOutput.cpp`

| Line | Mechanism | Effect |
|---|---|---|
| 10 | `#include "Ornithopter/OrnithopterFilter.h"` | Zero overhead without `-D ORNITHOPTER_MODE` |
| 110 | `us = orniFilterChannel(ch, us)` in `servoWrite()` | Replaces wing channel values with oscillator output |
| 158 | `ornithopterOnLinkDown()` in `servosEnterFailsafe()` | Wings to neutral on link loss |
| 227–233 | `ornithopterUpdate()` in `servosUpdate()` | Oscillator advances every tick (≈kHz rate) |
| 350 | `ornithopterOnLinkUp()` in `event()` | Arms ornithopter on connection |

### Stripped Modules in `rx_main.cpp`

All guarded with `#ifndef ORNITHOPTER_MODE`:
- AnalogVbat, Baro, VTxSPI, MSPVTx, Thermal
- Serial1, SerialUpdate
- Serial protocols (SBUS, CRSF serial via `OPT_CRSF_RCVR_NO_SERIAL`)

**Kept:** RF link, binding, PWM output, LED, WiFi config, Button, RX LUA.

## Configuration

Edit `src/lib/Ornithopter/OrnithopterConfig.h` to map your CRSF channels and tune flapping parameters:

- Wing servo PWM indices: `ORNITHOPTER_SERVO_LEFT` / `ORNITHOPTER_SERVO_RIGHT`
- 10 CRSF channel assignments: throttle, arm, cadence, ferocity, etc.
- Servo µs limits, flapping/gliding thresholds, amplitude scaling
- Steering differential, elevator/aileron scale, cadence rating

## Credits

Forked from [ExpressLRS](https://github.com/ExpressLRS/ExpressLRS). Ornithopter module ported from [GralhaAzul v1.29.0](https://github.com/dantiel/o-grande-codigo-da-gralha-azul).
