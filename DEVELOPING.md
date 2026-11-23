# Developing PteronautOS

A developer tutorial for people who want to **build, configure, and extend** the
PteronautOS firmware. If you only want to produce a local build to flash onto a
receiver, see the separate [flash.sh tutorial](FLASH-SCRIPT.md) — it wraps
everything below into one command.

PteronautOS is a **fork of ExpressLRS 4.x** turned into a flight controller for
servo-driven ornithopters. The firmware runs on the **ESP8285 @ 80 MHz, 1 MB
flash**, the same hardware ExpressLRS PWM receivers use. This document assumes
you are comfortable with the command line and have a vague idea what an embedded
toolchain is.

---

## 1. Toolchain prerequisites

Follow the **ExpressLRS toolchain installation guide** first — it is the single
authoritative setup for the exact PlatformIO + ESP8266 toolchain this fork uses:

> 🔗 **https://www.expresslrs.org/software/toolchain-install/**

That page installs everything PteronautOS builds on:

| Requirement | Version / notes |
|---|---|
| **PlatformIO Core** | `pio` on `PATH` (installs the ESP8266/Arduino toolchain automatically on first build) |
| **Node.js** | `20.x` required by the WebUI build (`.nvmrc` pins `20.20.2`) — use `nvm use 20` |
| **Git** | to clone the repo |
| **Python 3** | for the image verifier and build scripts |

If you already have PlatformIO installed, verify it:

```bash
pio --version
node --version   # should print v20.x
```

> ⚠️ **Do not skip the ExpressLRS toolchain step.** The 8285 toolchain
> (`espressif8266@4.2.1`) is pinned in `src/targets/common.ini`; PlatformIO will
> pull it on the first `pio run`, but the machine-level prerequisites (Python,
> udev/serial drivers, PATH) come from the guide above.

---

## 2. Getting the code

```bash
git clone https://github.com/dantiel/PteronautOS.git
cd PteronautOS
```

The project is a self-contained PlatformIO project rooted at `src/`. There are
no third-party application libraries outside the PlatformIO `lib_deps` declared
in the target files.

---

## 3. Repository layout

| Path | Purpose |
|---|---|
| `src/platformio.ini` | PlatformIO root — lists the target `.ini` files |
| `src/targets/pteronautos-rx.ini` | **PteronautOS target definitions** (the envs you build) |
| `src/targets/common.ini` | Shared ELRS/ESP8285 platform config |
| `src/lib/Ornithopter/` | Wing/motor mixer — waveform, gearbox, channel filter |
| `src/lib/Zephyrus/` | Gyro (MPU6050/LSM6DS3) + Mesozoic rate/attitude stabilizer |
| `src/lib/ServoOutput/` | `devServoOutput.cpp` — PWM output synced to the CRSF tick |
| `src/lib/WIFI/` | WebUI config server + `PteroWebHandlers.cpp` |
| `src/lib/Mushin/` | EP2 + RP2040 companion bridge |
| `src/html/` | WebUI source (Vite + CoffeeScript + Haml/Lithaml) |
| `src/python/` | Build/pre-build scripts, image verifier |
| `src/src/rx_main.cpp` | Entry point (forked ELRS RX loop) |
| `src/user_defines.txt` | **Your** compile-time overrides (gitignored) |
| `src/user_defines.txt.example` | Template for `user_defines.txt` |
| `scripts/flash.sh` | One-command build + flash wrapper |
| `hermetic.manifest.md` | Project mission, architecture, and **build prohibitions** |

---

## 4. Build order (the one thing that trips people up)

PteronautOS builds in **two stages**, and they must run in this order:

```
1. WebUI  →  src/html/headers/web-pteronautos-rx-8285.h
2. Firmware →  src/.pio/build/<env>/firmware.bin
```

The firmware build runs `src/python/copy_html.py` as a pre-build step, which
**copies the WebUI header into `src/lib/WIFI/WebContent.h`**. If you build the
firmware without building the WebUI first, `copy_html.py` falls back to a stale
(or missing) header and you get an old/broken WebUI baked into the image.

### 4.1 Build the WebUI

```bash
cd src/html
nvm use 20
I18N_LOCALES=en npm run build:pteronautos
```

The `I18N_LOCALES` variable selects which languages are baked in:

```bash
I18N_LOCALES=en        npm run build:pteronautos   # English only (smallest)
I18N_LOCALES=en,de     npm run build:pteronautos   # English + German
I18N_LOCALES=all       npm run build:pteronautos   # all 11 languages
```

> The underlying npm script (`build:pteronautos`) runs syntax + lint checks,
> cleans, then `vite build` with the PteronautOS feature flags. It writes
> `headers/web-pteronautos-rx-8285.h`.

### 4.2 Build the firmware

```bash
cd src
pio run -e PteronautOS_ESP8285_2400_RX
```

The binary lands in `src/.pio/build/PteronautOS_ESP8285_2400_RX/firmware.bin`.

---

## 5. Target environments

Defined in [`src/targets/pteronautos-rx.ini`](src/targets/pteronautos-rx.ini):

