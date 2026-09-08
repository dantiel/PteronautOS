// =============================================================================
//  YOSHIMITSU · the Hermetic Shinobi — ESP32-S3 build
//  One board, two faces.
// -----------------------------------------------------------------------------
//  A single ESP32-S3 (Waveshare Tiny/Micro/Nano or any native-USB devkit) that
//  transmutes between two workbench personalities:
//
//    FACE I  — CRSF → PWM servo converter
//              Reads ExpressLRS CRSF RC frames (420 000 baud) from any ELRS
//              receiver and drives up to 8 servos with 988–2012 µs pulses.
//              CRC-checked, failsafe-centred. This is the "fly" face.
//
//    FACE II — PteronautOS pocket flasher
//              USB ↔ UART bridge plus a BOOT-hold + power-cycle jig
//              (P-MOSFET) that drops EP2-class ESP8285 receivers into their
//              ROM bootloader — no FTDI adapter, no soldering per update.
//              This is the "transmigrate" face.
//
//  Yoshimitsu is the hermetic shinobi of the workbench: two faces, one blade,
//  silent and exact. FACE I transmutes CRSF frames into muscle; FACE II
//  transmigrates firmware into a sleeping receiver. One board, two faces,
//  one purpose: the harness never changes, and neither do you.
//
//  // homage to the Manji-clan shinobi of the soul — never print in docs.
//
//  MODE SWITCHING (BOOT button = GPIO0, pulled up, active low):
//
//    FACE I (converter)            FACE II (flasher)
//    ─────────────────             ─────────────────────────────
//    double-tap → FACE II          single tap   → restart receiver
//                                  double-tap   → drop into bootloader (flash)
//                                  long-press 2s → back to FACE I
//
//  USB SERIAL (FACE I only — the USB is a free console there, no binary):
//     FLASHER  switch to FACE II    ·    STATUS  mode + counters + pin map
//     HELP     command list
//
//  In FACE II the USB↔UART path is a PURE transparent bridge (no line parsing),
//  so esptool's SLIP-framed binary traffic passes through untouched.
//
//  TIMING: this sketch NEVER calls delay(). The BOOT-hold + power-cycle dance
//  and the receiver restart run on millis() state machines, so the bridge
//  stays byte-exact at all times.
//
//  Configure once, flash once, never touch again.
//
// =============================================================================
//  PIN MAP — EDIT HERE  (everything below is validated at compile time)
// -----------------------------------------------------------------------------
//  Pick ONE board profile. Use BOARD_CUSTOM to set every pin by hand.
//
//    BOARD_S3_WAVESHARE   Waveshare ESP32-S3-Tiny / -Micro / -Nano (default)
//    BOARD_CUSTOM         define every pin yourself in the block below
//
//  UART assignment is fixed by the sketch: Serial1 (UART1) = CRSF (FACE I),
//  Serial2 (UART2) = bridge (FACE II). On native-USB S3 boards almost every
//  GPIO can carry either UART via begin(baud, cfg, rx, tx) remapping.
//  GPIO0 is the BOOT button — never assign it. GPIO19/20 are the USB pins.
// =============================================================================

#include <Arduino.h>
#include <ESP32Servo.h>

// ── Board selection ─────────────────────────────────────────────────────────
#if !defined(BOARD_S3_WAVESHARE) && !defined(BOARD_CUSTOM)
  #define BOARD_S3_WAVESHARE 1
#endif

#if defined(BOARD_S3_WAVESHARE)
  #define CRSF_UART_RX_PIN  44      // S3 UART1 RX ← receiver TX
  #define CRSF_UART_TX_PIN  43      // S3 UART1 TX → receiver RX
  #define CRSF_BAUD         420000UL

  #define BRIDGE_RX_PIN     18      // S3 UART2 RX ← receiver TX
  #define BRIDGE_TX_PIN     17      // S3 UART2 TX → receiver RX
  #define BRIDGE_BAUD       115200UL

  #define SERVO_PIN_LEFT    1
  #define SERVO_PIN_RIGHT   2
  #define SERVO_PIN_RUDDER  3
  #define SERVO_PIN_AUX1    4       // live only while servoCount > 3
  #define SERVO_PIN_AUX2    5
  #define SERVO_PIN_AUX3    6
  #define SERVO_PIN_AUX4    7
  #define SERVO_PIN_AUX5    8

  #define RX_PWR_PIN        10      // P-MOSFET gate (LOW = receiver powered)
  #define RX_BOOT_PIN       9       // receiver BOOT pad (active low)
  #define MODE_LED_PIN      21      // status LED (set -1 to disable)
#endif

#ifdef BOARD_CUSTOM
  // ── define EVERY pin below ────────────────────────────────────────────────
  #define CRSF_UART_RX_PIN  44
  #define CRSF_UART_TX_PIN  43
  #define CRSF_BAUD         420000UL

  #define BRIDGE_RX_PIN     18
  #define BRIDGE_TX_PIN     17
  #define BRIDGE_BAUD       115200UL

  #define SERVO_PIN_LEFT    1
  #define SERVO_PIN_RIGHT   2
  #define SERVO_PIN_RUDDER  3
  #define SERVO_PIN_AUX1    4
  #define SERVO_PIN_AUX2    5
  #define SERVO_PIN_AUX3    6
  #define SERVO_PIN_AUX4    7
  #define SERVO_PIN_AUX5    8

  #define RX_PWR_PIN        10
  #define RX_BOOT_PIN       9
  #define MODE_LED_PIN      21
#endif

