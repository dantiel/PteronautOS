# Sketches for PteronautOS

Reference Arduino sketches that turn one tiny board into a workbench companion for
PteronautOS — reading CRSF on the bench **and** flashing PteronautOS onto EP2-class
ExpressLRS receivers, with no FTDI adapter.

**YOSHIMITSU · the Hermetic Shinobi** is **one sketch, two targets, six stances**.
There is no second sketch, no standalone half-solution, no stray file — only the
complete solution. A single `.ino` builds for both boards. The folder follows the
**Gralha Azul strategy**: the sketch is only a thin config shell, the whole core
lives in `src/Yoshimitsu.h`, every default in `src/Yoshimitsu_Padraos.h` — a
library dongle you update without re-touching config (see below).

| Folder | Sketch | Personality |
|---|---|---|
| [`yoshimitsu/`](yoshimitsu/yoshimitsu.ino) | **YOSHIMITSU** | One sketch for the **Waveshare RP2040-Tiny** (lightest, no onboard button) **and** any native-USB **ESP32-S3**. Six stances, switched by USB command, GPIO0 button (S3), or RESET-tap (RP2040). |

Yoshimitsu is a ronin. He never looks back — `BACK_TURNED` only ever opens a
deceptive follow-up, never a retreat. Configure once, flash once, never touch again.

### The library dongle — update without re-touching config

| File | Role |
|---|---|
| [`yoshimitsu/yoshimitsu.ino`](yoshimitsu/yoshimitsu.ino) | **Config shell only** — pins, flags, the `BOARD_CUSTOM` block. |
| [`yoshimitsu/src/Yoshimitsu.h`](yoshimitsu/src/Yoshimitsu.h) | **The whole core** — stances, CRSF, MUSHIN, JIGUANG, the flasher bridge. |
| [`yoshimitsu/src/Yoshimitsu_Padraos.h`](yoshimitsu/src/Yoshimitsu_Padraos.h) | **Every default** (pins, timings, gate levels) as `*_PADRAO` values. |
| [`yoshimitsu/library.properties`](yoshimitsu/library.properties) | Arduino library manifest (v1.0.0, rp2040 + esp32). |

Install once as an Arduino library (symlink or copy into `~/Documents/Arduino/libraries/`):

```bash
ln -s "$PWD/sketches/yoshimitsu" ~/Documents/Arduino/libraries/Yoshimitsu
```

Override anything in the sketch by defining it **before** the `#include <Yoshimitsu.h>`.
To update the dongle: `git pull` (or drop in a new release) — your config sketch stays
untouched. A firmware update is never a re-wire and never a re-configure.

---

## The six stances

One board, six poses — each a Manji-scroll stance with a workbench meaning:

| Stance | Function |
|---|---|
| **KINCHO** | CRSF → PWM servo converter. The sword stance: its parry is the CRC check that discards every corrupt frame. Pure channel→servo mapping — **no channel names**, no gyro. |
| **MANJI_DRAGONFLY** | Converter + **Zephyrus gyro link**. The levitation: reads an optional MPU6050 over I2C and feeds a yaw-rate correction into the crest servo. The standard pose for an ornithopter **with** a gyro. Without a gyro it degrades gracefully into KINCHO. |
| **FLEA** | The power-cycle jig. It balances on the sword-hilt to evade low attacks — holds `RX_BOOT` and power-cycles the receiver so an EP2-class ESP8285 drops into its ROM bootloader, then settles into MEDITATION. |
| **MEDITATION** | The pocket flasher. The sponge-head sits cross-legged, saving energy (servos detached, LED dimmed) — but it is **ready to be flashed**: a pure transparent USB↔UART bridge carries esptool's binary SLIP untouched. |
| **NSS** | No-Sword bench. Blade sheathed: direct servo PWM over USB (`SERVO i us`), no RF, no receiver power. |
| **BACK_TURNED** | The deceptive idle. It turns its back on every other stance and looks dead — servos centred, LED near-dark. The delusional follow-up: it silently cross-wires the two UARTs as a living mirror, so you can probe your wiring by sending bytes. |

The CRSF converter part is deliberately **channel-agnostic**: servos are just
`SERVO_PIN_1..8`, and which CRSF channel feeds each servo lives in the
`CHANNEL_TO_SERVO` table. Channel *meaning* belongs to the mixer/kernel, never to
the board.

---

## Switching stances

**USB console** — the canonical, reliable path on both boards:

```
KINCHO | MANJI | FLEA | MEDITATION | NSS | BACK | POSE <n> | STATUS | HELP
SERVO i us     (NSS only — drive servo i to microseconds)
MUSHIN         muscle-memory mode (無心) report  — companion compute
MUSHIN ON/OFF  arm / disarm the no-mind bridge (persisted in flash)
```

- **ESP32-S3** (GPIO0 BOOT button): double-tap cycles KINCHO → MANJI → NSS → BACK;
  long-press → MEDITATION. In MEDITATION/FLEA: tap = restart receiver, double-tap =
  FLEA jig, long-press = back to KINCHO.
- **RP2040-Tiny** (no readable button — BOOT = BOOTSEL, RESET = RUN): quick **RESET
  taps**, counted in flash-backed EEPROM against the AON RTC, advance the stance
  (1 = MANJI · 2 = NSS · 3 = BACK · 4 = MEDITATION · 5 = KINCHO). The adapter's BOOT
  is boot-strapping only; RESET reboots to KINCHO.

---

## Pin map — edit here