| Environment | Description |
|---|---|
| `PteronautOS_ESP8285_2400_RX` | PWMP7, no gyro (Mesozoic rate-only damper compiled but off) |
| `PteronautOS_ESP8285_2400_RX_HORIZON` | PWMP7, gyro + self-level (Mesozoic HORIZON) + baro |
| `PteronautOS_ESP8285_2400_RX_PWMP5` | PWMP5 (5 pads, GPIO5=radio BUSY) |
| `PteronautOS_ESP8285_2400_RX_PWMP5_HORIZON` | PWMP5 + gyro + self-level + baro |
| `PteronautOS_ESP8285_2400_RX_BARO` | Barometer-only (BMP180), no gyro |
| `PteronautOS_ESP8285_2400_RX_GEARBOX` | Gearbox variant (mixer profile 5) |
| `PteronautOS_ESP8285_EP2_2400_RX` | EP2 + RP2040 (MUSHIN companion bridge) |
| `PteronautOS_ESP8285_EP2_2400_RX_PWM` | EP2 standalone, RX/TX as 2 servos |

A normal PWMP7 build with no gyro:

```bash
pio run -e PteronautOS_ESP8285_2400_RX
```

To add gyro self-level, build the `_HORIZON` env instead (it defines
`-D ZEPHYRUS_ENABLED=1 -D MESOZOIC_HORIZON=1`).

---

## 6. Compile-time configuration

There are **two** ways to pass build flags, and they stack:

### 6.1 `src/user_defines.txt` (your overrides — gitignored)

Copy the template and edit it:

```bash
cp src/user_defines.txt.example src/user_defines.txt
```

This file holds the human-tunable knobs — binding phrase, WiFi, gyro, mixer
profile, UART baud. A minimal example:

```
-DAUTO_WIFI_ON_INTERVAL=30
-DLOCK_ON_FIRST_CONNECTION
-DMIXER_PROFILE=1
-DZEPHYRUS_ENABLED=1
-DZEPHYR_IMU=ZEPHYR_IMU_MPU6050
-DMY_BINDING_PHRASE="my-binding-phrase"
```

PlatformIO picks this up automatically (see `build_flags.py`); no CLI flags
needed. This is exactly what `scripts/flash.sh` writes for you.

### 6.2 One-off flags on the command line

```bash
PLATFORMIO_BUILD_FLAGS="-DRegulatory_Domain_EU_CE_2400" \
  pio run -e PteronautOS_ESP8285_2400_RX
```

`flash.sh` uses this channel for the regulatory domain; the rest go through
`user_defines.txt`.

### 6.3 Core tuning headers

For deeper, code-level tuning (channel maps, servo limits, mixer gains), edit
the headers directly:

- `src/lib/Ornithopter/OrnithopterConfig.h` — channel mapping, mixer profile enum, servo limits
- `src/lib/Zephyrus/ZephyrusConfig.h` — I²C, IMU, AHRS/PID, mixer gains

---

## 7. Where to make changes

| You want to… | Edit |
|---|---|
| Change wing/motor mixer behaviour | `src/lib/Ornithopter/OrnithopterGearbox.cpp` (gearbox) or `Ornithopter.cpp` (servo waveform) |
| Add/tune gyro stabilisation | `src/lib/Zephyrus/Zephyrus.cpp` |
| Add a WebUI setting | `src/html/src/pages/*.coffee` + `*.lithaml` + `src/html/src/locales/*.js`, then the JSON in `src/lib/WIFI/PteroWebHandlers.cpp` |
| Change PWM output | `src/lib/ServoOutput/devServoOutput.cpp` |
| Change the RX entry loop | `src/src/rx_main.cpp` |

After changing WebUI source, re-run the WebUI build (§4.1), then the firmware
build (§4.2).

---

## 8. Build rules (non-negotiable)

From [`hermetic.manifest.md`](hermetic.manifest.md). The target is a 1 MB,
80 MHz ESP8285 — every feature pays for itself in RAM/flash:

- **Never** `malloc`/`new` — stack and globals only.
- **Never** `delay()` — it breaks CRSF timing; everything is async.
- **Never** block the main loop.
- **Never** remove the centred failsafe — wings must return to 1500 µs on link loss.
- **Stay under** 65% RAM / 60% Flash (current builds are around 65% RAM — see note below).
- **Never** add GPS / VTX / waypoints / navigation — those belong to OrniFlight, not PteronautOS.

> ℹ️ The ESP8285 build currently sits near **65% RAM**, slightly over the
> manifest's strict guideline — a pre-existing cost of the ELRS WiFi stack +
> Ornithopter + Zephyrus modules. Value-type fields add ~0 bytes; be mindful of
> adding buffers or large tables.

---

## 9. Quick reference

```bash
# Fresh clone → first build (PWMP7, no gyro, English WebUI)
cd src/html && nvm use 20 && I18N_LOCALES=en npm run build:pteronautos
cd ../.. && cd src && pio run -e PteronautOS_ESP8285_2400_RX

# Build with gyro self-level
pio run -e PteronautOS_ESP8285_2400_RX_HORIZON

# Build PWMP5
pio run -e PteronautOS_ESP8285_2400_RX_PWMP5

# Inspect the full target list
pio run --list-targets
```

For the one-command build-and-flash flow (and how `--no-build`, saved presets,
and the bootloader work), read **[FLASH-SCRIPT.md](FLASH-SCRIPT.md)**.