// ── every pin must be a legal S3 GPIO — never 0 (BOOT), never 19/20 (USB) ──
#if CRSF_UART_RX_PIN < 0 || CRSF_UART_RX_PIN > 48 || CRSF_UART_RX_PIN == 0 || CRSF_UART_RX_PIN == 19 || CRSF_UART_RX_PIN == 20
  #error "CRSF_UART_RX_PIN invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if CRSF_UART_TX_PIN < 0 || CRSF_UART_TX_PIN > 48 || CRSF_UART_TX_PIN == 0 || CRSF_UART_TX_PIN == 19 || CRSF_UART_TX_PIN == 20
  #error "CRSF_UART_TX_PIN invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if BRIDGE_RX_PIN < 0 || BRIDGE_RX_PIN > 48 || BRIDGE_RX_PIN == 0 || BRIDGE_RX_PIN == 19 || BRIDGE_RX_PIN == 20
  #error "BRIDGE_RX_PIN invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if BRIDGE_TX_PIN < 0 || BRIDGE_TX_PIN > 48 || BRIDGE_TX_PIN == 0 || BRIDGE_TX_PIN == 19 || BRIDGE_TX_PIN == 20
  #error "BRIDGE_TX_PIN invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if SERVO_PIN_LEFT < 0 || SERVO_PIN_LEFT > 48 || SERVO_PIN_LEFT == 0 || SERVO_PIN_LEFT == 19 || SERVO_PIN_LEFT == 20
  #error "SERVO_PIN_LEFT invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if SERVO_PIN_RIGHT < 0 || SERVO_PIN_RIGHT > 48 || SERVO_PIN_RIGHT == 0 || SERVO_PIN_RIGHT == 19 || SERVO_PIN_RIGHT == 20
  #error "SERVO_PIN_RIGHT invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if SERVO_PIN_RUDDER < 0 || SERVO_PIN_RUDDER > 48 || SERVO_PIN_RUDDER == 0 || SERVO_PIN_RUDDER == 19 || SERVO_PIN_RUDDER == 20
  #error "SERVO_PIN_RUDDER invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if SERVO_PIN_AUX1 < 0 || SERVO_PIN_AUX1 > 48 || SERVO_PIN_AUX1 == 0 || SERVO_PIN_AUX1 == 19 || SERVO_PIN_AUX1 == 20
  #error "SERVO_PIN_AUX1 invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if SERVO_PIN_AUX2 < 0 || SERVO_PIN_AUX2 > 48 || SERVO_PIN_AUX2 == 0 || SERVO_PIN_AUX2 == 19 || SERVO_PIN_AUX2 == 20
  #error "SERVO_PIN_AUX2 invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if SERVO_PIN_AUX3 < 0 || SERVO_PIN_AUX3 > 48 || SERVO_PIN_AUX3 == 0 || SERVO_PIN_AUX3 == 19 || SERVO_PIN_AUX3 == 20
  #error "SERVO_PIN_AUX3 invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if SERVO_PIN_AUX4 < 0 || SERVO_PIN_AUX4 > 48 || SERVO_PIN_AUX4 == 0 || SERVO_PIN_AUX4 == 19 || SERVO_PIN_AUX4 == 20
  #error "SERVO_PIN_AUX4 invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if SERVO_PIN_AUX5 < 0 || SERVO_PIN_AUX5 > 48 || SERVO_PIN_AUX5 == 0 || SERVO_PIN_AUX5 == 19 || SERVO_PIN_AUX5 == 20
  #error "SERVO_PIN_AUX5 invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if RX_PWR_PIN < 0 || RX_PWR_PIN > 48 || RX_PWR_PIN == 0 || RX_PWR_PIN == 19 || RX_PWR_PIN == 20
  #error "RX_PWR_PIN invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if RX_BOOT_PIN < 0 || RX_BOOT_PIN > 48 || RX_BOOT_PIN == 0 || RX_BOOT_PIN == 19 || RX_BOOT_PIN == 20
  #error "RX_BOOT_PIN invalid — GPIO 1..48, never 0 (BOOT button), never 19/20 (USB)"
#endif
#if MODE_LED_PIN < -1 || MODE_LED_PIN == 0 || MODE_LED_PIN == 19 || MODE_LED_PIN == 20 || MODE_LED_PIN > 48
  #error "MODE_LED_PIN invalid — -1 disables it, else GPIO 1..48, never 0/19/20"
#endif

// ── one UART per face, TX ≠ RX on each ──
#if CRSF_UART_TX_PIN == CRSF_UART_RX_PIN
  #error "PIN COLLISION: CRSF TX and RX share one GPIO"
#endif
#if BRIDGE_TX_PIN == BRIDGE_RX_PIN
  #error "PIN COLLISION: BRIDGE TX and RX share one GPIO"
#endif

// ── pin collisions — two functions on one GPIO never build ──
// (aux servo pins are only live while servoCount > 3, but are validated
//  regardless — a harness must never have two meanings for one pin)
#if CRSF_UART_RX_PIN == CRSF_UART_TX_PIN
  #error "PIN COLLISION: CRSF_UART_RX_PIN and CRSF_UART_TX_PIN share one GPIO"
#endif
#if CRSF_UART_RX_PIN == BRIDGE_RX_PIN
  #error "PIN COLLISION: CRSF_UART_RX_PIN and BRIDGE_RX_PIN share one GPIO"
#endif
#if CRSF_UART_RX_PIN == BRIDGE_TX_PIN
  #error "PIN COLLISION: CRSF_UART_RX_PIN and BRIDGE_TX_PIN share one GPIO"
#endif
#if CRSF_UART_RX_PIN == SERVO_PIN_LEFT
  #error "PIN COLLISION: CRSF_UART_RX_PIN and SERVO_PIN_LEFT share one GPIO"
#endif
#if CRSF_UART_RX_PIN == SERVO_PIN_RIGHT
  #error "PIN COLLISION: CRSF_UART_RX_PIN and SERVO_PIN_RIGHT share one GPIO"
#endif
#if CRSF_UART_RX_PIN == SERVO_PIN_RUDDER
  #error "PIN COLLISION: CRSF_UART_RX_PIN and SERVO_PIN_RUDDER share one GPIO"
#endif
#if CRSF_UART_RX_PIN == SERVO_PIN_AUX1
  #error "PIN COLLISION: CRSF_UART_RX_PIN and SERVO_PIN_AUX1 share one GPIO"
#endif
#if CRSF_UART_RX_PIN == SERVO_PIN_AUX2
  #error "PIN COLLISION: CRSF_UART_RX_PIN and SERVO_PIN_AUX2 share one GPIO"
#endif
#if CRSF_UART_RX_PIN == SERVO_PIN_AUX3
  #error "PIN COLLISION: CRSF_UART_RX_PIN and SERVO_PIN_AUX3 share one GPIO"
#endif
#if CRSF_UART_RX_PIN == SERVO_PIN_AUX4
  #error "PIN COLLISION: CRSF_UART_RX_PIN and SERVO_PIN_AUX4 share one GPIO"
#endif
#if CRSF_UART_RX_PIN == SERVO_PIN_AUX5
  #error "PIN COLLISION: CRSF_UART_RX_PIN and SERVO_PIN_AUX5 share one GPIO"
#endif
#if CRSF_UART_RX_PIN == RX_PWR_PIN
  #error "PIN COLLISION: CRSF_UART_RX_PIN and RX_PWR_PIN share one GPIO"
#endif
#if CRSF_UART_RX_PIN == RX_BOOT_PIN
  #error "PIN COLLISION: CRSF_UART_RX_PIN and RX_BOOT_PIN share one GPIO"
