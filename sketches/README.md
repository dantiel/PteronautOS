# Sketches for PteronautOS

Reference Arduino sketches that turn a tiny micro board into a workbench companion
for PteronautOS — one that reads CRSF on the bench **and** flashes PteronautOS onto
EP2-class ExpressLRS receivers, with no FTDI adapter.

| Folder | Sketch | Personality |
|---|---|---|
| [`rp2040_tiny_hermes/`](rp2040_tiny_hermes/rp2040_tiny_hermes.ino) | **HERMES · RP2040-Tiny** | One board, two faces for the **Waveshare RP2040-Tiny** (lightest, no onboard button): **FACE I** CRSF→PWM converter + **FACE II** pocket flasher, switched by a USB command + the adapter's RESET. |
| [`esp32s3_hermes/`](esp32s3_hermes/esp32s3_hermes.ino) | **HERMES · ESP32-S3** | The same two faces for any native-USB ESP32-S3 board, switched by a GPIO0 double-tap. |
| [`esp32s3_crsf_pwm/`](esp32s3_crsf_pwm/esp32s3_crsf_pwm.ino) | CRSF→PWM servo converter | Standalone converter only (superseded by HERMES FACE I). Reads CRSF (420 000 baud) from any ELRS receiver and drives up to 8 servos with 988–2012 µs pulses, CRC-checked, 500 ms failsafe. |
| [`esp32s3_flash_bridge/`](esp32s3_flash_bridge/esp32s3_flash_bridge.ino) | Pocket flasher | Standalone flasher only (superseded by HERMES FACE II). USB↔UART bridge plus a BOOT-hold + power-cycle jig (P-MOSFET) that drops EP2-class ESP8285 receivers into their bootloader — no FTDI adapter needed. |

---

## HERMES · RP2040-Tiny — quick start

For the **Waveshare RP2040-Tiny** (or RP2040-Zero — identical pinout). The Tiny has
**no onboard button** — its USB adapter only carries BOOT (BOOTSEL) and RESET (RUN),
which are not readable GPIOs. So HERMES-RP2040 is buttonless:

| Face | Input | Action |
|---|---|---|
| FACE I (boot default) | USB console `FLASHER` | enter FACE II |
| FACE I | USB console `STATUS` | show mode + receiver power |
| FACE II (on entry) | — | auto: hold BOOT + power-cycle → receiver in bootloader |
| FACE II | adapter RESET | reboot → FACE I **and** receiver runs its new firmware |

**Flashing an EP2-class receiver:**

1. Open the USB-CDC serial monitor and type `FLASHER`.
2. **Close the serial monitor** (release the port) once the receiver is in its
   bootloader.
3. Flash through the same port — **always** `--before no_reset`:
   ```bash
   python3 -m esptool --chip esp8285 --port /dev/cu.usbmodemXXXX \
     --baud 115200 --before no_reset write_flash \
     --flash_mode dout --flash_size 1MB --flash_freq 40m \
     0x0 firmware.bin
   ```
4. Press **RESET** on the adapter — the RP2040 reboots into FACE I and power-cycles
   the receiver into its new firmware.

> `--baud` **must** match `BRIDGE_BAUD` (115200). The bridge is pure transparent
> (no line parsing), so esptool's binary SLIP flows untouched.

**Flashing HERMES onto the RP2040-Tiny itself:** hold **BOOT**, tap **RESET**, release
**BOOT** (or hold BOOT while plugging USB) → an `RPI-RP2` drive appears → drag the
`.uf2` onto it.

### RP2040-Tiny wiring (one permanent harness — both faces)