Both targets carry a prominent **PIN MAP — EDIT HERE** block right after the header.
UART0 and UART1 are exposed on **different pins**; the sketch sets them explicitly,
validates them at boot (range + collision POST), and prints the active wiring in the
boot banner and `STATUS`.

### RP2040-Tiny (one permanent harness)

```
RP2040-Tiny 3V3 ──► S ── P-MOSFET (AO3401) ── D ──► RX 3V3
RP2040-Tiny GP6 ──► gate     (10 kΩ pull-up to 3V3; LOW = receiver powered)
RP2040-Tiny GP5 ──► RX BOOT pad (GPIO0, active low)
RP2040-Tiny GP1 ◄── RX TX    (CRSF, UART0)
RP2040-Tiny GP0 ──► RX RX    (CRSF, wired for completeness)
RP2040-Tiny GP9 ◄── RX TX    (flash bridge, UART1)
RP2040-Tiny GP8 ──► RX RX    (flash bridge, UART1)
RP2040-Tiny GND ──► RX GND

Servos:  SERVO_PIN_1 → GP2 · SERVO_PIN_2 → GP3 · SERVO_PIN_3 → GP4
Gyro:    MPU6050 SDA → GP10 · SCL → GP11   (MANJI_DRAGONFLY, optional)
RGB:     GP16 (onboard WS2812B — stance colours if Adafruit_NeoPixel present)
```

### ESP32-S3 (one permanent harness)

```
S3 3V3    ──► S ── P-MOSFET (AO3401) ── D ──► RX 3V3
S3 GPIO10 ──► gate           (10 kΩ pull-up to 3V3; LOW = power ON)
S3 GPIO9  ──► RX BOOT pad
S3 GPIO44 ◄── RX TX · S3 GPIO43 ──► RX RX        (CRSF, UART1)
S3 GPIO18 ◄── RX TX · S3 GPIO17 ──► RX RX        (bridge, UART2)
S3 GND     ──► RX GND
servos → GPIO1..8   ·   gyro SDA → GPIO15 · SCL → GPIO16   ·   status LED → GPIO21
```

> ⚠️ Feed the receiver **3.3 V only** on the jig. Servos run from their own rail,
> never the board's 3V3. Verify MOSFET orientation with a voltmeter first.

---

## Flashing an EP2-class receiver through YOSHIMITSU

1. Enter the flasher: type `FLEA` (or `MEDITATION` then `FLEA`), or on the S3
   double-tap BOOT. FLEA drops the receiver into its ROM bootloader and settles
   into MEDITATION.
2. **Close the serial monitor** (release the port) once the receiver is in its
   bootloader.
3. Flash through the same port — **always** `--before no_reset`:

   ```bash
   python3 -m esptool --chip esp8285 --port /dev/cu.usbmodemXXXX \
     --baud 115200 --before no_reset write_flash \
     --flash_mode dout --flash_size 1MB --flash_freq 40m \
     0x0 firmware.bin
   ```

4. Exit: long-press BOOT (ESP32-S3) or press RESET (RP2040) — the board returns to
   KINCHO and the receiver runs its new firmware.

> `--baud` **must** match `BRIDGE_BAUD` (115200). The bridge is pure transparent
> (no line parsing), so esptool's binary SLIP flows untouched.

**Flashing YOSHIMITSU onto the RP2040-Tiny itself:** hold **BOOT**, tap **RESET**,
release **BOOT** (or hold BOOT while plugging USB) → an `RPI-RP2` drive appears →
drag the `.uf2` onto it.

---

## Requirements

- **RP2040-Tiny build**: [arduino-pico](https://github.com/earlephilhower/arduino-pico)
  core (Board: "Waveshare RP2040 Zero" or any generic RP2040). `Adafruit_NeoPixel` is
  optional (onboard RGB status). `EEPROM` + AON `hardware/rtc` for the RESET-tap
  counter ship with the core.
- **ESP32-S3 build**: [arduino-esp32](https://github.com/espressif/arduino-esp32)
  core, plus [ESP32Servo](https://github.com/jkb-git/ESP32Servo).
- **Gyro (optional)**: any MPU6050 on `GYRO_SDA`/`GYRO_SCL` enables MANJI_DRAGONFLY;
  set `YOSHI_GYRO=0` to compile it out.

## Build verification — honest status

The single sketch is **syntax-checked against host stubs** (`tools/stub/`, a minimal
Arduino API surface) for both targets and every board/gyro variant, zero diagnostics:

```bash
clang++ -fsyntax-only -std=gnu++17 -I tools/stub -I sketches/yoshimitsu/src \
  -DARDUINO_ARCH_RP2040 -x c++ sketches/yoshimitsu/yoshimitsu.ino
clang++ -fsyntax-only -std=gnu++17 -I tools/stub -I sketches/yoshimitsu/src \
  -DARDUINO_ARCH_ESP32  -x c++ sketches/yoshimitsu/yoshimitsu.ino
```

The compile-time UART guards are proven by `tools/yoshi_guard_test.py`. Pin range
and collision checks run as a **boot POST** (`validatePins()`) and halt with a clear
message before any wire is touched.

It was **not** compiled against the real arduino-pico / arduino-esp32 cores on the
machine that wrote it, and **not** bench-tested on hardware. Before first use: verify
the MOSFET polarity with a voltmeter (gate LOW must power the receiver) and run one
esptool handshake against a receiver on the jig. The PIN MAP block and boot POST exist
precisely so the first flash is also the last — once the harness is verified, nothing
needs to be touched again.

## Documentation

Full illustrated tutorial lives in the docs site:

- Tutorial 06 — [YOSHIMITSU · the Complete Solution](../docs/tutorials/yoshimitsu/index.html)