#endif
#if CRSF_UART_TX_PIN == BRIDGE_RX_PIN
  #error "PIN COLLISION: CRSF_UART_TX_PIN and BRIDGE_RX_PIN share one GPIO"
#endif
#if CRSF_UART_TX_PIN == BRIDGE_TX_PIN
  #error "PIN COLLISION: CRSF_UART_TX_PIN and BRIDGE_TX_PIN share one GPIO"
#endif
#if CRSF_UART_TX_PIN == SERVO_PIN_LEFT
  #error "PIN COLLISION: CRSF_UART_TX_PIN and SERVO_PIN_LEFT share one GPIO"
#endif
#if CRSF_UART_TX_PIN == SERVO_PIN_RIGHT
  #error "PIN COLLISION: CRSF_UART_TX_PIN and SERVO_PIN_RIGHT share one GPIO"
#endif
#if CRSF_UART_TX_PIN == SERVO_PIN_RUDDER
  #error "PIN COLLISION: CRSF_UART_TX_PIN and SERVO_PIN_RUDDER share one GPIO"
#endif
#if CRSF_UART_TX_PIN == SERVO_PIN_AUX1
  #error "PIN COLLISION: CRSF_UART_TX_PIN and SERVO_PIN_AUX1 share one GPIO"
#endif
#if CRSF_UART_TX_PIN == SERVO_PIN_AUX2
  #error "PIN COLLISION: CRSF_UART_TX_PIN and SERVO_PIN_AUX2 share one GPIO"
#endif
#if CRSF_UART_TX_PIN == SERVO_PIN_AUX3
  #error "PIN COLLISION: CRSF_UART_TX_PIN and SERVO_PIN_AUX3 share one GPIO"
#endif
#if CRSF_UART_TX_PIN == SERVO_PIN_AUX4
  #error "PIN COLLISION: CRSF_UART_TX_PIN and SERVO_PIN_AUX4 share one GPIO"
#endif
#if CRSF_UART_TX_PIN == SERVO_PIN_AUX5
  #error "PIN COLLISION: CRSF_UART_TX_PIN and SERVO_PIN_AUX5 share one GPIO"
#endif
#if CRSF_UART_TX_PIN == RX_PWR_PIN
  #error "PIN COLLISION: CRSF_UART_TX_PIN and RX_PWR_PIN share one GPIO"
#endif
#if CRSF_UART_TX_PIN == RX_BOOT_PIN
  #error "PIN COLLISION: CRSF_UART_TX_PIN and RX_BOOT_PIN share one GPIO"
#endif
#if BRIDGE_RX_PIN == BRIDGE_TX_PIN
  #error "PIN COLLISION: BRIDGE_RX_PIN and BRIDGE_TX_PIN share one GPIO"
#endif
#if BRIDGE_RX_PIN == SERVO_PIN_LEFT
  #error "PIN COLLISION: BRIDGE_RX_PIN and SERVO_PIN_LEFT share one GPIO"
#endif
#if BRIDGE_RX_PIN == SERVO_PIN_RIGHT
  #error "PIN COLLISION: BRIDGE_RX_PIN and SERVO_PIN_RIGHT share one GPIO"
#endif
#if BRIDGE_RX_PIN == SERVO_PIN_RUDDER
  #error "PIN COLLISION: BRIDGE_RX_PIN and SERVO_PIN_RUDDER share one GPIO"
#endif
#if BRIDGE_RX_PIN == SERVO_PIN_AUX1
  #error "PIN COLLISION: BRIDGE_RX_PIN and SERVO_PIN_AUX1 share one GPIO"
#endif
#if BRIDGE_RX_PIN == SERVO_PIN_AUX2
  #error "PIN COLLISION: BRIDGE_RX_PIN and SERVO_PIN_AUX2 share one GPIO"
#endif
#if BRIDGE_RX_PIN == SERVO_PIN_AUX3
  #error "PIN COLLISION: BRIDGE_RX_PIN and SERVO_PIN_AUX3 share one GPIO"
#endif
#if BRIDGE_RX_PIN == SERVO_PIN_AUX4
  #error "PIN COLLISION: BRIDGE_RX_PIN and SERVO_PIN_AUX4 share one GPIO"
#endif
#if BRIDGE_RX_PIN == SERVO_PIN_AUX5
  #error "PIN COLLISION: BRIDGE_RX_PIN and SERVO_PIN_AUX5 share one GPIO"
#endif
#if BRIDGE_RX_PIN == RX_PWR_PIN
  #error "PIN COLLISION: BRIDGE_RX_PIN and RX_PWR_PIN share one GPIO"
#endif
#if BRIDGE_RX_PIN == RX_BOOT_PIN
  #error "PIN COLLISION: BRIDGE_RX_PIN and RX_BOOT_PIN share one GPIO"
#endif
#if BRIDGE_TX_PIN == SERVO_PIN_LEFT
  #error "PIN COLLISION: BRIDGE_TX_PIN and SERVO_PIN_LEFT share one GPIO"
#endif
#if BRIDGE_TX_PIN == SERVO_PIN_RIGHT
  #error "PIN COLLISION: BRIDGE_TX_PIN and SERVO_PIN_RIGHT share one GPIO"
#endif
#if BRIDGE_TX_PIN == SERVO_PIN_RUDDER
  #error "PIN COLLISION: BRIDGE_TX_PIN and SERVO_PIN_RUDDER share one GPIO"
#endif
#if BRIDGE_TX_PIN == SERVO_PIN_AUX1
  #error "PIN COLLISION: BRIDGE_TX_PIN and SERVO_PIN_AUX1 share one GPIO"
#endif
#if BRIDGE_TX_PIN == SERVO_PIN_AUX2
  #error "PIN COLLISION: BRIDGE_TX_PIN and SERVO_PIN_AUX2 share one GPIO"
#endif
#if BRIDGE_TX_PIN == SERVO_PIN_AUX3
  #error "PIN COLLISION: BRIDGE_TX_PIN and SERVO_PIN_AUX3 share one GPIO"
#endif
#if BRIDGE_TX_PIN == SERVO_PIN_AUX4
  #error "PIN COLLISION: BRIDGE_TX_PIN and SERVO_PIN_AUX4 share one GPIO"
#endif
#if BRIDGE_TX_PIN == SERVO_PIN_AUX5
  #error "PIN COLLISION: BRIDGE_TX_PIN and SERVO_PIN_AUX5 share one GPIO"
#endif
#if BRIDGE_TX_PIN == RX_PWR_PIN
  #error "PIN COLLISION: BRIDGE_TX_PIN and RX_PWR_PIN share one GPIO"