```
RP2040-Tiny 3V3 ──► S ── P-MOSFET (AO3401) ── D ──► RX 3V3
RP2040-Tiny GP6 ──► gate    (10 kΩ pull-up to 3V3; LOW = receiver powered)
RP2040-Tiny GP5 ──► RX BOOT pad (GPIO0, active low)
RP2040-Tiny GP1 ◄── RX TX        (CRSF, FACE I)
RP2040-Tiny GP0 ──► RX RX        (CRSF, unused but wired)
RP2040-Tiny GP9 ◄── RX TX        (flash bridge, FACE II)
RP2040-Tiny GP8 ──► RX RX        (flash bridge, FACE II)
RP2040-Tiny GND ──► RX GND

Servos: left wing → GP2 · right wing → GP3 · crest rudder → GP4
Onboard RGB: GP16 (WS2812B — status if Adafruit_NeoPixel is installed)
```

Exposed GPIOs on the Tiny/Zero: `GP0–GP15` + `GP26–GP29`. `Serial1` = UART0 (GP0/GP1),
`Serial2` = UART1 (GP8/GP9) — the sketch sets these pins explicitly, so any generic
RP2040 board selection works.

> ⚠️ Feed the receiver **3.3 V only** on the jig. Servos run from their own rail,
> never the RP2040's 3V3. Verify MOSFET orientation with a voltmeter first.

---

## HERMES · ESP32-S3 — quick start

HERMES boots in **FACE I** (converter). The BOOT button (GPIO0) is the single
control surface:

| Context | Gesture | Action |
|---|---|---|
| FACE I | double-tap | enter FACE II (flasher) |
| FACE II | single tap | restart the receiver (run new firmware) |
| FACE II | double-tap | drop receiver into bootloader (flash mode) |
| FACE II | long-press (2 s) | return to FACE I |

FACE I USB console: `STATUS` · `FLASHER`. FACE II is a **pure transparent
bridge** (no line parsing) so esptool's SLIP-framed binary traffic passes
untouched.

## Requirements

- **RP2040-Tiny build**: [arduino-pico](https://github.com/earlephilhower/arduino-pico)
  core (Board: "Waveshare RP2040 Zero" or any generic RP2040). No extra library
  required; `Adafruit_NeoPixel` is optional (onboard RGB status).
- **ESP32-S3 build**: [arduino-esp32](https://github.com/espressif/arduino-esp32) core,
  plus the [ESP32Servo](https://github.com/jkb-git/ESP32Servo) library.

## Documentation

Full illustrated tutorials live in the docs site:

- Tutorial 06 — [CRSF→PWM Converter](../docs/tutorials/crsf-converter/index.html)
- Tutorial 07 — [ESP32-S3 Toolbox](../docs/tutorials/esp32s3-toolbox/index.html)

## Wiring quick reference

### HERMES · ESP32-S3 (one permanent harness — both faces)
```
S3 3V3    ──► S ── P-MOSFET (AO3401) ── D ──► RX 3V3
S3 GPIO10 ──► gate           (10 kΩ pull-up to 3V3; LOW = power ON)
S3 GPIO9  ──► RX BOOT pad
S3 GPIO44 ◄── RX TX · S3 GPIO43 ──► RX RX        (FACE I CRSF)
S3 GPIO18 ◄── RX TX · S3 GPIO17 ──► RX RX        (FACE II bridge)
S3 GND     ──► RX GND
servo left → GPIO1 · servo right → GPIO2 · crest rudder → GPIO3
status LED → GPIO21 (optional)
```

### Flashing an EP2-class receiver through HERMES (FACE II)
1. Double-tap BOOT → FACE II.
2. Double-tap BOOT again → receiver drops into its ROM bootloader.
3. Flash with esptool — **always** `--before no_reset`:
   ```bash
   python3 -m esptool --chip esp8285 --port /dev/cu.usbmodemXXXX \
     --baud 115200 --before no_reset write_flash \
     --flash_mode dout --flash_size 1MB --flash_freq 40m \
     0x0 firmware.bin
   ```
4. Single-tap BOOT → receiver restarts and runs the new firmware.
5. Long-press BOOT → back to FACE I (converter).

> ⚠️ Feed the receiver **3.3 V only** while on the jig, never 5 V. Verify the
> MOSFET orientation with a voltmeter before connecting a receiver.