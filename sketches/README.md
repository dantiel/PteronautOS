# ESP32-S3 Sketches for PteronautOS

Two reference Arduino sketches that turn a ~6 USD ESP32-S3 micro board
(Waveshare ESP32-S3-Tiny / -Micro / -Nano or any generic S3 devkit with native
USB) into workbench companions for PteronautOS.

| Folder | Sketch | Personality |
|---|---|---|
| [`esp32s3_crsf_pwm/`](esp32s3_crsf_pwm/esp32s3_crsf_pwm.ino) | CRSF→PWM servo converter | Reads CRSF (420000 baud) from any ELRS receiver and drives up to 8 servos with 988–2012 µs pulses, CRC-checked, with a 500 ms failsafe. |
| [`esp32s3_flash_bridge/`](esp32s3_flash_bridge/esp32s3_flash_bridge.ino) | Pocket flasher | USB↔UART bridge plus a BOOT-hold + power-cycle jig (P-MOSFET) that drops EP2-class ESP8285 receivers into their bootloader — no FTDI adapter needed. |

## Requirements

- [arduino-esp32](https://github.com/espressif/arduino-esp32) core
  (ESP32-S3 board support)
- `esp32s3_crsf_pwm` additionally needs the
  [ESP32Servo](https://github.com/jkb-git/ESP32Servo) library (Library Manager).

## Documentation

Full illustrated tutorials live in the docs site:

- Tutorial 06 — [CRSF→PWM Converter](../docs/tutorials/crsf-converter/index.html)
- Tutorial 07 — [ESP32-S3 Toolbox](../docs/tutorials/esp32s3-toolbox/index.html)

## Wiring quick reference

### Converter (`esp32s3_crsf_pwm`)
```
receiver TX  → S3 GPIO44   (UART RX)
receiver RX  → S3 GPIO43   (UART TX, unused)
receiver 5V  → shared 5V rail · GND → GND
servo left   → GPIO1 · servo right → GPIO2 · crest rudder → GPIO3
```

### Flash bridge (`esp32s3_flash_bridge`)
```
S3 3V3    ──► S ── P-MOSFET (AO3401) ── D ──► RX 3V3
S3 GPIO10 ──► gate           (10 kΩ pull-up to 3V3; LOW = power ON)
S3 GPIO9  ──► RX BOOT pad
S3 GPIO18 ◄── RX TX · S3 GPIO17 ──► RX RX · S3 GND ──► RX GND
```

### Flashing an EP2-class receiver through the bridge
1. Press the S3 BOOT button **twice** within 1.5 s → the sketch holds BOOT low
   and power-cycles the receiver into its ROM bootloader.
2. Flash with esptool — **always** `--before no_reset`:
   ```bash
   python3 -m esptool --chip esp8285 --port /dev/cu.usbmodemXXXX \
     --baud 115200 --before no_reset write_flash \
     --flash_mode dout --flash_size 1MB --flash_freq 40m \
     0x0 firmware.bin
   ```
3. Press BOOT **once** to restart the receiver and run the new firmware.

> ⚠️ Feed the receiver **3.3 V only** while on the jig, never 5 V. Verify the
> MOSFET orientation with a voltmeter before connecting a receiver.