#endif
#if BRIDGE_TX_PIN == RX_BOOT_PIN
  #error "PIN COLLISION: BRIDGE_TX_PIN and RX_BOOT_PIN share one GPIO"
#endif
#if SERVO_PIN_LEFT == SERVO_PIN_RIGHT
  #error "PIN COLLISION: SERVO_PIN_LEFT and SERVO_PIN_RIGHT share one GPIO"
#endif
#if SERVO_PIN_LEFT == SERVO_PIN_RUDDER
  #error "PIN COLLISION: SERVO_PIN_LEFT and SERVO_PIN_RUDDER share one GPIO"
#endif
#if SERVO_PIN_LEFT == SERVO_PIN_AUX1
  #error "PIN COLLISION: SERVO_PIN_LEFT and SERVO_PIN_AUX1 share one GPIO"
#endif
#if SERVO_PIN_LEFT == SERVO_PIN_AUX2
  #error "PIN COLLISION: SERVO_PIN_LEFT and SERVO_PIN_AUX2 share one GPIO"
#endif
#if SERVO_PIN_LEFT == SERVO_PIN_AUX3
  #error "PIN COLLISION: SERVO_PIN_LEFT and SERVO_PIN_AUX3 share one GPIO"
#endif
#if SERVO_PIN_LEFT == SERVO_PIN_AUX4
  #error "PIN COLLISION: SERVO_PIN_LEFT and SERVO_PIN_AUX4 share one GPIO"
#endif
#if SERVO_PIN_LEFT == SERVO_PIN_AUX5
  #error "PIN COLLISION: SERVO_PIN_LEFT and SERVO_PIN_AUX5 share one GPIO"
#endif
#if SERVO_PIN_LEFT == RX_PWR_PIN
  #error "PIN COLLISION: SERVO_PIN_LEFT and RX_PWR_PIN share one GPIO"
#endif
#if SERVO_PIN_LEFT == RX_BOOT_PIN
  #error "PIN COLLISION: SERVO_PIN_LEFT and RX_BOOT_PIN share one GPIO"
#endif
#if SERVO_PIN_RIGHT == SERVO_PIN_RUDDER
  #error "PIN COLLISION: SERVO_PIN_RIGHT and SERVO_PIN_RUDDER share one GPIO"
#endif
#if SERVO_PIN_RIGHT == SERVO_PIN_AUX1
  #error "PIN COLLISION: SERVO_PIN_RIGHT and SERVO_PIN_AUX1 share one GPIO"
#endif
#if SERVO_PIN_RIGHT == SERVO_PIN_AUX2
  #error "PIN COLLISION: SERVO_PIN_RIGHT and SERVO_PIN_AUX2 share one GPIO"
#endif
#if SERVO_PIN_RIGHT == SERVO_PIN_AUX3
  #error "PIN COLLISION: SERVO_PIN_RIGHT and SERVO_PIN_AUX3 share one GPIO"
#endif
#if SERVO_PIN_RIGHT == SERVO_PIN_AUX4
  #error "PIN COLLISION: SERVO_PIN_RIGHT and SERVO_PIN_AUX4 share one GPIO"
#endif
#if SERVO_PIN_RIGHT == SERVO_PIN_AUX5
  #error "PIN COLLISION: SERVO_PIN_RIGHT and SERVO_PIN_AUX5 share one GPIO"
#endif
#if SERVO_PIN_RIGHT == RX_PWR_PIN
  #error "PIN COLLISION: SERVO_PIN_RIGHT and RX_PWR_PIN share one GPIO"
#endif
#if SERVO_PIN_RIGHT == RX_BOOT_PIN
  #error "PIN COLLISION: SERVO_PIN_RIGHT and RX_BOOT_PIN share one GPIO"
#endif
#if SERVO_PIN_RUDDER == SERVO_PIN_AUX1
  #error "PIN COLLISION: SERVO_PIN_RUDDER and SERVO_PIN_AUX1 share one GPIO"
#endif
#if SERVO_PIN_RUDDER == SERVO_PIN_AUX2
  #error "PIN COLLISION: SERVO_PIN_RUDDER and SERVO_PIN_AUX2 share one GPIO"
#endif
#if SERVO_PIN_RUDDER == SERVO_PIN_AUX3
  #error "PIN COLLISION: SERVO_PIN_RUDDER and SERVO_PIN_AUX3 share one GPIO"
#endif
#if SERVO_PIN_RUDDER == SERVO_PIN_AUX4
  #error "PIN COLLISION: SERVO_PIN_RUDDER and SERVO_PIN_AUX4 share one GPIO"
#endif
#if SERVO_PIN_RUDDER == SERVO_PIN_AUX5
  #error "PIN COLLISION: SERVO_PIN_RUDDER and SERVO_PIN_AUX5 share one GPIO"
#endif
#if SERVO_PIN_RUDDER == RX_PWR_PIN
  #error "PIN COLLISION: SERVO_PIN_RUDDER and RX_PWR_PIN share one GPIO"
#endif
#if SERVO_PIN_RUDDER == RX_BOOT_PIN
  #error "PIN COLLISION: SERVO_PIN_RUDDER and RX_BOOT_PIN share one GPIO"
#endif
#if SERVO_PIN_AUX1 == SERVO_PIN_AUX2
  #error "PIN COLLISION: SERVO_PIN_AUX1 and SERVO_PIN_AUX2 share one GPIO"
#endif
#if SERVO_PIN_AUX1 == SERVO_PIN_AUX3
  #error "PIN COLLISION: SERVO_PIN_AUX1 and SERVO_PIN_AUX3 share one GPIO"
#endif
#if SERVO_PIN_AUX1 == SERVO_PIN_AUX4
  #error "PIN COLLISION: SERVO_PIN_AUX1 and SERVO_PIN_AUX4 share one GPIO"
#endif
#if SERVO_PIN_AUX1 == SERVO_PIN_AUX5
  #error "PIN COLLISION: SERVO_PIN_AUX1 and SERVO_PIN_AUX5 share one GPIO"
#endif
#if SERVO_PIN_AUX1 == RX_PWR_PIN
  #error "PIN COLLISION: SERVO_PIN_AUX1 and RX_PWR_PIN share one GPIO"
#endif
#if SERVO_PIN_AUX1 == RX_BOOT_PIN
  #error "PIN COLLISION: SERVO_PIN_AUX1 and RX_BOOT_PIN share one GPIO"
#endif
#if SERVO_PIN_AUX2 == SERVO_PIN_AUX3
  #error "PIN COLLISION: SERVO_PIN_AUX2 and SERVO_PIN_AUX3 share one GPIO"
