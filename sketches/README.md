# ESP32-S3 Sketches for PteronautOS

Reference Arduino sketches that turn a ~6 USD ESP32-S3 micro board
(Waveshare ESP32-S3-Tiny / -Micro / -Nano or any generic S3 devkit with native
USB) into workbench companions for PteronautOS.

| Folder | Sketch | Personality |
|---|---|---|
| [`esp32s3_hermes/`](esp32s3_hermes/esp32s3_hermes.ino) | **HERMES · the Hermetic Shinobi** | One board, two faces: **FACE I** CRSF→PWM servo converter + **FACE II** PteronautOS pocket flasher, switched by a GPIO0 double-tap. The combined firmware. |
| [`esp32s3_crsf_pwm/`](esp32s3_crsf_pwm/esp32s3_crsf_pwm.ino) | CRSF→PWM servo converter | Standalone converter only (superseded by HERMES FACE I). Reads CRSF (420 000 baud) from any ELRS receiver and drives up to 8 servos with 988–2012 µs pulses, CRC-checked, 500 ms failsafe. |
| [`esp32s3_flash_bridge/`](esp32s3_flash_bridge/esp32s3_flash_bridge.ino) | Pocket flasher | Standalone flasher only (superseded by HERMES FACE II). USB↔UART bridge plus a BOOT-hold + power-cycle jig (P-MOSFET) that drops EP2-class ESP8285 receivers into their bootloader — no FTDI adapter needed. |

## HERMES — quick start

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

- [arduino-esp32](https://github.com/espressif/arduino-esp32) core
  (ESP32-S3 board support)
- HERMES and `esp32s3_crsf_pwm` additionally need the
  [ESP32Servo](https://github.com/jkb-git/ESP32Servo) library (Library Manager).

## Documentation

Full illustrated tutorials live in the docs site:

- Tutorial 06 — [CRSF→PWM Converter](../docs/tutorials/crsf-converter/index.html)
- Tutorial 07 — [ESP32-S3 Toolbox](../docs/tutorials/esp32s3-toolbox/index.html)

## Wiring quick reference

### HERMES (one permanent harness — both faces)
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
