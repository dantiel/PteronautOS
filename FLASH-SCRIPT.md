# flash.sh — Building & Flashing PteronautOS Locally

`scripts/flash.sh` is the one-command counterpart to the browser Flasher. It
walks the **same configuration surface** as the web flasher and the GitHub
Actions cloud build, then builds the WebUI, compiles the firmware, verifies the
image, and flashes it over UART.

It is the fastest way to produce a **local build** with your exact hardware
settings — no browser, no cloud build required.

For the manual (step-by-step) build path, see [DEVELOPING.md](DEVELOPING.md).

---

## 1. Prerequisites

Same toolchain as developing — see
**https://www.expresslrs.org/software/toolchain-install/** and
[DEVELOPING.md](DEVELOPING.md). In short:

- PlatformIO Core (`pio`)
- Node.js 20 (`nvm use 20`)
- Python 3
- An FTDI/serial adapter (or RP2040 bridge) wired to the receiver

---

## 2. Quick start — the interactive wizard

With no flags, the script runs a guided wizard (device → mixer → domain → gyro
→ languages) and then builds + flashes:

```bash
./scripts/flash.sh
```

Everything the wizard asks can also be passed directly as a flag for scripting:

```bash
./scripts/flash.sh --device pwmp5 --mixer 1 --domain eu --gyro none --lang en
```

---

## 3. Common one-liners

```bash
# Flash a PWMP5, 2-wings + crest rudder, no gyro, English WebUI
./scripts/flash.sh --device pwmp5 --mixer 1 --gyro none --lang en

# PWMP7 with rate-only gyro (Mesozoic damper)
./scripts/flash.sh --device pwmp7 --gyro rate

# Self-level (Horizon) gyro, German + English WebUI
./scripts/flash.sh --gyro horizon --lang de,en

# Skip the rebuild and flash the existing firmware.bin
./scripts/flash.sh --device pwmp5 --no-build

# Just probe the bootloader (no build, no flash)
./scripts/flash.sh --check

# List the catalogs
./scripts/flash.sh --list-devices --list-mixers --list-langs
```

---

## 4. Primary flags

### Device (`--device`)

| Key | Hardware |
|---|---|
| `pwmp5` | PWMP5 — 5 local servo pads (GPIO0/1/3/9/10, GPIO5=radio BUSY) |
| `pwmp7` | PWMP7 — 6 local servo pads (GPIO0/1/3/9/10/5) |
| `ep2pwm` | EP2 PWM — standalone, RX/TX (GPIO3/1) as 2 servos |
| `mushin` | EP2 + RP2040 — MUSHIN companion bridge (9 ch) |

### Mixer profile (`--mixer <0-13>`)

| # | Airframe |
|---|---|
| 0 | 2 wings (flapping waveform) |
| 1 | 2 wings + crest rudder |
| 2 | 4 wings (front/back flapping) |
| 3 | V-tail + head rudder (gearbox) |
| 4 | motor + V-tail (gearbox) |
| 5 | motor + V-tail + head rudder (gearbox) |
| 6 | elevator + rudder (gearbox) |
| 7 | motor + elevator + rudder (gearbox) |
| 8 | motor + rudder + 3-servo rotation tail (gearbox) |
| 9 | motor + rudder + 2-servo split elevon tail (gearbox) |
| 10 | motor + rudder + 1-pitch 1-rotate tail (gearbox) |
| 11 | motor + rudder + merged 2-servo tail (gearbox) |
| 12 | Z-Wing 9-PWM (motor + rudder + 4 wing + 3 tail) |
| 13 | motor + merged flat tail (gearbox) |

### Regulatory domain (`--domain`)

| Key | Meaning |
|---|---|
| `eu` | EU CE 2400 — LBT (listen-before-talk, limited) |
| `ism` | ISM 2400 — full ISM band |

### Gyro (`--gyro`)

| Key | Effect |
|---|---|
| `none` | no gyro (default) |
| `rate` | rate-only damper (Mesozoic) |
| `horizon` | rate + self-level (Mesozoic HORIZON) |

Legacy aliases still work: `--mode none|rate|horizon` and `--horizon`.

### IMU chip (`--imu`)

| Key | Sensor board |
|---|---|
| `mpu6050` | GY-521 (MPU6050) — default |
| `lsm6ds3` | GY-LSM6DS3 (LSM6DS3) |

---

## 5. Detail flags