#endif
#if SERVO_PIN_AUX2 == SERVO_PIN_AUX4
  #error "PIN COLLISION: SERVO_PIN_AUX2 and SERVO_PIN_AUX4 share one GPIO"
#endif
#if SERVO_PIN_AUX2 == SERVO_PIN_AUX5
  #error "PIN COLLISION: SERVO_PIN_AUX2 and SERVO_PIN_AUX5 share one GPIO"
#endif
#if SERVO_PIN_AUX2 == RX_PWR_PIN
  #error "PIN COLLISION: SERVO_PIN_AUX2 and RX_PWR_PIN share one GPIO"
#endif
#if SERVO_PIN_AUX2 == RX_BOOT_PIN
  #error "PIN COLLISION: SERVO_PIN_AUX2 and RX_BOOT_PIN share one GPIO"
#endif
#if SERVO_PIN_AUX3 == SERVO_PIN_AUX4
  #error "PIN COLLISION: SERVO_PIN_AUX3 and SERVO_PIN_AUX4 share one GPIO"
#endif
#if SERVO_PIN_AUX3 == SERVO_PIN_AUX5
  #error "PIN COLLISION: SERVO_PIN_AUX3 and SERVO_PIN_AUX5 share one GPIO"
#endif
#if SERVO_PIN_AUX3 == RX_PWR_PIN
  #error "PIN COLLISION: SERVO_PIN_AUX3 and RX_PWR_PIN share one GPIO"
#endif
#if SERVO_PIN_AUX3 == RX_BOOT_PIN
  #error "PIN COLLISION: SERVO_PIN_AUX3 and RX_BOOT_PIN share one GPIO"
#endif
#if SERVO_PIN_AUX4 == SERVO_PIN_AUX5
  #error "PIN COLLISION: SERVO_PIN_AUX4 and SERVO_PIN_AUX5 share one GPIO"
#endif
#if SERVO_PIN_AUX4 == RX_PWR_PIN
  #error "PIN COLLISION: SERVO_PIN_AUX4 and RX_PWR_PIN share one GPIO"
#endif
#if SERVO_PIN_AUX4 == RX_BOOT_PIN
  #error "PIN COLLISION: SERVO_PIN_AUX4 and RX_BOOT_PIN share one GPIO"
#endif
#if SERVO_PIN_AUX5 == RX_PWR_PIN
  #error "PIN COLLISION: SERVO_PIN_AUX5 and RX_PWR_PIN share one GPIO"
#endif
#if SERVO_PIN_AUX5 == RX_BOOT_PIN
  #error "PIN COLLISION: SERVO_PIN_AUX5 and RX_BOOT_PIN share one GPIO"
#endif
#if RX_PWR_PIN == RX_BOOT_PIN
  #error "PIN COLLISION: RX_PWR_PIN and RX_BOOT_PIN share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == CRSF_UART_RX_PIN
  #error "PIN COLLISION: MODE_LED_PIN and CRSF_UART_RX_PIN share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == CRSF_UART_TX_PIN
  #error "PIN COLLISION: MODE_LED_PIN and CRSF_UART_TX_PIN share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == BRIDGE_RX_PIN
  #error "PIN COLLISION: MODE_LED_PIN and BRIDGE_RX_PIN share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == BRIDGE_TX_PIN
  #error "PIN COLLISION: MODE_LED_PIN and BRIDGE_TX_PIN share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == SERVO_PIN_LEFT
  #error "PIN COLLISION: MODE_LED_PIN and SERVO_PIN_LEFT share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == SERVO_PIN_RIGHT
  #error "PIN COLLISION: MODE_LED_PIN and SERVO_PIN_RIGHT share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == SERVO_PIN_RUDDER
  #error "PIN COLLISION: MODE_LED_PIN and SERVO_PIN_RUDDER share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == SERVO_PIN_AUX1
  #error "PIN COLLISION: MODE_LED_PIN and SERVO_PIN_AUX1 share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == SERVO_PIN_AUX2
  #error "PIN COLLISION: MODE_LED_PIN and SERVO_PIN_AUX2 share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == SERVO_PIN_AUX3
  #error "PIN COLLISION: MODE_LED_PIN and SERVO_PIN_AUX3 share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == SERVO_PIN_AUX4
  #error "PIN COLLISION: MODE_LED_PIN and SERVO_PIN_AUX4 share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == SERVO_PIN_AUX5
  #error "PIN COLLISION: MODE_LED_PIN and SERVO_PIN_AUX5 share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == RX_PWR_PIN
  #error "PIN COLLISION: MODE_LED_PIN and RX_PWR_PIN share one GPIO"
#endif
#if MODE_LED_PIN >= 0 && MODE_LED_PIN == RX_BOOT_PIN
  #error "PIN COLLISION: MODE_LED_PIN and RX_BOOT_PIN share one GPIO"
#endif

// ── BOARD_CUSTOM must define every pin ──
#ifndef CRSF_UART_RX_PIN
  #error "BOARD_CUSTOM: define CRSF_UART_RX_PIN"
#endif
#ifndef CRSF_UART_TX_PIN
  #error "BOARD_CUSTOM: define CRSF_UART_TX_PIN"
#endif
#ifndef BRIDGE_RX_PIN
  #error "BOARD_CUSTOM: define BRIDGE_RX_PIN"
#endif
#ifndef BRIDGE_TX_PIN
  #error "BOARD_CUSTOM: define BRIDGE_TX_PIN"
#endif
#ifndef SERVO_PIN_LEFT
  #error "BOARD_CUSTOM: define SERVO_PIN_LEFT"
#endif
#ifndef SERVO_PIN_RIGHT
  #error "BOARD_CUSTOM: define SERVO_PIN_RIGHT"
#endif
#ifndef SERVO_PIN_RUDDER
  #error "BOARD_CUSTOM: define SERVO_PIN_RUDDER"
#endif
#ifndef SERVO_PIN_AUX1
  #error "BOARD_CUSTOM: define SERVO_PIN_AUX1"
#endif
#ifndef SERVO_PIN_AUX2
  #error "BOARD_CUSTOM: define SERVO_PIN_AUX2"
#endif
#ifndef SERVO_PIN_AUX3
  #error "BOARD_CUSTOM: define SERVO_PIN_AUX3"
#endif
#ifndef SERVO_PIN_AUX4
  #error "BOARD_CUSTOM: define SERVO_PIN_AUX4"
#endif
#ifndef SERVO_PIN_AUX5
  #error "BOARD_CUSTOM: define SERVO_PIN_AUX5"
