# EP2 + RP2040: one shared UART

The current motion implementation is described in [Prepared motion](PREPARED_MOTION.md).
It replaces the reduced v1 parameter sender discussed below.

PteronautOS runs on the HappyModel EP2 ESP8285 receiver; Yoshimitsu runs on
the RP2040, which owns the servos and optional gyro. There is **one** full-duplex
UART connection between them, not separate CRSF and MUSHIN connections.

## Wiring (RP2040 Tiny loadout)

| RP2040 | EP2 |
| --- | --- |
| GP0 / UART0 TX | RX / ESP GPIO3 |
| GP1 / UART0 RX | TX / ESP GPIO1 |
| GND | GND |

Use the receiver's specified supply voltage and 3.3 V UART logic. Servo power
must be sized separately. Do not connect another UART transmitter in parallel.
The EP2 target uses its radio BUSY GPIO5 and does not allocate local servo PWM
or an ESP-side I2C gyro on the UART pins.

## Runtime and compatibility

At 420000 baud, a single parser receives standard CRSF RC frames (type 0x16)
and project-private MUSHIN envelopes (type 0x80). The latter carry the complete
existing MUSHIN message, including its inner checksum. The outer CRSF CRC
covers type and payload. Frames are queued whole, with bounded buffers; when
there is insufficient transmit space they are dropped rather than blocked.

Update **both** firmwares together: old raw-MUSHIN/two-UART builds are not
wire-compatible. The envelope is private to this pair, not a newly registered
CRSF extension. MUSHIN v1 still carries the existing reduced, symmetric waveform
model; this transport correction does not establish parity with every newer
PteronautOS waveform parameter.

Loss of RF causes the receiver to send STOP instead of repeatedly renewing an
old flight intent. The companion also retains its local link timeout/failsafe.
Test failsafe with propulsion disabled before operating the aircraft.

## Flashing over the same wires

MEDITATION detaches the servos, changes the shared UART to **115200 baud**,
and bridges native RP2040 USB to ESP UART0 as raw bytes. CRSF, MUSHIN and
console output must not enter that binary stream. BACK_TURNED is receiver-off
and silent; it cannot mirror two UARTs that no longer exist.

Entering the ESP ROM bootloader additionally requires GPIO0 low during reset.
For automatic entry, wire the configured Yoshimitsu BOOT control and receiver
power-switch circuit. Those are control signals, **not a second UART**. Without
them, manually hold EP2 BOOT and power-cycle it. Wait for the boot dance to
complete before starting the flasher. Returning to runtime releases BOOT,
reboots the receiver and restores 420000 baud; after a raw flashing session,
use the physical stance/reset controls rather than injecting console commands.

Build/flash the receiver with:

```sh
./scripts/flash.sh --target ep2 --lang en --baud 115200 --port /dev/your-rp2040-usb-port
```

The target is `PteronautOS_ESP8285_EP2_2400_RX`. The default script target remains
the standalone PWMP7 board. The browser flasher's EP2 + RP2040 selection chooses
the EP2 build; legacy custom MUSHIN pin/baud inputs are no longer used.

Transport tests use host stubs. Hardware validation is still required for RP2040
UART buffering, EP2 power/BOOT timing, USB flashing and actual servo failsafe.
