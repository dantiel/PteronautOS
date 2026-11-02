# 🥚 Pteronaut-Egg

A **minimal WiFi bootstrap** for the ESP8285 (1 MB flash). Its only job: accept a
full PteronautOS image over WiFi and stage it via `ESP8266 Updater` (`U_FLASH`).
No CRSF, no WebUI, no ArduinoJson, no littlefs — just AP mode + a raw TCP server
+ the core `Updater` class.

## Why the egg exists

The 8285 has a single sketch region of **991 216 B** (linker
`elrs.flash.1m.ld`). A full PteronautOS image is **636 064 B**. A running image
can therefore only stage a new image that fits in its *free* staging space:

```
free space = 995 328 − (running size + 4096 rounded)
```

| running | free space | can stage |
|---|---|---|
| **Egg** (291 KB) | **677 KB** | Full (636 KB) ✅ |
| **Full** (636 KB) | **348 KB** | Egg (291 KB) ✅ |

The egg is **small enough** (297 743 B ≈ 291 KB, target ≤ 320 KB) that the cycle
**Egg → Full → Egg** closes indefinitely — entirely over WiFi, no FTDI, no
RP2040 companion.

## Building

```sh
pio run -d egg
# → egg/.pio/build/egg/firmware.bin  (~291 KB)
```

The egg uses the exact same linker as PteronautOS (`elrs.flash.1m.ld`), so
littlefs/eeprom/rfcal/wifi land at the same addresses across a `U_FLASH`
switch. Keep both files **byte-identical** — every change goes in both
(`src/elrs.flash.1m.ld` and `egg/elrs.flash.1m.ld`).

## One-time first install

The egg itself needs **one** UART flash (it is not a firmware update — it is the
bootstrap):

```sh
# FTDI, or the Yoshimitsu companion ('bl' → ESP.rebootIntoUartDownloadMode())
esptool.py -p <port> -b 460800 write_flash 0x0 egg/.pio/build/egg/firmware.bin
```

## The two-phase cycle (WiFi-only afterwards)

1. **Egg runs** → opens AP `PTERONAUT-EGG` (password `hatchnatural`) →
   `http://pteronaut-egg.local` (fallback `http://192.168.4.1`) →
   drag `firmware.bin` (636 KB) onto the ASCII egg (drag & drop) or click it →
   the egg stages and reboots into full PteronautOS.
2. **Full runs** → existing WebUI → Firmware Update → upload `egg.bin` (277 KB)
   ("Flash Anyway" on target mismatch) → reboots into the egg.

Per update: **2 uploads, ~913 KB WiFi traffic, zero hardware.**

## Brick risk (honest)

As with any OTA: flashing a **broken** full image (no more WiFi) over the egg
means you need UART again. The egg does not lower the risk below OTA level — it
only removes the cable from the *normal* update path.

## Security

The AP is password-protected (`hatchnatural`). Since an open AP would let anyone
in range flash firmware, a password is mandatory. Change it in `src/main.cpp`
(`AP_SSID` / `AP_PASS` / `AP_HOST`) if needed.

## mDNS

The egg answers at `http://pteronaut-egg.local` (Bonjour/mDNS). Fallback when
the client has no mDNS (e.g. some Android): `http://192.168.4.1`.