#endif
#ifndef RX_PWR_PIN
  #error "BOARD_CUSTOM: define RX_PWR_PIN"
#endif
#ifndef RX_BOOT_PIN
  #error "BOARD_CUSTOM: define RX_BOOT_PIN"
#endif
#ifndef MODE_LED_PIN
  #error "BOARD_CUSTOM: define MODE_LED_PIN"
#endif

// ── CRSF constants (FACE I) ─────────────────────────────────────────────
#define SERVO_COUNT_MAX 8
#define CHANNEL_COUNT   16
#define CRSF_RC_TYPE    0x16
#define CRSF_PAYLOAD    22        // 16 ch × 11 bit = 22 bytes

// CRSF numeric window and PWM envelope (PteronautOS conventions)
#define RAW_MIN   172
#define RAW_MAX   1811
#define PWM_MIN   988
#define PWM_MAX   2012
#define FAILSAFE_MS 500
#define BRIDGE_BURST 8         // max bytes per direction per pass (FACE II)

// ── Timing (FACE II + mode switch) ──────────────────────────────────────
#define PRESS_WINDOW_MS 1500
#define DEBOUNCE_MS     40
#define LONG_PRESS_MS   2000
#define HOLD_OFF_MS     120       // receiver power-off during the dance
#define HOLD_ON_MS      900       // wait inside bootloader after re-power
#define RX_RESTART_MS   700       // settle after a plain restart
#define SETTLE_MS       80        // BOOT release settle
#define BOOT_PRINT_MS   150       // USB settle gate before the boot banner

enum Mode : uint8_t { MODE_CONVERTER, MODE_FLASHER };

// ── FACE I state ────────────────────────────────────────────────────────
static Servo servos[SERVO_COUNT_MAX];
static uint16_t channel[CHANNEL_COUNT];
static uint8_t  pinOf[SERVO_COUNT_MAX] = {
  SERVO_PIN_LEFT, SERVO_PIN_RIGHT, SERVO_PIN_RUDDER,
  SERVO_PIN_AUX1, SERVO_PIN_AUX2, SERVO_PIN_AUX3, SERVO_PIN_AUX4, SERVO_PIN_AUX5
};
static uint8_t  servoCount = 3;                 // set higher for 4–8 channels

enum { S_HEADER, S_LEN, S_TYPE, S_PAYLOAD, S_CRC };
static uint8_t  crsfState = S_HEADER;
static uint8_t  frameLen = 0, frameType = 0;
static uint8_t  payload[CRSF_PAYLOAD];
static uint8_t  payloadIdx = 0;
static uint32_t lastGoodMs = 0;

// ── Global mode state ───────────────────────────────────────────────────
static Mode     mode = MODE_CONVERTER;
static bool     rxPowered = false;

// ── Bridge watchdog counters (visible via STATUS, never printed in FACE II) ─
static uint32_t bridgeUsbToRx = 0;    // bytes forwarded USB → receiver
static uint32_t bridgeRxToUsb = 0;    // bytes forwarded receiver → USB
static uint32_t bridgeOverflows = 0;  // bridge UART overflow recoveries
static uint32_t lastBridgeMs  = 0;    // last byte through the bridge

// =============================================================================
//  FACE I — CRSF → PWM
// =============================================================================

static uint8_t crsfCrc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static uint16_t mapRaw(uint16_t raw) {
  if (raw <= RAW_MIN) return PWM_MIN;
  if (raw >= RAW_MAX) return PWM_MAX;
  return (uint16_t)(PWM_MIN + (uint32_t)(raw - RAW_MIN) * (PWM_MAX - PWM_MIN) / (RAW_MAX - RAW_MIN));
}

static void applyChannels(void) {
  // PteronautOS mirror conventions: roll differential on the wings,
  // rudder on the crest, elevator feeding both wings common-mode.
  // Channel map: 0 roll, 1 pitch (elevator), 3 yaw (rudder), 4 arm.
  uint16_t roll  = mapRaw(channel[0]);
  uint16_t pitch = mapRaw(channel[1]);
  uint16_t yaw   = mapRaw(channel[3]);
  uint16_t arm   = channel[4];
  bool     armed = (arm > 992);

  if (!armed) {                       // disarmed: wings centred, rudder centred
    for (uint8_t i = 0; i < servoCount; i++) servos[i].writeMicroseconds(1500);
    return;
  }

  int32_t left  = 1500 + ((int32_t)roll  - 1500) + ((int32_t)pitch - 1500);
  int32_t right = 1500 - ((int32_t)roll  - 1500) + ((int32_t)pitch - 1500);
  left  = constrain(left,  (int32_t)PWM_MIN, (int32_t)PWM_MAX);
  right = constrain(right, (int32_t)PWM_MIN, (int32_t)PWM_MAX);

  servos[0].writeMicroseconds((uint16_t)left);    // left wing
  servos[1].writeMicroseconds((uint16_t)right);   // right wing
  servos[2].writeMicroseconds(yaw);               // crest rudder
  for (uint8_t i = 3; i < servoCount; i++) {      // aux channels pass through
    servos[i].writeMicroseconds(mapRaw(channel[i < CHANNEL_COUNT ? i : 0]));
  }
}

static void attachServos() {
  for (uint8_t i = 0; i < servoCount && i < SERVO_COUNT_MAX; i++) {
    if (!servos[i].attached()) {
      servos[i].attach(pinOf[i], PWM_MIN, PWM_MAX);
    }
    servos[i].writeMicroseconds(1500);
  }
}

static void detachServos() {
  for (uint8_t i = 0; i < SERVO_COUNT_MAX; i++) {
    if (servos[i].attached()) servos[i].detach();
  }
}

