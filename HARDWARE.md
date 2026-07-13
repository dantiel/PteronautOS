# PteronautOS — Hardware Reference

> *Board-specific wiring guides, pin mappings, voltage considerations, and servo compatibility for PteronautOS ornithopter firmware.*

---

## Supported Boards

| Board | MCU | Radio | Guide |
|-------|-----|-------|-------|
| **Generic 2400 PWMP7 v1.1** | ESP8285 @ 80MHz | SX1280 2.4GHz | [§1 — PWMP7 v1.1](#1-generic-2400-pwmp7-v11) |
| *More coming* | — | — | *(PRs welcome)* |

---

## 1. Generic 2400 PWMP7 v1.1

> ⚠️ **This guide is for PCB revision 1.1** — GPIO2 and GPIO5 are **not** broken out to pads. All I²C wiring uses CH2/CH3 breakouts instead. See [§1.7](#17-v11-vs-other-revisions) for revision differences.

### 1.1 Board Overview

```
                    ┌──────────────────────────────┐
                    │    SX1280 Radio Module        │
                    │   (SPI: GPIO12-15, IRQ: 4)    │
                    │                               │
    + ──────────────┤  POWER RAIL (+ and - pads)    │
    - ──────────────┤  → AMS1117-3.3V regulator     │
                    │  → All servo headers          │
                    │                               │
  CH1 (GPIO0/BOOT) ─┤ ← Button shared              │
  CH2 (GPIO1/TX)   ─┤ ← UART TX + I²C SDA          │
  CH3 (GPIO3/RX)   ─┤ ← UART RX + I²C SCL          │
  CH7 (GPIO9)      ─┤                               │
  (unlabeled)       ┤── GPIO10                      │
  LED  (GPIO16)    ─┤ ← Digital only                │
                    └──────────────────────────────┘
```

### 1.2 Complete Pin Map

| Silkscreen | GPIO | Primary | PteronautOS Role | Shared? |
|-----------|------|---------|------------------|---------|
| **CH1** | 0 | PWM + Button | Left Wing Servo + BOOT button | ✅ |
| **CH2** | 1 | PWM + UART TX | Right Wing Servo + I²C SDA | ✅ |
| **CH3** | 3 | PWM + UART RX | Crest Rudder + I²C SCL | ✅ |
| *(none)* | 2 | *(not exposed)* | — | — |
| *(none)* | 4 | SX1280 DIO1 IRQ | Radio interrupt | 🔒 Hard-reserved |
| *(none)* | 5 | *(not exposed)* | — | — |
| **CH7** | 9 | PWM | Aux Servo 4 | ✅ Free |
| *(none)* | 10 | PWM | Aux Servo 5 | ✅ Free |
| *(none)* | 12 | SPI MISO | Radio bus | 🔒 Hard-reserved |
| *(none)* | 13 | SPI MOSI | Radio bus | 🔒 Hard-reserved |
| *(none)* | 14 | SPI SCK | Radio bus | 🔒 Hard-reserved |
| *(none)* | 15 | SPI NSS | Radio bus | 🔒 Hard-reserved |
| **LED** | 16 | Digital out | Status LED | ✅ Free |
| *(none)* | 17 | VBAT ADC | Voltage monitor | ✅ Free |

### 1.3 Servo Channel Assignment

| Logical Ch | GPIO | Silkscreen | Role | Notes |
|-----------|------|-----------|------|-------|
| **Ch1** | 0 | CH1 | Left wing servo | Shared with BOOT button |
| **Ch2** | 1 | CH2 | Right wing servo | Shared with FTDI TX / I²C SDA |
| **Ch3** | 3 | CH3 | Crest rudder servo | Shared with FTDI RX / I²C SCL |
| **Ch4** | 9 | CH7 | Aux / spare | Free |
| **Ch5** | 10 | *(unlabeled)* | Aux / spare | May need to solder wire |

### 1.4 MPU6050 Wiring (GY-521 Breakout)

```
     GY-521                    PWMP7 v1.1
     ──────                    ──────────
     ┌──────┐                  ┌──────────────┐
     │ VCC  │──────────────────│  +  (5V rail) │
     │ GND  │──────────────────│  -  (GND)     │
     │ SDA  │──────────────────│  CH2 (GPIO1)  │
     │ SCL  │──────────────────│  CH3 (GPIO3)  │
     │ AD0  │  (leave open)    │               │
     │ INT  │  (leave open)    │               │
     │ XDA  │  (leave open)    │               │
     │ XCL  │  (leave open)    │               │
     └──────┘                  └──────────────┘
```

**Four wires total.** No trace cutting, no extra regulators.

The GY-521 has onboard **4.7kΩ I²C pull-up resistors** and an **AMS1117-3.3V regulator** — feed it 5V on VCC, the module generates its own 3.3V. I²C logic levels auto-match.

### 1.5 FTDI Flashing Pinout

```
     USB-UART (FTDI)            PWMP7 v1.1
     ────────────────            ──────────
     ┌────────┐                  ┌──────────────┐
     │ TX     │──────────────────│  CH3 / RX     │  (GPIO3)
     │ RX     │──────────────────│  CH2 / TX     │  (GPIO1)
     │ GND    │──────────────────│  -  (GND)     │
     │        │                  │               │
     │ 5V/VCC │  ← DO NOT CONNECT (power separately)
     └────────┘                  └──────────────┘
```

> ⚠️ **Use 3.3V logic FTDI only.** ESP8285 GPIOs are not 5V-tolerant.
> ⚠️ **Power the board from its own 5V supply** — do not feed 5V from the FTDI into the `+` rail. The AMS1117 regulator handles its own power.

### 1.6 Workflow

```
┌──────────────────────────────────────────────────────┐
│                    FLASH MODE                         │
│                                                      │
│  FTDI ──→ CH2 (TX), CH3 (RX), GND                    │
│  MPU  ──→ DISCONNECTED (remove plug from CH2/CH3)    │
│  Power → 5V to + rail                                │
│  Hold BOOT (CH1 button), power on, release            │
│                                                      │
│  esptool.py write_flash 0x0 firmware.bin              │
└──────────────────────────────────────────────────────┘
                         │
                         ▼
┌──────────────────────────────────────────────────────┐
│                    FLY MODE                          │
│                                                      │
│  FTDI  ──→ DISCONNECTED                              │
│  MPU   ──→ PLUGGED onto CH2 (SDA), CH3 (SCL)         │
│  Power → 5V (or 2S) to + rail                        │
│                                                      │
│  Boot → Zephyrus auto-detects MPU → gyro active      │
└──────────────────────────────────────────────────────┘
```

**One plug, two pins.** The CH2/CH3 pads pull double duty: FTDI data during flash, I²C data during flight. Unplug, flash, replug — 5 seconds.

### 1.7 v1.1 vs Other Revisions

| Feature | v1.0 | **v1.1** |
|---------|------|----------|
| GPIO2 (SCL) broken out | ✅ Pad exposed | ❌ Not exposed |
| GPIO5 (SDA) broken out | ✅ Pad exposed | ❌ Not exposed |
| I²C on dedicated pads | ✅ Yes | ❌ Must use CH2/CH3 |
| FTDI + MPU simultaneous | ✅ Yes | ❌ Unplug to flash |
| Servos | 5 channels | 5 channels |

> If you have a v1.0 board where GPIO2 and GPIO5 are exposed, you can use them directly and leave the FTDI connected. The build flags in `pteronautos-rx.ini` would need to change to `-D ZEPHYR_I2C_SDA=5 -D ZEPHYR_I2C_SCL=2`.

### 1.8 Voltage

The `+` and `-` pads are a **shared power rail** — they feed:
- The AMS1117-3.3V regulator (powers ESP8285 + SX1280)
- Every servo header
- The GY-521 (which has its own onboard AMS1117-3.3V)

| Input | Regulator | Servos | Verdict |
|-------|-----------|--------|---------|
| **3.7V (1S LiPo)** | Drops out — brownout | Underpowered | ❌ Unusable |
| **5V (BEC / 2S LiFe)** | ✅ AMS1117 happy | ✅ Standard servos happy | ✅ **Recommended** |
| **7.4V (2S LiPo)** | ✅ AMS1117 rated to 15V | ⚠️ Need HV servos | ⚠️ Check servo rating |
| **8.4V (2S HV)** | ✅ AMS1117 rated to 15V | ⚠️ Need HV servos | ⚠️ Check servo rating |
| **12.6V (3S)** | ✅ AMS1117 rated to 15V | ❌ Most servos smoke | ❌ Don't |

> **Regulator spec:** AMS1117-3.3 has a 15V absolute maximum input. 2S (8.4V) is well within limits. At 8.4V the regulator dissipates ~1W — warm but fine.

### 1.9 Servo Compatibility

#### Power Voltage

| Servo Type | Max Voltage | 5V | 2S (7.4V) | 2S HV (8.4V) |
|-----------|-------------|-----|-----------|---------------|
| Standard analog | 6.0V | ✅ | ❌ Smoke | ❌ Smoke |
| Standard digital | 6.0V | ✅ | ❌ Smoke | ❌ Smoke |
| HV digital | 8.4V | ✅ | ✅ | ✅ |
| HV coreless | 8.4V | ✅ | ✅ | ✅ |

#### Logic Level (PWM Signal)

ESP8285 outputs **3.3V PWM logic**. Most modern digital servos accept logic levels down to 2.7V — including HV models. Check your servo datasheet:

| Servo | Logic LOW max | Logic HIGH min | 3.3V Compatible? |
|-------|---------------|-----------------|-------------------|
| EMAX ES08MA II | 0.7V | 2.0V | ✅ |
| EMAX ES09MD | 0.7V | 2.0V | ✅ |
| KST X06 | 0.7V | 2.5V | ✅ |
| KST X08 | 0.7V | 2.5V | ✅ |
| Older analog (Futaba) | 0.7V | 4.0V | ❌ Needs level shifter |

> ⚠️ **If using older analog servos** with 4V+ logic thresholds: add a 74HCT125 level shifter between the ESP8285 PWM pin and the servo signal wire. Or use modern digital servos — they all accept 3.3V logic.

### 1.10 Build Flags

Current I²C configuration in `src/targets/pteronautos-rx.ini`:

```ini
build_flags =
    -D ORNITHOPTER_MODE=1
    -D PTERONAUTOS=1
    -D ZEPHYRUS_ENABLED=1
    -D ZEPHYR_I2C_SDA=1      # GPIO1 → CH2 breakout
    -D ZEPHYR_I2C_SCL=3      # GPIO3 → CH3 breakout
```

> **To use a v1.0 board** (GPIO2/GPIO5 exposed): change to `-D ZEPHYR_I2C_SDA=5 -D ZEPHYR_I2C_SCL=2`.

### 1.11 First Flash — Critical Notes

1. **Full chip erase required** — factory ELRS hardware.json survives in LittleFS otherwise.
   ```bash
   esptool.py --chip esp8266 erase_flash
   ```
2. **After erase, upload hardware.json** via Web UI at `http://10.0.0.1/hardware.html` to enable PWM outputs.
3. **Cross-flash from ELRS 3.x must be UART** — Wi-Fi fails with `ERROR[4]: Not Enough Space`.
4. **Subsequent PteronautOS→PteronautOS updates work via Wi-Fi.**

### 1.12 Current Build Stats

| Resource | Used | Free | % |
|----------|------|------|---|
| RAM | ~51 KB | ~31 KB | 62.7% |
| Flash | ~555 KB | ~436 KB | 56.0% |

---

## 2. GY-521 MPU6050 Module

The GY-521 is a breakout board for the InvenSense MPU6050 6-axis IMU. It is the recommended gyro module for PteronautOS.

| Property | Value |
|----------|-------|
| **Chip** | MPU6050 (WHO_AM_I = 0x68) |
| **I²C Address** | 0x68 (AD0 pulled LOW on-board) |
| **VCC** | 3.3V–5V (onboard AMS1117-3.3V regulator) |
| **Pull-ups** | 4.7kΩ on SDA + SCL (on-board) |
| **Required Pins** | VCC, GND, SDA, SCL (4 wires) |
| **Unused pins** | AD0, INT, XDA, XCL — leave disconnected |

### Orientation Reference

For the Zephyrus gyro to correct in the right direction, the GY-521 must be mounted with a known orientation relative to the ornithopter frame:

```
     FRONT OF ORNITHOPTER
          ▲
          │
    ┌─────┴─────┐
    │  GY-521   │  ← Y-axis points forward
    │  ┌──────┐ │     X-axis points right
    │  │ CHIP │ │     Z-axis points down (into board)
    │  └──────┘ │
    └───────────┘
```

> ⚠️ **Verify in flight test:** if rudder correction goes the wrong direction, negate `ZEPHYR_RUDDER_ROLL_GAIN` and `ZEPHYR_RUDDER_YAW_GAIN` in `ZephyrusConfig.h`.

---

## 3. Adding a New Board

To contribute a board-specific guide:

1. Fork the `PteronautOS` repository
2. Add a new section to this file following the PWMP7 v1.1 template
3. Include: pin map table, wiring diagram (ASCII art or photo), voltage notes, servo compatibility, build flags
4. Add photos to `docs/assets/images/` and link them
5. Submit a PR

### Template Checklist

- [ ] Board photo (top + bottom)
- [ ] Complete GPIO table (silkscreen → GPIO → PteronautOS role)
- [ ] Wiring diagram for MPU6050 + FTDI
- [ ] Voltage limits (regulator part number + max input)
- [ ] Servo logic level compatibility
- [ ] Build flags (pins, any board-specific defines)
- [ ] Flash procedure (any board-specific quirks)
- [ ] Known limitations

---

## 4. Quick Reference Card

### Pin Cheat Sheet (PWMP7 v1.1)

```
I²C:  SDA=CH2  SCL=CH3  (unplug MPU to flash)
PWM:  L-wing=CH1  R-wing=CH2  Rudder=CH3  Aux=CH7  Aux=GPIO10
LED:  GPIO16 (digital only)
BOOT: Hold CH1 button during power-on
```

### Voltage Cheat Sheet

```
5V BEC → + rail → everything happy → recommended
2S LiPo → + rail → need HV servos → check rating
3S+ → never connect → servos explode
```

### Flash Cheat Sheet

```bash
# First install (UART only)
esptool.py --chip esp8266 erase_flash
esptool.py --chip esp8266 write_flash 0x0 firmware.bin

# Subsequent (WiFi)
# → http://10.0.0.1 → upload firmware.bin
```

---

*PteronautOS — Fly Natural. Control Precise.*