```bash
--binding-phrase <s>   # hardcoded binding phrase (empty = traditional bind)
--wifi-interval <n>    # WiFi auto-on seconds (-1 disables, default 30)
--device-name <s>      # device hostname
--wifi-ssid <s>        # home WiFi SSID (auto-join)
--wifi-pass <s>        # home WiFi password
--i2c-sda <gpio>       # IMU SDA (default 1)
--i2c-scl <gpio>       # IMU SCL (default 3)
--rotation <0-6>       # IMU board rotation
--pre-detect <0|1>     # probe SCL before I²C init (default 1)
--uart-baud <rate>     # CRSF UART baud (default 420000)
--baro <0|1>           # BMP180 barometer + altitude hold (shares gyro I²C bus)
```

### WebUI languages (`--lang` / `-l`)

```bash
--lang en        # English only (smallest bundle)
--lang en,de     # English + German
--lang all       # all 11 languages
# omit --lang → interactive pick (or reuse saved settings)
```

Available codes: `en pt de es fr hi ja ko ru zh ar`.

---

## 6. Saved config presets

The script can persist a whole configuration to a **gitignored** file (safe for
binding phrases / WiFi passwords):

```bash
# Save this run's config (prompts for a name)
./scripts/flash.sh --device pwmp5 --gyro none --save

# Save under a named preset
./scripts/flash.sh --device pwmp7 --gyro rate --save=turbo

# Reuse a saved preset (flags override saved defaults)
./scripts/flash.sh --conf=turbo --lang all

# Delete a saved preset
./scripts/flash.sh --conf=turbo --reset
```

Files live at `.pteronautos-flash-<name>.conf` (the default is
`.pteronautos-flash.conf`). Without `--conf`, a saved default is offered
interactively ("Reuse saved settings?").

---

## 7. `--no-build` and image verification

`--no-build` skips the rebuild and flashes the **existing** `firmware.bin`:

```bash
./scripts/flash.sh --device pwmp5 --no-build
```

Before flashing, the script runs `src/python/verify_pteronautos_image.py`:

- **Fresh build** (`BUILD=1`) — the `.bin` must exactly match the flags you just passed (including the gyro on/off PWM map).
- **`--no-build`** (`BUILD=0`) — the script **trusts the embedded hardware map** in the bin (added with `--trust_embedded`), so it no longer complains about a mismatch just because your *current* flags differ from what the bin was last compiled with.

One guard is always enforced: the **`--expect_product`** board-type check is
derived from `--device`, so you can't accidentally flash a PWMP7 image onto a
PWMP5.

> ℹ️ If a fresh build fails with `Hardware differs from project map`, it means
> the bin on disk was built with a **different gyro setting** than your current
> flags — rebuild (drop `--no-build`) so the image matches, or keep the bin and
> pass the matching `--gyro` flag.

---

## 8. Flashing (bootloader)

The script locates `esptool` and the USB port, then writes the firmware. It
assumes the receiver is **already in bootloader mode**:

1. **Hold BOOT (GPIO0)** on the receiver.
2. **Power-cycle** the receiver (unplug its 3.3 V/USB → plug back in).
3. Keep holding ~1 s, then release.

Then run the script. The flash happens over a single connection with three
retry attempts.

**Tips:**

- Disconnect servos / external load from the 3.3 V rail while flashing — the ESP draws more current and can brown out.
- If flashing is flaky, drop the baud: `--baud 9600`.
- After a successful flash, **unplug and re-plug** the receiver (do *not* hold GPIO0) for a normal boot.
- To check the chip is alive without flashing: `./scripts/flash.sh --check`.

---

## 9. Where the build output lands

| Artifact | Path |
|---|---|
| WebUI header | `src/html/headers/web-pteronautos-rx-8285.h` |
| Firmware binary | `src/.pio/build/<target-env>/firmware.bin` |
| Your build flags | `src/user_defines.txt` (written by the script, gitignored) |

---

## 10. What a typical run looks like

```text
$ ./scripts/flash.sh --device pwmp5 --mixer 1 --gyro none --lang en
Configuration
  device        : pwmp5
  mixer         : 1  · 2 wings + crest rudder
  domain        : EU_CE_2400
  gyro          : none
  wifi interval : 30s
  target env    : PteronautOS_ESP8285_2400_RX

==> Building WebUI
==> Building firmware (PteronautOS_ESP8285_2400_RX)
==> Locating a working esptool
==> Locating USB serial port
==> Locating firmware binary
==> Flashing firmware
✓ Flash complete.
```