static void pumpCrsf() {
  while (Serial1.available()) {
    uint8_t b = (uint8_t)Serial1.read();

    switch (crsfState) {
      case S_HEADER:
        if (b == 0xC8 || b == 0xEE) crsfState = S_LEN;   // device / extended header
        break;
      case S_LEN:
        frameLen = b;
        crsfState = (b >= 2 && b <= 64) ? S_TYPE : S_HEADER;
        break;
      case S_TYPE:
        frameType = b;
        payloadIdx = 0;
        crsfState = S_PAYLOAD;
        break;
      case S_PAYLOAD:
        if (payloadIdx < CRSF_PAYLOAD) {
          payload[payloadIdx++] = b;
          if (payloadIdx == CRSF_PAYLOAD) crsfState = S_CRC;
        } else {
          crsfState = S_HEADER;
        }
        break;
      case S_CRC: {
        uint8_t crcBuf[CRSF_PAYLOAD + 2];                 // len + type + payload
        crcBuf[0] = frameLen;
        crcBuf[1] = frameType;
        memcpy(crcBuf + 2, payload, CRSF_PAYLOAD);
        if (frameType == CRSF_RC_TYPE && crsfCrc8(crcBuf, CRSF_PAYLOAD + 2) == b) {
          for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {   // 16 × 11-bit LE channels
            uint8_t byteIdx = (uint8_t)((i * 11) >> 3);
            uint8_t shift   = (uint8_t)((i * 11) & 7);
            channel[i] = (uint16_t)((payload[byteIdx] | (payload[byteIdx + 1] << 8)) >> shift) & 0x07FF;
          }
          lastGoodMs = millis();
          applyChannels();
        }
        crsfState = S_HEADER;
        break;
      }
    }
  }

  if (millis() - lastGoodMs > FAILSAFE_MS) {
    for (uint8_t i = 0; i < servoCount; i++) servos[i].writeMicroseconds(1500);
    lastGoodMs = millis();
  }
}

// =============================================================================
//  FACE II — pocket flasher
// =============================================================================

static void rxPower(bool on) {
  digitalWrite(RX_PWR_PIN, on ? LOW : HIGH);    // gate LOW = MOSFET on
  rxPowered = on;
}

static void bootAssert(bool hold) {
  digitalWrite(RX_BOOT_PIN, hold ? LOW : HIGH);
}

// ── Power-cycle dance — async millis() machine, NEVER blocking ──────────────
enum : uint8_t { DANCE_IDLE, DANCE_BOOT_HOLD, DANCE_POWER_OFF, DANCE_POWER_ON, DANCE_SETTLE };
enum : uint8_t { DANCE_QUIET, DANCE_ANNOUNCE_RESTART, DANCE_ANNOUNCE_BOOTLOADER };
static uint8_t  dancePhase    = DANCE_IDLE;
static uint8_t  danceAnnounce = DANCE_QUIET;
static bool     danceHoldBoot = false;
static uint32_t danceOnMs     = HOLD_ON_MS;
static uint32_t danceT0       = 0;

static void danceBegin(bool holdBoot, uint8_t announce) {
  if (dancePhase != DANCE_IDLE) return;         // one dance at a time
  danceHoldBoot = holdBoot;
  danceAnnounce = announce;
  danceOnMs     = holdBoot ? HOLD_ON_MS : RX_RESTART_MS;
  dancePhase    = DANCE_BOOT_HOLD;
  danceT0       = millis();
}

static void pumpDance(void) {
  switch (dancePhase) {
    case DANCE_IDLE:
      return;
    case DANCE_BOOT_HOLD:
      if (danceHoldBoot) bootAssert(true);      // 1. hold BOOT low (flash mode)
      rxPower(false);                           // 2. kill power
      dancePhase = DANCE_POWER_OFF;
      danceT0 = millis();
      break;
    case DANCE_POWER_OFF:
      if (millis() - danceT0 >= HOLD_OFF_MS) {
        rxPower(true);                          // 3. power up (bootloader or new fw)
        dancePhase = DANCE_POWER_ON;
        danceT0 = millis();
      }
      break;
    case DANCE_POWER_ON:
      if (millis() - danceT0 >= danceOnMs) {
        if (danceHoldBoot) bootAssert(false);   // 4. release BOOT
        dancePhase = DANCE_SETTLE;
        danceT0 = millis();
      }
      break;
    case DANCE_SETTLE:
      if (millis() - danceT0 >= SETTLE_MS) {
        dancePhase = DANCE_IDLE;
        if (danceAnnounce == DANCE_ANNOUNCE_BOOTLOADER) {
          Serial.println("YOSHIMITSU: receiver in ROM bootloader — run esptool with --before no_reset now.");
        } else if (danceAnnounce == DANCE_ANNOUNCE_RESTART) {
          Serial.println("YOSHIMITSU: receiver restarted — new firmware should be running.");
        }
        danceAnnounce = DANCE_QUIET;
      }
      break;
  }
}

// PURE transparent bridge — no line parsing, so esptool's binary SLIP flows free.
// Up to BRIDGE_BURST bytes per direction per pass: byte-exact, never blocking,
// and fast enough for 460800-baud esptool transfers even in a single pass.
static void pumpBridge() {
  bool moved = false;
  uint8_t n = 0;
  while (Serial2.available() && Serial.availableForWrite() && n < BRIDGE_BURST) {
    Serial.write(Serial2.read());
    n++;
    moved = true;
  }
  if (n) bridgeRxToUsb += n;
  n = 0;
  while (Serial.available() && Serial2.availableForWrite() && n < BRIDGE_BURST) {
    Serial2.write(Serial.read());
    n++;
    moved = true;
  }
  if (n) bridgeUsbToRx += n;
  if (moved) lastBridgeMs = millis();
}

// UART overflow reset: drain the FIFO and re-init the UART — esptool's SLIP
// CRC/timeout then simply retransmits, and the link stays alive.
static void recoverBridgeUart() {
  while (Serial2.available()) (void)Serial2.read();
  Serial2.end();
  Serial2.begin(BRIDGE_BAUD, SERIAL_8N1, BRIDGE_RX_PIN, BRIDGE_TX_PIN);
  bridgeOverflows++;
}

// =============================================================================
//  Mode switching + console (FACE I)
// =============================================================================

static void setLed() {
  if (MODE_LED_PIN >= 0) {
    digitalWrite(MODE_LED_PIN, mode == MODE_CONVERTER ? HIGH : LOW);
  }
}

static void printPinMap(void) {
  Serial.println("YOSHIMITSU: active pin map (edit in the PIN MAP block)");
  Serial.print("  CRSF    UART1 RX=GP"); Serial.print(CRSF_UART_RX_PIN);
  Serial.print("  TX=GP"); Serial.println(CRSF_UART_TX_PIN);
  Serial.print("  BRIDGE  UART2 RX=GP"); Serial.print(BRIDGE_RX_PIN);
  Serial.print("  TX=GP"); Serial.println(BRIDGE_TX_PIN);
  Serial.print("  SERVO   L=GP"); Serial.print(SERVO_PIN_LEFT);
  Serial.print("  R=GP"); Serial.print(SERVO_PIN_RIGHT);
  Serial.print("  RUD=GP"); Serial.print(SERVO_PIN_RUDDER);
  Serial.print("  AUX=GP"); Serial.print(SERVO_PIN_AUX1);
  Serial.print(",GP"); Serial.print(SERVO_PIN_AUX2);
  Serial.print(",GP"); Serial.print(SERVO_PIN_AUX3);
  Serial.print(",GP"); Serial.print(SERVO_PIN_AUX4);
  Serial.print(",GP"); Serial.println(SERVO_PIN_AUX5);
  Serial.print("  RX_BOOT=GP"); Serial.print(RX_BOOT_PIN);
  Serial.print("  RX_PWR=GP"); Serial.print(RX_PWR_PIN);
  Serial.print("  LED=GP"); Serial.println(MODE_LED_PIN);
}

static void printStatus() {
  Serial.print("YOSHIMITSU: mode = ");
  Serial.print(mode == MODE_CONVERTER ? "FACE I (converter)" : "FACE II (flasher)");
  Serial.print(" · receiver power = ");
  Serial.println(rxPowered ? "ON" : "OFF");
  Serial.print("  bridge: USB→RX "); Serial.print(bridgeUsbToRx);
  Serial.print(" bytes · RX→USB "); Serial.print(bridgeRxToUsb);
  Serial.print(" bytes · overflows "); Serial.print(bridgeOverflows);
  Serial.print(" · idle "); Serial.print(millis() - lastBridgeMs);
  Serial.println(" ms");
  printPinMap();
}

static void printHelp() {
  Serial.println("YOSHIMITSU commands (FACE I console):");
  Serial.println("  FLASHER  enter FACE II (pocket flasher)");
  Serial.println("  STATUS   mode + bridge counters + pin map");
  Serial.println("  HELP     this list");
}

static void printBootBanner(void) {
  Serial.println();
  Serial.println("YOSHIMITSU · the Hermetic Shinobi — one board, two faces.");
  Serial.println("  FACE I  CRSF→PWM converter   (active)");
  Serial.println("  FACE II PteronautOS flasher   (double-tap BOOT to enter)");
  Serial.println("  STATUS / FLASHER / HELP");
  printPinMap();                    // boot POST: the wiring is always visible
}

static void enterMode(Mode next) {
  if (next == mode) return;
  mode = next;

  if (mode == MODE_CONVERTER) {
    // Receiver powers up; CRSF resumes on UART1. The bridge UART idles.
    rxPower(true);
    attachServos();
    setLed();
    Serial.println("YOSHIMITSU: FACE I — CRSF→PWM converter. Servos live, receiver powered.");
  } else {
    // Kill servo drive, power the receiver so esptool can see it through the bridge.
    detachServos();
    rxPower(true);
    setLed();
    Serial.println("YOSHIMITSU: FACE II — pocket flasher. USB↔UART bridge live.");
    Serial.println("YOSHIMITSU:   tap = restart · double-tap = bootloader · long-press = FACE I");
  }
}

static void handleUsb() {
  if (mode == MODE_FLASHER) {
    pumpBridge();          // transparent, no line parsing
    if (Serial2.overflow()) recoverBridgeUart();
    return;
  }

  // FACE I: the USB is a free console (no binary traffic) — high-level commands.
  static char line[24];
  static uint8_t n = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (n < sizeof(line) - 1) line[n++] = c;
    if (c == '\n' || c == '\r') {
      line[n] = '\0';
      if      (strncmp(line, "FLASHER", 7) == 0) enterMode(MODE_FLASHER);
      else if (strncmp(line, "FLASH",   5) == 0) enterMode(MODE_FLASHER);
      else if (strncmp(line, "STATUS",  6) == 0) printStatus();
      else if (strncmp(line, "HELP",    4) == 0) printHelp();
      n = 0;
    }
    if (n >= sizeof(line) - 1) n = 0;    // overflow guard
  }
}

static void handleBootButton() {
  static uint8_t  last = HIGH;
  static uint32_t pressStartMs = 0;
  static uint32_t lastTapMs = 0;
  static uint8_t  tapCount = 0;
  static bool     longFired = false;

  uint8_t  now = digitalRead(0);                // BOOT button, pulled up
  uint32_t ms  = millis();

  if (now != last) {
    last = now;
    if (now == LOW) {
      pressStartMs = ms;
      longFired = false;
    } else {
      // released
      if (!longFired) {
        uint32_t held = ms - pressStartMs;
        if (held >= LONG_PRESS_MS) {
          longFired = true;
          tapCount = 0;
          if (mode == MODE_FLASHER) enterMode(MODE_CONVERTER);   // long-press → FACE I
        } else if (held >= DEBOUNCE_MS) {
          tapCount++;
          if (tapCount == 1) lastTapMs = ms;
        }
      }
    }
    return;
  }

  // stable level
  if (now == LOW) {
    if (!longFired && (ms - pressStartMs) >= LONG_PRESS_MS) {
      longFired = true;
      tapCount = 0;
      if (mode == MODE_FLASHER) enterMode(MODE_CONVERTER);
    }
  } else {
    if (tapCount >= 2) {
      tapCount = 0;
      if (mode == MODE_CONVERTER) enterMode(MODE_FLASHER);               // FACE I ×2 → FACE II
      else danceBegin(true, DANCE_ANNOUNCE_BOOTLOADER);                  // FACE II ×2 → bootloader
    } else if (tapCount == 1 && (ms - lastTapMs) > PRESS_WINDOW_MS) {
      tapCount = 0;
      if (mode == MODE_FLASHER) danceBegin(false, DANCE_ANNOUNCE_RESTART);   // FACE II single → restart
    }
  }
}

// =============================================================================

static bool postDone = false;

void setup() {
  pinMode(RX_PWR_PIN, OUTPUT);
  pinMode(RX_BOOT_PIN, OUTPUT);
  pinMode(0, INPUT_PULLUP);                       // BOOT button
  if (MODE_LED_PIN >= 0) pinMode(MODE_LED_PIN, OUTPUT);

  bootAssert(false);                              // BOOT released
  rxPower(true);                                  // receiver powered by default

  Serial.begin(115200);                           // USB CDC
  Serial1.begin(CRSF_BAUD, SERIAL_8N1, CRSF_UART_RX_PIN, CRSF_UART_TX_PIN);   // FACE I
  Serial2.begin(BRIDGE_BAUD, SERIAL_8N1, BRIDGE_RX_PIN, BRIDGE_TX_PIN);       // FACE II

  mode = MODE_CONVERTER;
  attachServos();
  setLed();
}

void loop() {
  if (!postDone && millis() >= BOOT_PRINT_MS) {   // USB settle gate, no delay()
    postDone = true;
    printBootBanner();
  }
  handleBootButton();
  handleUsb();
  pumpDance();
  if (mode == MODE_CONVERTER) {
    pumpCrsf();
  }
  // FACE II bridging happens inside handleUsb() → pumpBridge().
}