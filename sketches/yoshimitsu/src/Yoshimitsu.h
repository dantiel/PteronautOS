// =============================================================================
//  YOSHIMITSU · Yoshimitsu.h — the Hermetic Shinobi (library core)
// -----------------------------------------------------------------------------
//  ONE library, TWO targets, SIX stances. The library carries the whole core;
//  the sketch is only a thin relaxed config shell. Every default lives in
//  Yoshimitsu_Loadout.h — the starting loadout. Update the library (git pull /
//  new release) without ever re-touching your sketch.
//
//  Ronin never looks back.
// =============================================================================
#ifndef YOSHIMITSU_H
#define YOSHIMITSU_H

#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include "Yoshimitsu_Loadout.h"
#include "CompanionTransport.h"
#include "PreparedMotion.h"
#include "MotionServo.h"
static Companion::Parser companionParser;
static Motion::Receiver motionReceiver;
static Motion::Intent motionIntent;
static bool motionActive = false;
static float motionPhase = 0, motionCyclesPerUs = 0;
static uint32_t motionLastUs = 0, motionLastMs = 0;

/* ── Target detection ── */
#if defined(ARDUINO_ARCH_RP2040)
  #define YOSHI_RP2040 1
  #define YOSHI_ESP32   0
  #include <Servo.h>
#elif defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP32S3)
  #define YOSHI_RP2040 0
  #define YOSHI_ESP32   1
  #include <ESP32Servo.h>
#else
  #error "YOSHIMITSU targets ESP32-S3 or RP2040 only — no other core."
#endif

/* ── Onboard WS2812B aurora (RP2040-Tiny/Zero) ── */
// Defaults ON on RP2040 (the Tiny/Zero ship an onboard WS2812B); the ESP32-S3
// boards carry a plain status LED (LED_PIN) instead, so it stays OFF there.
// Force it off with `#define YOSHI_RGB 0` (e.g. when NeoPixel isn't installed).
#if YOSHI_RP2040
  #if !defined(YOSHI_RGB)
    #define YOSHI_RGB 1
  #endif
  #if YOSHI_RGB
    #if __has_include(<Adafruit_NeoPixel.h>)
      #include <Adafruit_NeoPixel.h>
    #else
      #error "YOSHI_RGB is ON but <Adafruit_NeoPixel.h> is missing — install the Adafruit NeoPixel library, or #define YOSHI_RGB 0."
    #endif
  #endif
#else
  #if !defined(YOSHI_RGB)
    #define YOSHI_RGB 0
  #endif
#endif

/* ── Buttonless RESET-tap bookkeeping (RP2040) ── */
#if YOSHI_RP2040
  #include <EEPROM.h>
  #if __has_include(<hardware/rtc.h>)
    #include <hardware/rtc.h>
    #if !defined(YOSHI_RTC)
      #define YOSHI_RTC 1
    #endif
  #else
    #if !defined(YOSHI_RTC)
      #define YOSHI_RTC 0
    #endif
  #endif
#endif

/* ── Optional Zephyrus gyro link (MPU6050 over Wire) ── */
#if !defined(YOSHI_GYRO)
  #define YOSHI_GYRO YOSHI_GYRO_DEFAULT
#endif
#if YOSHI_GYRO
  #include <Wire.h>
#endif

/* ── JIGUANG (極光 — the aurora) — the storyteller/administrator ── */
// Hermetic compile gate. 1 = omnipresent: the 極光 sigil is written everywhere
// and the storyteller may speak. 0 = hermetically deactivated: the lamb — not
// one 極光 byte is compiled in, MEDITATION stays a pure flasher bridge.
#if !defined(JIGUANG)
  #define JIGUANG JIGUANG_DEFAULT
#endif  // BOARD PROFILES

// =============================================================================
//  BOARD PROFILES
// =============================================================================
#if YOSHI_RP2040
  #if !defined(BOARD_RP2040_TINY) && !defined(BOARD_RP2040_ZERO)
    #define BOARD_RP2040_TINY 1          // default — the Tiny (Zero shares its pinout)
  #endif
  #if defined(BOARD_RP2040_ZERO)
    #define YOSHI_DEFAULT(n)  RP2040_ZERO_##n##_DEFAULT
    #define YOSHI_BOARD_NAME  "RP2040-Zero"
  #elif defined(BOARD_RP2040_TINY)
    #define YOSHI_DEFAULT(n)  RP2040_TINY_##n##_DEFAULT
    #define YOSHI_BOARD_NAME  "RP2040-Tiny"
  #else
    #error "Pick a board: #define BOARD_RP2040_TINY or BOARD_RP2040_ZERO"
  #endif
  #ifndef CRSF_UART_NUM
    #define CRSF_UART_NUM YOSHI_DEFAULT(CRSF_UART_NUM)
  #endif
  #ifndef CRSF_TX_PIN
    #define CRSF_TX_PIN YOSHI_DEFAULT(CRSF_TX_PIN)
  #endif
  #ifndef CRSF_RX_PIN
    #define CRSF_RX_PIN YOSHI_DEFAULT(CRSF_RX_PIN)
  #endif
  #ifndef CRSF_BAUD
    #define CRSF_BAUD YOSHI_DEFAULT(CRSF_BAUD)
  #endif
  #ifndef BRIDGE_UART_NUM
    #define BRIDGE_UART_NUM YOSHI_DEFAULT(BRIDGE_UART_NUM)
  #endif
  #ifndef BRIDGE_TX_PIN
    #define BRIDGE_TX_PIN YOSHI_DEFAULT(BRIDGE_TX_PIN)
  #endif
  #ifndef BRIDGE_RX_PIN
    #define BRIDGE_RX_PIN YOSHI_DEFAULT(BRIDGE_RX_PIN)
  #endif
  #ifndef BRIDGE_BAUD
    #define BRIDGE_BAUD YOSHI_DEFAULT(BRIDGE_BAUD)
  #endif
  #ifndef SERVO_PIN_1
    #define SERVO_PIN_1 YOSHI_DEFAULT(SERVO_PIN_1)
  #endif
  #ifndef SERVO_PIN_2
    #define SERVO_PIN_2 YOSHI_DEFAULT(SERVO_PIN_2)
  #endif
  #ifndef SERVO_PIN_3
    #define SERVO_PIN_3 YOSHI_DEFAULT(SERVO_PIN_3)
  #endif
  #ifndef SERVO_PIN_4
    #define SERVO_PIN_4 YOSHI_DEFAULT(SERVO_PIN_4)
  #endif
  #ifndef SERVO_PIN_5
    #define SERVO_PIN_5 YOSHI_DEFAULT(SERVO_PIN_5)
  #endif
  #ifndef SERVO_PIN_6
    #define SERVO_PIN_6 YOSHI_DEFAULT(SERVO_PIN_6)
  #endif
  #ifndef SERVO_PIN_7
    #define SERVO_PIN_7 YOSHI_DEFAULT(SERVO_PIN_7)
  #endif
  #ifndef SERVO_PIN_8
    #define SERVO_PIN_8 YOSHI_DEFAULT(SERVO_PIN_8)
  #endif
  #ifndef RX_BOOT_PIN
    #define RX_BOOT_PIN YOSHI_DEFAULT(RX_BOOT_PIN)
  #endif
  #ifndef RX_PWR_PIN
    #define RX_PWR_PIN YOSHI_DEFAULT(RX_PWR_PIN)
  #endif
  #ifndef GYRO_SDA_PIN
    #define GYRO_SDA_PIN YOSHI_DEFAULT(GYRO_SDA_PIN)
  #endif
  #ifndef GYRO_SCL_PIN
    #define GYRO_SCL_PIN YOSHI_DEFAULT(GYRO_SCL_PIN)
  #endif
  #ifndef RGB_LED_PIN
    #define RGB_LED_PIN YOSHI_DEFAULT(RGB_LED_PIN)
  #endif
#elif YOSHI_ESP32
  #if !defined(BOARD_S3_WAVESHARE)
    #define BOARD_S3_WAVESHARE 1
  #endif
  #if defined(BOARD_S3_WAVESHARE)
    #define YOSHI_DEFAULT(n)  S3_WAVESHARE_##n##_DEFAULT
    #define YOSHI_BOARD_NAME  "ESP32-S3 (Waveshare)"
  #else
    #error "Pick a board: #define BOARD_S3_WAVESHARE"
  #endif
  #ifndef CRSF_RX_PIN
    #define CRSF_RX_PIN YOSHI_DEFAULT(CRSF_RX_PIN)
  #endif
  #ifndef CRSF_TX_PIN
    #define CRSF_TX_PIN YOSHI_DEFAULT(CRSF_TX_PIN)
  #endif
  #ifndef CRSF_BAUD
    #define CRSF_BAUD YOSHI_DEFAULT(CRSF_BAUD)
  #endif
  #ifndef BRIDGE_RX_PIN
    #define BRIDGE_RX_PIN YOSHI_DEFAULT(BRIDGE_RX_PIN)
  #endif
  #ifndef BRIDGE_TX_PIN
    #define BRIDGE_TX_PIN YOSHI_DEFAULT(BRIDGE_TX_PIN)
  #endif
  #ifndef BRIDGE_BAUD
    #define BRIDGE_BAUD YOSHI_DEFAULT(BRIDGE_BAUD)
  #endif
  #ifndef SERVO_PIN_1
    #define SERVO_PIN_1 YOSHI_DEFAULT(SERVO_PIN_1)
  #endif
  #ifndef SERVO_PIN_2
    #define SERVO_PIN_2 YOSHI_DEFAULT(SERVO_PIN_2)
  #endif
  #ifndef SERVO_PIN_3
    #define SERVO_PIN_3 YOSHI_DEFAULT(SERVO_PIN_3)
  #endif
  #ifndef SERVO_PIN_4
    #define SERVO_PIN_4 YOSHI_DEFAULT(SERVO_PIN_4)
  #endif
  #ifndef SERVO_PIN_5
    #define SERVO_PIN_5 YOSHI_DEFAULT(SERVO_PIN_5)
  #endif
  #ifndef SERVO_PIN_6
    #define SERVO_PIN_6 YOSHI_DEFAULT(SERVO_PIN_6)
  #endif
  #ifndef SERVO_PIN_7
    #define SERVO_PIN_7 YOSHI_DEFAULT(SERVO_PIN_7)
  #endif
  #ifndef SERVO_PIN_8
    #define SERVO_PIN_8 YOSHI_DEFAULT(SERVO_PIN_8)
  #endif
  #ifndef RX_PWR_PIN
    #define RX_PWR_PIN YOSHI_DEFAULT(RX_PWR_PIN)
  #endif
  #ifndef RX_BOOT_PIN
    #define RX_BOOT_PIN YOSHI_DEFAULT(RX_BOOT_PIN)
  #endif
  #ifndef GYRO_SDA_PIN
    #define GYRO_SDA_PIN YOSHI_DEFAULT(GYRO_SDA_PIN)
  #endif
  #ifndef GYRO_SCL_PIN
    #define GYRO_SCL_PIN YOSHI_DEFAULT(GYRO_SCL_PIN)
  #endif
  #ifndef LED_PIN
    #define LED_PIN YOSHI_DEFAULT(LED_PIN)
  #endif
#endif


// =============================================================================
//  UART ABSTRACTION  (one face per UART, two targets share the rest)
// =============================================================================

#if YOSHI_RP2040
  #if CRSF_UART_NUM == 0
    #define CRSF_SERIAL Serial1
  #elif CRSF_UART_NUM == 1
    #define CRSF_SERIAL Serial2
  #else
    #error "CRSF_UART_NUM must be 0 or 1"
  #endif
  #if BRIDGE_UART_NUM == 0
    #define BRIDGE_SERIAL Serial1
  #elif BRIDGE_UART_NUM == 1
    #define BRIDGE_SERIAL Serial2
  #else
    #error "BRIDGE_UART_NUM must be 0 or 1"
  #endif
  #if CRSF_UART_NUM != BRIDGE_UART_NUM
    #error "EP2 uses ONE UART: BRIDGE_UART_NUM must equal CRSF_UART_NUM"
  #endif
  #define CRSF_UART_DISPLAY   CRSF_UART_NUM
  #define BRIDGE_UART_DISPLAY BRIDGE_UART_NUM
#else
  #define CRSF_SERIAL   Serial1
  #define BRIDGE_SERIAL Serial1
  #define CRSF_UART_DISPLAY   1
  #define BRIDGE_UART_DISPLAY 1
#endif
static_assert(CRSF_TX_PIN == BRIDGE_TX_PIN && CRSF_RX_PIN == BRIDGE_RX_PIN,
              "Runtime and flashing must use the same receiver TX/RX pins");
static_assert(CRSF_BAUD == Companion::runtimeBaud && BRIDGE_BAUD == Companion::flashBaud,
              "Use 420000 runtime / 115200 flashing on the shared link");

static void crsfSerialBegin() {
  CRSF_SERIAL.end();
  companionParser.reset();
#if YOSHI_RP2040
  CRSF_SERIAL.setTX(CRSF_TX_PIN);
  CRSF_SERIAL.setRX(CRSF_RX_PIN);
  CRSF_SERIAL.begin(CRSF_BAUD);
#else
  CRSF_SERIAL.begin(CRSF_BAUD, SERIAL_8N1, CRSF_RX_PIN, CRSF_TX_PIN);
#endif
}

static void bridgeSerialBegin() {
  BRIDGE_SERIAL.end();
  companionParser.reset();
#if YOSHI_RP2040
  BRIDGE_SERIAL.setTX(BRIDGE_TX_PIN);
  BRIDGE_SERIAL.setRX(BRIDGE_RX_PIN);
  BRIDGE_SERIAL.begin(BRIDGE_BAUD);
#else
  BRIDGE_SERIAL.begin(BRIDGE_BAUD, SERIAL_8N1, BRIDGE_RX_PIN, BRIDGE_TX_PIN);
#endif
}

static void bridgeSerialRecover() {
  while (BRIDGE_SERIAL.available()) (void)BRIDGE_SERIAL.read();
#if YOSHI_RP2040
  BRIDGE_SERIAL.end();
  BRIDGE_SERIAL.setTX(BRIDGE_TX_PIN);
  BRIDGE_SERIAL.setRX(BRIDGE_RX_PIN);
  BRIDGE_SERIAL.begin(BRIDGE_BAUD);
#else
  BRIDGE_SERIAL.end();
  BRIDGE_SERIAL.begin(BRIDGE_BAUD, SERIAL_8N1, BRIDGE_RX_PIN, BRIDGE_TX_PIN);
#endif
}


// =============================================================================
//  CONSTANTS  (defaults live in Yoshimitsu_Loadout.h as *_DEFAULT)
// =============================================================================

#ifndef SERVO_COUNT_MAX
  #define SERVO_COUNT_MAX SERVO_COUNT_MAX_DEFAULT
#endif
#ifndef CHANNEL_COUNT
  #define CHANNEL_COUNT CHANNEL_COUNT_DEFAULT
#endif
#ifndef CRSF_RC_TYPE
  #define CRSF_RC_TYPE CRSF_RC_TYPE_DEFAULT
#endif
#ifndef CRSF_PAYLOAD
  #define CRSF_PAYLOAD CRSF_PAYLOAD_DEFAULT
#endif
#ifndef RAW_MIN
  #define RAW_MIN RAW_MIN_DEFAULT
#endif
#ifndef RAW_MAX
  #define RAW_MAX RAW_MAX_DEFAULT
#endif
#ifndef PWM_MIN
  #define PWM_MIN PWM_MIN_DEFAULT
#endif
#ifndef PWM_MAX
  #define PWM_MAX PWM_MAX_DEFAULT
#endif
#ifndef FAILSAFE_MS
  #define FAILSAFE_MS FAILSAFE_MS_DEFAULT
#endif
#ifndef BRIDGE_BURST
  #define BRIDGE_BURST BRIDGE_BURST_DEFAULT
#endif
#ifndef RX_PWR_MANUAL
  #define RX_PWR_MANUAL 0   // 1 = no P-MOSFET: receiver power/reset is the user's hand (two-phase MEDITATION)
#endif
#ifndef HOLD_OFF_MS
  #define HOLD_OFF_MS HOLD_OFF_MS_DEFAULT
#endif
#ifndef HOLD_ON_MS
  #define HOLD_ON_MS HOLD_ON_MS_DEFAULT
#endif
#ifndef RX_RESTART_MS
  #define RX_RESTART_MS RX_RESTART_MS_DEFAULT
#endif
#ifndef SETTLE_MS
  #define SETTLE_MS SETTLE_MS_DEFAULT
#endif
#ifndef BOOT_PRINT_MS
  #define BOOT_PRINT_MS BOOT_PRINT_MS_DEFAULT
#endif
#ifndef PRESS_WINDOW_MS
  #define PRESS_WINDOW_MS PRESS_WINDOW_MS_DEFAULT
#endif
#ifndef DEBOUNCE_MS
  #define DEBOUNCE_MS DEBOUNCE_MS_DEFAULT
#endif
#ifndef LONG_PRESS_MS
  #define LONG_PRESS_MS LONG_PRESS_MS_DEFAULT
#endif
#ifndef RESET_TAP_WINDOW_SEC
  #define RESET_TAP_WINDOW_SEC RESET_TAP_WINDOW_SEC_DEFAULT
#endif
#ifndef MPU_ADDR
  #define MPU_ADDR MPU_ADDR_DEFAULT
#endif
#ifndef MPU_WHOAMI
  #define MPU_WHOAMI MPU_WHOAMI_DEFAULT
#endif
#ifndef MPU_PWR
  #define MPU_PWR MPU_PWR_DEFAULT
#endif
#ifndef MPU_GYRO_CFG
  #define MPU_GYRO_CFG MPU_GYRO_CFG_DEFAULT
#endif
#ifndef MPU_GZ_H
  #define MPU_GZ_H MPU_GZ_H_DEFAULT
#endif
#ifndef GYRO_SCALE_LSB
  #define GYRO_SCALE_LSB GYRO_SCALE_LSB_DEFAULT
#endif
#ifndef GYRO_GAIN
  #define GYRO_GAIN GYRO_GAIN_DEFAULT
#endif
#ifndef GYRO_CORRECTION_SERVO
  #define GYRO_CORRECTION_SERVO GYRO_CORRECTION_SERVO_DEFAULT
#endif
#ifndef ARM_CHANNEL
  #define ARM_CHANNEL ARM_CHANNEL_DEFAULT
#endif

// =============================================================================
//  STANCES
// =============================================================================

enum Stance : uint8_t {
  STANCE_KINCHO,          // converter — the parry
  STANCE_MANJI_DRAGONFLY, // converter + gyro — the levitation
  STANCE_FLEA,            // power-cycle jig — the lift
  STANCE_MEDITATION,      // flasher bridge — the sponge-head
  STANCE_NSS,             // no-sword bench — direct servo
  STANCE_BACK_TURNED      // deceptive idle — the mirror
};
#define STANCE_COUNT 6

static const char* STANCE_NAME[STANCE_COUNT] = {
  "KINCHO", "MANJI_DRAGONFLY", "FLEA", "MEDITATION", "NSS", "BACK_TURNED"
};

// =============================================================================
//  STATE
// =============================================================================

#if YOSHI_MOTION_CORE
static MotionServo servos[SERVO_COUNT_MAX];
// Single producer/single consumer mailbox: the producer never rewrites a
// published slot until the consumer releases it. No spinlocks or allocation.
static Motion::Intent motionPending;
static uint32_t motionPendingMs, motionPendingEpoch;
static std::atomic<bool> motionReady{false}, motionSetupReady{false};
static std::atomic<uint32_t> motionEpoch{0};
static std::atomic<int32_t> motionCorrection{0};
static std::atomic<uint32_t> motionDeadlineMisses{0};
static std::atomic<uint32_t> motionMaxComputeUs{0};
static void motionCancel() { motionEpoch.fetch_add(1,std::memory_order_release); }
static void motionPublish(uint32_t now) {
    if(motionReady.load(std::memory_order_acquire)) return; // never block flight
    motionPending=motionIntent;
    motionPendingMs=now;
    motionPendingEpoch=motionEpoch.load(std::memory_order_acquire);
    motionReady.store(true,std::memory_order_release);
}
#else
static Servo servos[SERVO_COUNT_MAX];
static void motionCancel() {}
static void motionPublish(uint32_t) {}
#endif

// Generic servo pins — no channel NAMES. A servo is just an index; which CRSF
// channel feeds it lives in CHANNEL_TO_SERVO below. Reorder to taste.
static const int8_t SERVO_PIN[SERVO_COUNT_MAX] = {
  SERVO_PIN_1, SERVO_PIN_2, SERVO_PIN_3, SERVO_PIN_4,
  SERVO_PIN_5, SERVO_PIN_6, SERVO_PIN_7, SERVO_PIN_8
};
static uint8_t servoCount = 0;      // auto-counted from SERVO_PIN[] at boot

// CHANNEL_TO_SERVO[i] = CRSF channel (0-based) feeding servo i.
// The mixer/kernel owns channel meaning; the board only obeys.
static const uint8_t CHANNEL_TO_SERVO[SERVO_COUNT_MAX] = { 0, 1, 3, 4, 5, 6, 7, 8 };

static uint16_t channel[CHANNEL_COUNT];

enum { S_HEADER, S_LEN, S_TYPE, S_PAYLOAD, S_CRC };
static uint8_t  crsfState   = S_HEADER;
static uint8_t  frameLen    = 0, frameType = 0;
static uint8_t  payload[CRSF_PAYLOAD];
static uint8_t  payloadIdx  = 0;
static uint32_t lastGoodMs  = 0;

static Stance   stance     = STANCE_KINCHO;
static bool     rxPowered  = false;

// Bridge watchdog counters (visible via STATUS, never printed in flasher mode)
static uint32_t bridgeUsbToRx  = 0;
static uint32_t bridgeRxToUsb  = 0;
static uint32_t bridgeOverflows = 0;
static uint32_t lastBridgeMs   = 0;

// CRSF/stats ledger for JIGUANG's HIGHSCORE (arcade-style, told by 極光)
static uint32_t goodFrames     = 0;        // CRSF RC frames accepted (CRC clean)
static uint32_t failsafeHits   = 0;        // link-loss events (failsafe entries)
static bool     failsafeActive = false;

#if YOSHI_RGB
static Adafruit_NeoPixel rgb(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
#endif

// JIGUANG (極光 · the aurora) — the storyteller's voice level. 0 = the lamb
// (silent, but explicit commands still answer), 1 = tale (boot + stance
// narration), 2 = scroll (+ CRSF readout), 3 = omni (+ ELRS passthrough).
// JIGUANG / jiguang / 極光 never dies. It never seizes the stance — CRSF,
// servos and the bridge keep obeying the pose while the legend is told.
static uint8_t jigLevel = 0;               // boots as the lamb — JIGUANG wakes it

#if YOSHI_GYRO
static bool gyroConnected = false;
#endif

// MUSHIN (無心) — the muscle-memory mode. The spirit (PteronautOS) plans the
// wave and sends servo intents across the bridge UART; the muscle applies the
// local Zephyrus gyro PID and drives PWM at the muscle. The same two wires as
// the flasher bridge: MEDITATION carries esptool's SLIP, the converter stances
// carry MUSHIN. Boots ON (the cheatcode); admin-configurable via MUSHIN ON/OFF
// and persisted in flash (EEPROM byte 6 on RP2040).
static bool     mushin               = true;
static bool     mushinLinked         = false;  // an intent frame holds the link
static uint16_t mushinIntent[SERVO_COUNT_MAX]; // µs per servo from the spirit
static uint32_t mushinLastIntentMs   = 0;
static uint32_t mushinLastAnnounceMs = 0;
static uint32_t mushinFrames         = 0;      // xor-clean intent frames

// =============================================================================
//  JIGUANG (極光 · the aurora) — the storyteller / commentator / administrator
// -----------------------------------------------------------------------------
//  Yoshimitsu's voice is functional; JIGUANG's is the legend. The same ronin,
//  two tongues. JIGUANG narrates the boot, every stance change, and — when
//  awakened — the CRSF scroll, an arcade HIGHSCORE ledger and the receiver's
//  ELRS debug bytes passed through verbatim. Mute it and it is silent as a
//  lamb; wake it and the 極光 sigil is everywhere. It never seizes the stance.
// =============================================================================

#if JIGUANG
#if !defined(JIGUANG_PROMPT)
  #define JIGUANG_PROMPT JIGUANG_PROMPT_DEFAULT
#endif
#define JIG_SIGIL JIGUANG_PROMPT " "   // the throne sigil — type the prompt, then the command
#define JIG_CRSF_MS 250        // level-2 CRSF scroll cadence (~4 Hz)

static bool jigBegin(uint8_t minLevel) {   // open a JIGUANG line if allowed
  if (jigLevel < minLevel) return false;
  Serial.print(JIG_SIGIL);
  return true;
}
static void jigSay(uint8_t minLevel, const char* s) {
  if (jigBegin(minLevel)) Serial.println(s);
}
static void jigCmd() { Serial.print(JIG_SIGIL); }   // explicit admin — always answers

// The throne is guarded: these commands demand the JIGUANG_PROMPT prefix.
static bool adminRequired(const char* line) {
  return strncmp(line, "JIGUANG", 7) == 0 || strncmp(line, "LEGEND", 6) == 0 ||
         strncmp(line, "MUTE", 4) == 0    || strncmp(line, "CRSF", 4) == 0 ||
         strncmp(line, "SCORE", 5) == 0   || strncmp(line, "HIGHSCORE", 9) == 0 ||
         strncmp(line, "ELRS", 4) == 0    || strncmp(line, "BRIDGE", 6) == 0 ||
         strncmp(line, "ADMIN", 5) == 0   || strncmp(line, "MUSHIN", 6) == 0 ||
         strncmp(line, "DOC", 3) == 0     || strncmp(line, "SETUP", 5) == 0;
}

static uint32_t lastJigCrsfMs = 0;

// MEDITATION's console sub-state: true = JIGUANG's admin throne, false = the
// transparent flasher bridge. esptool's SLIP (0xC0) auto-yields to the bridge.
static bool medAdmin = true;

static uint16_t mapRaw(uint16_t raw);      // forward — defined in CRSF→PWM below

// One legend line per stance (level 1 — the tale)
static const char* STANCE_TALE[STANCE_COUNT] = {
  "KINCHO — 忍 the parry. The sword holds; the false falls at the hilt.",
  "MANJI_DRAGONFLY — the levitation. The gyro blades turn the wind.",
  "FLEA — 跳 the lift. The ronin vaults over low steel into the bootloader.",
  "MEDITATION — 禅 the sponge-head. 極光 sits cross-legged, ready to be flashed.",
  "NSS — 鞘 the no-sword bench. The blade sheathed; the bench bows.",
  "BACK_TURNED — 背 the mirror. It looks dead, but it never looks back."
};
static void jigNarrateStance(Stance s) { jigSay(1, STANCE_TALE[s]); }

static void printCrsfLine() {              // level-2 auto scroll (one compact line)
  if (!jigBegin(2)) return;
  Serial.print("CRSF[");
  for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
    Serial.print(channel[i]);
    if (i + 1 < CHANNEL_COUNT) Serial.print(',');
  }
  Serial.println("]");
}

static void printCrsfChannels() {          // explicit `CRSF` — the full 16-ch scroll
  jigCmd(); Serial.println("the 16 channels (raw → µs):");
  for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
    jigCmd();
    Serial.print("CH"); Serial.print((int)(i + 1));
    Serial.print(" raw="); Serial.print(channel[i]);
    Serial.print(" µs="); Serial.println(mapRaw(channel[i]));
  }
}

static void printHighscore() {             // explicit `SCORE` — the ronin's ledger
  jigCmd(); Serial.println("════ HIGHSCORE — the ronin's ledger ════");
  jigCmd(); Serial.print("good CRSF frames ... "); Serial.print(goodFrames); Serial.println();
  jigCmd(); Serial.print("failsafe events .... "); Serial.print(failsafeHits); Serial.println();
  jigCmd(); Serial.print("bridge USB→RX ...... "); Serial.print(bridgeUsbToRx); Serial.println(" bytes");
  jigCmd(); Serial.print("bridge RX→USB ...... "); Serial.print(bridgeRxToUsb); Serial.println(" bytes");
  jigCmd(); Serial.print("bridge overflows ... "); Serial.print(bridgeOverflows); Serial.println();
  jigCmd(); Serial.print("uptime ............. "); Serial.print(millis() / 1000UL); Serial.println(" s");
  jigCmd(); Serial.println("════════════════════════════════");
}

// `LEGEND` — the easter egg: the entire legend as one ASCII scroll. The ronin's
// whole story in a single unbroken scroll — stances, cheatcodes, the throne and
// the sigil that never dies. JIGUANG / jiguang / 極光 never dies.
static void jigLegend() {
  jigCmd(); Serial.println("════════════════════════════════════════════════");
  jigCmd(); Serial.println("   JIGUANG (極光) — THE LEGEND · ONE ASCII SCROLL");
  jigCmd(); Serial.println("════════════════════════════════════════════════");
  jigCmd(); Serial.println();
  jigCmd(); Serial.println("  YOSHIMITSU — the Hermetic Shinobi. One soul, six stances.");
  jigCmd(); Serial.println("  Configure once, flash once, never look back.");
  jigCmd(); Serial.println();
  for (uint8_t s = 0; s < STANCE_COUNT; s++) { jigSay(0, STANCE_TALE[s]); }
  jigCmd(); Serial.println();
  jigCmd(); Serial.println("  YOSHI   — the aurora cheatcode, always on: every stance breathes");
  jigCmd(); Serial.println("           its own colour and rhythm, read across the room.");
  jigCmd(); Serial.println("  JIGUANG — the storyteller / commentator / administrator. In MEDITATION");
  jigCmd(); Serial.println("           the sponge-head is the throne: commands from the USB-serial");
  jigCmd(); Serial.println("           heaven, esptool's SLIP yields it back to the flasher bridge.");
  jigCmd(); Serial.println("  MUSHIN 無心 — the muscle-memory mode. PteronautOS plans the wave");
  jigCmd(); Serial.println("           (the spirit), the muscle strikes here: intents cross the bridge,");
  jigCmd(); Serial.println("           the local gyro PID holds the crest, PIO PWM answers at the");
  jigCmd(); Serial.println("           muscle. Dual-core, glitchless — no other ELRS PWM board can");
  jigCmd(); Serial.println("           wear this cheatcode.");
  jigCmd(); Serial.println("  The bridge is transparent. The parry is the CRC. The levitation is the");
  jigCmd(); Serial.println("  gyro. The pose never seizes while the legend is told — 極光 never dies.");
  jigCmd(); Serial.println();
  jigCmd(); Serial.println("  Cheatcodes: KINCHO · MANJI · FLEA · MEDITATION · NSS · BACK · POSE n");
  jigCmd(); Serial.println("              極光 MUSHIN ON/OFF · 無心 · 極光 DOC · 極光 SETUP");
  jigCmd(); Serial.println("              極光 JIGUANG n · MUTE · CRSF · SCORE · ELRS · LEGEND");
  jigCmd(); Serial.println("════════════════════════════════════════════════");
  jigCmd(); Serial.println("  JIGUANG / jiguang / 極光 never dies. Never look back.");
  jigCmd(); Serial.println("════════════════════════════════════════════════");
}

// Level-3 omni: relay the receiver's ELRS debug bytes verbatim to the console.
static void pumpElrsDebug() {
  // Runtime bytes belong exclusively to the frame parser, never to a second
  // debug reader. Console diagnostics may report decoded state instead.
}
#else
#define JIG_SIGIL ""
static bool jigBegin(uint8_t) { return false; }
static void jigSay(uint8_t, const char*) {}
static void jigCmd() {}
static void jigNarrateStance(Stance) {}
static void printCrsfLine() {}
static void printCrsfChannels() {}
static void printHighscore() {}
static void pumpElrsDebug() {}
#endif

// =============================================================================
//  YOSHI (the aurora) — the RGB cheatcode · always-on stance story
// -----------------------------------------------------------------------------
//  The single onboard WS2812B never sits idle: every stance breathes its own
//  psychedelic aurora — a colour AND a rhythm that tell the pose's story, so
//  the ronin can be read across the room without a single serial byte. All
//  timings are millis() state machines (no delay, no float, no allocation),
//  throttled so the byte-exact flasher bridge is never disturbed.
// =============================================================================

#if YOSHI_RGB
#define RGB_FRAME_MS 20          // max repaint rate (~50 fps) — never hogs the bridge

// hue 0..255 → 24-bit RGB (integer wheel — the aurora's palette)
static uint32_t hueWheel(uint8_t h) {
  uint8_t r, g, b;
  uint8_t sector = h / 43;
  uint8_t rem    = (uint8_t)((h % 43) * 6u);
  uint8_t up     = rem;
  uint8_t down   = (uint8_t)(255 - rem);
  switch (sector) {
    case 0: r = 255; g = up;   b = 0;    break;
    case 1: r = down; g = 255; b = 0;    break;
    case 2: r = 0;    g = 255; b = up;   break;
    case 3: r = 0;    g = down; b = 255; break;
    case 4: r = up;   g = 0;    b = 255; break;
    default:r = 255; g = 0;    b = down; break;
  }
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// triangle wave 0..255..0 over periodMs — the breath / pulse
static uint8_t triWave(uint32_t ms, uint16_t periodMs) {
  uint16_t p = (uint16_t)((ms % periodMs) * 255u / periodMs);
  return (p < 128) ? (uint8_t)(p << 1) : (uint8_t)((255 - p) << 1);
}

// scale a 24-bit colour by k (0..255)
static uint32_t dimColor(uint32_t c, uint8_t k) {
  uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * k / 255);
  uint8_t g = (uint8_t)(((c >> 8)  & 0xFF) * k / 255);
  uint8_t b = (uint8_t)(( c        & 0xFF) * k / 255);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// JIGUANG (極光 · the aurora) — the invincible lens. A facet of the story:
// one slow full-spectrum wheel that weaves every stance colour — KINCHO
// green, the MANJI spin, FLEA amber, MEDITATION violet, NSS blue — into a
// single unbroken breath. A pure function of time that seizes nothing.
// JIGUANG / jiguang / 極光 never dies.
static uint32_t jiguang(uint32_t ms) {
  uint8_t h    = (uint8_t)((ms / 24) & 0xFF);   // the whole story as one wheel
  uint8_t wave = triWave(ms, 4200);             // the breath of the codex
  uint8_t k    = (uint8_t)(32 + wave / 4);      // dim enough to feel, never dark
  return dimColor(hueWheel(h), k);
}

static uint32_t rgbLast   = 0xFFFFFFFF;   // sentinel → force first paint
static uint32_t rgbLastMs = 0;

// One frame of the current stance's aurora. Call every loop().
static void pumpRgb() {
  uint32_t ms = millis();
  uint32_t c  = 0;

#if JIGUANG
  if (jigLevel >= 3) {
    c = jiguang(ms);                        // JIGUANG omni — the storyteller wears the aurora
  } else
#endif
  {
    switch (stance) {
    case STANCE_KINCHO: {                    // sword stance — the parry
      uint8_t k = (uint8_t)(24 + triWave(ms, 2200) / 3);   // slow green breath
      c = dimColor(0x00FF00, k);
      uint16_t p = (uint16_t)(ms % 2400);                  // crisp double parry-flash
      if (p < 120 || (p >= 160 && p < 200)) c = 0x00FF00;
      break;
    }
    case STANCE_MANJI_DRAGONFLY: {          // levitation — the gyro spin
      uint8_t h = (uint8_t)((ms / 7) & 0xFF);              // hue wheel spins like blades
      uint8_t k = (uint8_t)(40 + triWave(ms, 800) / 2);
      c = dimColor(hueWheel(h), k);
      break;
    }
    case STANCE_FLEA: {                      // the lift — frantic ascending strobe
      uint8_t h = (uint8_t)(30 + ((ms >> 2) & 0x1F));      // amber-orange jitter
      uint8_t k = (uint8_t)((ms >> 3) & 0xFF);
      if (k < 60) k = 60;
      c = dimColor(hueWheel(h), k);
      break;
    }
    case STANCE_MEDITATION: {                // sponge-head — ready to be flashed
      uint8_t k = (uint8_t)(8 + triWave(ms, 3400) / 4);    // near-dark violet, slow breath
      c = dimColor(0x8000FF, k);
      if ((uint16_t)(ms % 3400) < 90) c = 0x400080;        // the heartbeat thump
      break;
    }
    case STANCE_NSS: {                       // no-sword — blade sheathed, speed up
      c = dimColor(0x0000FF, 20);
      uint16_t p = (uint16_t)(ms % 1600);                  // sharp triple-tap blink
      if (p < 80 || (p >= 120 && p < 160) || (p >= 200 && p < 240)) c = 0x0040FF;
      break;
    }
    case STANCE_BACK_TURNED: {               // deceptive idle — the mirror's glint
      c = 0x000000;
      if ((ms / 1000) % 7 == 3) c = dimColor(0x200008, triWave(ms, 120));
      break;
    }
    }
  }

  if (c != rgbLast && (ms - rgbLastMs >= RGB_FRAME_MS || rgbLast == 0xFFFFFFFF)) {
    rgbLast   = c;
    rgbLastMs = ms;
    rgb.setPixelColor(0, c);
    rgb.show();
  }
}

#endif // YOSHI_RGB

#if !YOSHI_RGB
static void pumpRgb() { /* no WS2812B onboard — the aurora sleeps */ }
#endif

static void setStanceLed() {
#if YOSHI_RGB
  rgbLast = 0xFFFFFFFF;                      // force an immediate repaint on transition
  pumpRgb();
#elif defined(LED_PIN)
  if (LED_PIN >= 0) {
    digitalWrite(LED_PIN, (stance == STANCE_KINCHO || stance == STANCE_MANJI_DRAGONFLY) ? HIGH : LOW);
  }
#endif
}

// =============================================================================
//  CRSF → PWM  (KINCHO + MANJI_DRAGONFLY)
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

#if YOSHI_GYRO
static int32_t gyroCachedRate=0;
static bool gyroInit() {
#if YOSHI_RP2040
  Wire.setSDA(GYRO_SDA_PIN);
  Wire.setSCL(GYRO_SCL_PIN);
  Wire.begin();
#if YOSHI_MOTION_CORE
  Wire.setClock(400000);
  Wire.setTimeout(2,true); // a failed sensor cannot monopolise the UART core
#endif
#else
  Wire.begin(GYRO_SDA_PIN, GYRO_SCL_PIN, 400000UL);
#endif
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_PWR); Wire.write(0x00);          // wake the sponge-head
  if (Wire.endTransmission() != 0) { gyroConnected = false; return false; }

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_WHOAMI);
  Wire.endTransmission(false);
  Wire.requestFrom((int)MPU_ADDR, 1);
  gyroConnected = (Wire.available() && Wire.read() == 0x68);

  if (gyroConnected) {                             // ±250 dps full-scale
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(MPU_GYRO_CFG); Wire.write(0x00);
    Wire.endTransmission();
  }
  return gyroConnected;
}

static int32_t gyroReadZRate() {                  // raw yaw rate, ±250 dps
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_GZ_H);
  if(Wire.endTransmission(false)!=0) { gyroConnected=false; return 0; }
  if(Wire.requestFrom((int)MPU_ADDR, 2)!=2 || Wire.available()<2) { gyroConnected=false; return 0; }
  return (int32_t)(int16_t)((Wire.read() << 8) | Wire.read());
}
static int32_t gyroZRate() {
#if YOSHI_MOTION_CORE
  return gyroCachedRate; // consumers never start extra I2C transactions
#else
  return gyroReadZRate();
#endif
}
#endif

static void applyChannels() {
  if (millis() - lastGoodMs > FAILSAFE_MS) {
    for (uint8_t i = 0; i < servoCount; i++) servos[i].writeMicroseconds(1500);
    if (!failsafeActive) {
      failsafeActive = true;
      failsafeHits++;
      jigSay(2, "FAILSAFE — the link fell; the wings centre. The parry holds.");
    }
    lastGoodMs = millis();
  } else {
    failsafeActive = false;
  }

#if YOSHI_GYRO
  int32_t gyroUs = 0;
  if (stance == STANCE_MANJI_DRAGONFLY && gyroConnected) {
    gyroUs = -(gyroZRate() * GYRO_GAIN) / GYRO_SCALE_LSB;
  }
#endif

  for (uint8_t i = 0; i < servoCount; i++) {
    if (mushinLinked) continue;          // MUSHIN — the muscle obeys the spirit's intents
    int32_t pwm = mapRaw(channel[CHANNEL_TO_SERVO[i]]);
#if YOSHI_GYRO
    if (i == GYRO_CORRECTION_SERVO) pwm += gyroUs;
#endif
    pwm = constrain(pwm, (int32_t)PWM_MIN, (int32_t)PWM_MAX);
    servos[i].writeMicroseconds((int)pwm);
  }
}

static void attachServos() {
  for (uint8_t i = 0; i < servoCount; i++) {
    if (!servos[i].attached()) servos[i].attach(SERVO_PIN[i], 500, 2500);
    servos[i].writeMicroseconds(1500);
  }
}

static void detachServos() {
  for (uint8_t i = 0; i < SERVO_COUNT_MAX; i++) {
    if (servos[i].attached()) servos[i].detach();
  }
}

// =============================================================================
//  MUSHIN (無心) — the muscle-memory mode · protocol v1 — the local wave core
// -----------------------------------------------------------------------------
//  Frame on the bridge UART: [0x9B][len][type][payload…][xor] with xor over
//  len+type+payload. 0x01 = spirit→muscle intent: v1 = 11-byte wave
//  parameters (throttle, flapFreq, ferocity, skew, slew, stance, rate
//  setpoints — the muscle computes phase + shapeWave locally per tick, so the
//  bit-banged bridge no longer limits flapping resolution); v0 = n × uint16 µs
//  (length-based fallback: len==11 → v1, else even → v0). 0x02 = muscle→spirit
//  announce (version byte 0 = MUSHIN_VER, servo count, gyro), 0x03 = muscle→spirit
//  gyro telemetry. The bridge is the same two wires as the flasher — in the
//  converter stances they speak MUSHIN, in MEDITATION they carry esptool's
//  SLIP untouched. No dynamic memory, no delay(), never blocking.
// =============================================================================

#define MUSHIN_SYNC      0x9B        // 無心 — the no-mind sync
#define MUSHIN_VER       1           // protocol version — 1 = parameter intents
#define MUSHIN_INTENT    0x01        // spirit → muscle: wave params / servo µs
#define MUSHIN_ANNOUNCE  0x02        // muscle → spirit: version + posture
#define MUSHIN_TELEMETRY 0x03        // muscle → spirit: gyro rate + correction
#define MUSHIN_SWEEP     0x05        // spirit → muscle: raw µs bench sweep (u16 LE)
#define MUSHIN_MAX_PAY   16          // 8 servos × 2 bytes
#define MUSHIN_INTENT_STALE_MS   500         // intents stale after this → CRSF path resumes
#define MUSHIN_INTENT_V1_LEN    11          // v1 parameter intent payload length
#define MUSHIN_TWO_PI_Q16       411775UL    // 2π·65536 — phase wrap in Q16 rad
#define MUSHIN_OMEGA_DHZ_Q16    41177       // 0.1 Hz·2π·65536 — ω per deci-Hz, Q16 rad/s
#define MUSHIN_COS_LUT_BITS     8           // 256-entry cosine LUT over [0, 2π)
#define MUSHIN_COS_LUT_SIZE     (1 << MUSHIN_COS_LUT_BITS)
#define MUSHIN_COS_ONE_Q14      16384       // 1.0 in Q14
#define MUSHIN_TOTBAND_THROTTLE 20          // throttle‰ ≤ this parks the wings at centre
#define MUSHIN_FAILSAFE_EASE_MS 250         // damped unlink: ease to centre, then release
#define MUSHIN_THROTTLE_MAX     1000        // throttle ceiling - mirrors the spirit's emit clamp
#define MUSHIN_SETPOINT_MAX     250         // dps ceiling for the v1 rate setpoints
#ifndef MUSHIN_PID_I_GAIN
  #define MUSHIN_PID_I_GAIN     4           // Q8 I-term gain for the muscle's crest PID
#endif
#define MUSHIN_PID_I_MAX        4000        // I-accumulator clamp (LSB·tick)

enum : uint8_t { MS_IDLE, MS_LEN, MS_TYPE, MS_PAY, MS_XOR };
static uint8_t msState = MS_IDLE, msLen = 0, msType = 0, msIdx = 0, msXor = 0;
static uint8_t msBuf[MUSHIN_MAX_PAY];

// v1 parameter intent as parsed from the wire (layout mirrors the spirit's
// MushinIntentV1: u16 throttle, u8 flapFreq/ferocity/slew/stance, i8 skew,
// i16 setRoll/setPitch). mushinIntent[] below remains the v0 µs fallback.
static struct {
  uint16_t throttle;
  uint8_t  flapFreq, ferocity, slew, stance;
  int8_t   skew;
  int16_t  setRoll, setPitch;
} mushinParam;
static uint8_t mushinV1         = 0;   // 1 = the last intent was a v1 parameter frame
static uint8_t mushinParamDirty = 0;   // first v1 frame after (re)link → seed the wave core

// Wave-core state (muscle-local phase accumulator, Q16 rad·µs world):
static uint32_t mwLastUs   = 0;        // last tick timestamp (µs)
static uint32_t mwLastApplyUs = 0;     // 1 kHz rate gate — the free-spinning loop must
                                       // not churn the division-heavy core per iteration
static uint32_t mwClampUs  = 0;        // last velocity-clamp timestamp (µs)
static uint32_t mwClampRemainder = 0; // fractional step budget; never bank whole steps
static uint8_t mwClampSlew = 0;      // denominator changes invalidate the remainder
static int32_t  mwCadence  = 0;        // Q16 rad/s approach value (unity-gain damped)
static int64_t  mwPhaseAcc = 0;        // 64-bit phase accumulator, Q16 rad·µs
static int32_t  mwIterm    = 0;        // crest PID I accumulator (LSB·tick)
static uint16_t mwWingL    = 1500;     // last left-wing µs (velocity-clamp anchor)
static uint16_t mwWingR    = 1500;     // last right-wing µs
static uint8_t  mwEasing   = 0;        // damped failsafe in progress
static uint32_t mwUnlinkMs = 0;        // easing start (ms)

// The dual tongue: JIGUANG narrates the MUSHIN link when awake; without the
// storyteller compiled in, the muscle works in silence (as it should).
static void mushinTale(const char* s) {
#if JIGUANG
  jigSay(1, s);
#else
  (void)s;
#endif
}

static void mushinParse(uint8_t b) {
  switch (msState) {
    case MS_IDLE:
      if (b == MUSHIN_SYNC) { msState = MS_LEN; msXor = 0; }
      break;
    case MS_LEN:
      msLen = b; msXor ^= b;
      msState = (msLen <= MUSHIN_MAX_PAY) ? MS_TYPE : MS_IDLE;
      break;
    case MS_TYPE:
      msType = b; msXor ^= b; msIdx = 0;
      msState = (msLen == 0) ? MS_XOR : MS_PAY;   // 0-length frame: no payload to swallow
      break;
    case MS_PAY:
      msBuf[msIdx++] = b; msXor ^= b;
      if (msIdx >= msLen) msState = MS_XOR;
      break;
    case MS_XOR:
      msState = MS_IDLE;
      if (b != msXor) return;
      if (msType == 4 && msLen == 0) { // STOP: RF/model-match lost on EP2
        motionCancel();
        motionActive = false;
        motionReceiver.reset();
        mushinLinked = false;
        mushinV1 = 0;
        mwEasing = 0;
        mwIterm = 0;
        mwWingL = mwWingR = 1500;
        for (uint8_t i = 0; i < servoCount; ++i)
          servos[i].writeMicroseconds(i<7 && motionIntent.kind[i]==6 ? 1000 : 1500);
        return;
      }
      if (msType == MUSHIN_SWEEP) {   // raw µs bench sweep from the WebUI
        if (msLen == 2) {
          uint16_t us = (uint16_t)(msBuf[0] | (msBuf[1] << 8));
          if (us < 900) us = 900; else if (us > 2100) us = 2100;
          for (uint8_t i = 0; i < servoCount; ++i)
            servos[i].writeMicroseconds(us);
        }
        return;
      }
      if (msType == MUSHIN_INTENT) {
        if (msLen == MUSHIN_INTENT_V1_LEN) {
          // v1 parameter frame — the muscle computes phase + shapeWave locally
          // Clamp to the spec envelope - the wire field is a u16, not a
          // guarantee: a forged throttle overshoots the easing anchor into
          // uint16 wrap (never trust a broken intent).
          uint16_t th = (uint16_t)(msBuf[0] | (msBuf[1] << 8));
          mushinParam.throttle = (th > MUSHIN_THROTTLE_MAX) ? MUSHIN_THROTTLE_MAX : th;
          mushinParam.flapFreq = msBuf[2];
          mushinParam.ferocity = msBuf[3];
          mushinParam.skew     = (int8_t)msBuf[4];
          mushinParam.slew     = msBuf[5];
          mushinParam.stance   = msBuf[6];  // parsed for the record ??? the muscle&#39;s own
                                            // CRSF stance stays authoritative (announce p[3])
          int16_t sr = (int16_t)(msBuf[7] | (msBuf[8] << 8));
          int16_t sp = (int16_t)(msBuf[9] | (msBuf[10] << 8));
          mushinParam.setRoll  = (sr > MUSHIN_SETPOINT_MAX) ? MUSHIN_SETPOINT_MAX : (sr < -MUSHIN_SETPOINT_MAX) ? -MUSHIN_SETPOINT_MAX : sr;
          mushinParam.setPitch = (sp > MUSHIN_SETPOINT_MAX) ? MUSHIN_SETPOINT_MAX : (sp < -MUSHIN_SETPOINT_MAX) ? -MUSHIN_SETPOINT_MAX : sp;
          if (!mushinV1) mushinParamDirty = 1;   // (re)link → seed the wave core
          mushinV1 = 1;
          mwEasing = 0;                          // fresh intent cancels the ease
        } else {
          // v0 µs intents — the old path, kept for a v0 spirit
          uint8_t n = (uint8_t)(msLen / 2);
          if (n > servoCount) n = servoCount;
          for (uint8_t i = 0; i < n; i++)
            mushinIntent[i] = (uint16_t)(msBuf[2 * i] | (msBuf[2 * i + 1] << 8));
          mushinV1 = 0;
        }
        if (!mushinLinked) mushinTale("MUSHIN linked — the spirit's intent arrives; the muscle strikes before the thought.");
        mushinLinked = true;
        mushinLastIntentMs = millis();
        mushinFrames++;
      }
      break;
  }
}

static void mushinSend(uint8_t type, const uint8_t* p, uint8_t n) {
  if (n > MUSHIN_MAX_PAY || (stance != STANCE_KINCHO && stance != STANCE_MANJI_DRAGONFLY)) return;
  uint8_t frame[MUSHIN_MAX_PAY + 4] = { MUSHIN_SYNC, n, type };
  uint8_t x = (uint8_t)(n ^ type);
  for (uint8_t i = 0; i < n; i++) { frame[i + 3] = p[i]; x ^= p[i]; }
  frame[n + 3] = x;
  Companion::send(CRSF_SERIAL, Companion::mushinType, frame, n + 4);
  bridgeRxToUsb += n;                   // the muscle also keeps the ledger honest
}

// The muscle calls out once per second: who it is, how many servos it holds,
// whether the gyro is linked — and the crest telemetry the spirit may watch.
static void mushinAnnounce() {
  uint8_t p[6];
  p[0] = MUSHIN_VER;
  p[1] = servoCount;
  p[2] = (uint8_t)(YOSHI_RP2040 ? 1 : 0);
  p[3] = (uint8_t)(stance == STANCE_MANJI_DRAGONFLY ? 1 : 0);
#if YOSHI_GYRO
  p[4] = gyroConnected ? 1 : 0;
#else
  p[4] = 0;
#endif
  p[5] = mushinLinked ? 1 : 0;
  mushinSend(MUSHIN_ANNOUNCE, p, 6);

  int32_t rate = 0, corr = 0;
#if YOSHI_GYRO
  if (gyroConnected) {
    rate = gyroZRate();
    corr = -(rate * GYRO_GAIN) / GYRO_SCALE_LSB;
  }
#endif
  uint8_t t[6];
  t[0] = (uint8_t)(rate & 0xFF);  t[1] = (uint8_t)((rate >> 8) & 0xFF);
  t[2] = (uint8_t)(corr & 0xFF);  t[3] = (uint8_t)((corr >> 8) & 0xFF);
  t[4] = mushinLinked ? 1 : 0;    t[5] = MUSHIN_VER;
  mushinSend(MUSHIN_TELEMETRY, t, 6);
}

// =============================================================================
//  MUSHIN v1 wave core — the muscle's own phase + shapeWave, per tick
// -----------------------------------------------------------------------------
//  The spirit streams parameters; the muscle strikes. Phase advances on a
//  64-bit accumulator (acc += cadence[Q16]·dt[µs], wrap at 2π·1e6), the
//  waveform comes from a 256-entry Q14 cosine LUT — no float, no malloc, no
//  delay(). KINCHO laws without gyro: throttle deadband parks the wings,
//  slew becomes a velocity clamp, link loss eases to centre before release.
// =============================================================================

// cos(2π·i/256) in Q14 (16384 = 1.0), generated once — no PROGMEM needed on
// RP2040/ESP32, the table lives in flash as ordinary const.
static const int16_t mushinCosLut[MUSHIN_COS_LUT_SIZE] = {
    16384, 16379, 16364, 16340, 16305, 16261, 16207, 16143,
    16069, 15986, 15893, 15791, 15679, 15557, 15426, 15286,
    15137, 14978, 14811, 14635, 14449, 14256, 14053, 13842,
    13623, 13395, 13160, 12916, 12665, 12406, 12140, 11866,
    11585, 11297, 11003, 10702, 10394, 10080, 9760, 9434,
    9102, 8765, 8423, 8076, 7723, 7366, 7005, 6639,
    6270, 5897, 5520, 5139, 4756, 4370, 3981, 3590,
    3196, 2801, 2404, 2006, 1606, 1205, 804, 402,
    0, -402, -804, -1205, -1606, -2006, -2404, -2801,
    -3196, -3590, -3981, -4370, -4756, -5139, -5520, -5897,
    -6270, -6639, -7005, -7366, -7723, -8076, -8423, -8765,
    -9102, -9434, -9760, -10080, -10394, -10702, -11003, -11297,
    -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395,
    -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
    -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986,
    -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
    -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143,
    -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
    -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842,
    -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
    -11585, -11297, -11003, -10702, -10394, -10080, -9760, -9434,
    -9102, -8765, -8423, -8076, -7723, -7366, -7005, -6639,
    -6270, -5897, -5520, -5139, -4756, -4370, -3981, -3590,
    -3196, -2801, -2404, -2006, -1606, -1205, -804, -402,
    0, 402, 804, 1205, 1606, 2006, 2404, 2801,
    3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765,
    9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395,
    13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986,
    16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379,
};

// cos(phaseQ16) in Q14: 8-bit LUT index, 8-bit linear interpolation. Full
// circle = 65536 (16-bit phase), 256-entry LUT ⇒ each entry spans 256 units;
// the full low byte is the inter-entry fraction (a 2-bit fraction would quantize
// the cosine's steepest slope to ~0.6%, audible as a staircase on the wings).
static int16_t mushinCosQ14(uint32_t phaseQ16) {
  uint32_t i = (phaseQ16 >> 8) & (MUSHIN_COS_LUT_SIZE - 1);   // 8-bit index
  uint8_t  f = (uint8_t)(phaseQ16 & 0xFF);                    // 8-bit fraction
  int32_t  a = mushinCosLut[i];
  int32_t  b = mushinCosLut[(i + 1) & (MUSHIN_COS_LUT_SIZE - 1)];
  return (int16_t)((a * (256 - f) + b * f) >> 8);
}

// Advance the muscle's phase. Unity-gain damping k=10 mirrors the spirit's
// FlappingOscillator (10·dt ≤ 1e6 ⇒ never overshoots). During damped
// failsafe the target is zero and the cadence decays ×0.9 per tick.
static void mushinWaveTick(uint32_t nowUs) {
  if (mushinParamDirty) {                 // first v1 frame after (re)link: seed
    mwLastUs = nowUs;                     // the clock — no dt jump
    mwClampUs = nowUs;                    // and the velocity-clamp clock — no link kick
    mwClampRemainder = 0;
    mwClampSlew = mushinParam.slew;
    mwPhaseAcc = 0;
    mwCadence = 0;
    mwEasing = 0;
    mushinParamDirty = 0;
  }
  uint32_t dtUs = nowUs - mwLastUs;
  mwLastUs = nowUs;
  if (dtUs > 100000UL) dtUs = 100000UL;
  if (dtUs == 0) return;  // repeated timestamp must not invent elapsed time

  int32_t targetQ16 = mwEasing ? 0 : (int32_t)mushinParam.flapFreq * MUSHIN_OMEGA_DHZ_Q16;
  mwCadence += (int32_t)(((int64_t)10 * (int64_t)(targetQ16 - mwCadence) * (int64_t)dtUs) / 1000000);
  if (mwEasing) mwCadence = mwCadence * 9 / 10;
  mwPhaseAcc += (int64_t)mwCadence * (int64_t)dtUs;
  const int64_t wrap = (int64_t)MUSHIN_TWO_PI_Q16 * 1000000LL;   // 2π in Q16·µs
  while (mwPhaseAcc >= wrap) mwPhaseAcc -= wrap;
  while (mwPhaseAcc < 0) mwPhaseAcc += wrap;
}

// Fixed-point mirror of the spirit's shapeWave with shapeMix=0 (plateau+cos,
// the classic GralhaAzul family). Q14 in, Q14 out; one ferocity byte drives
// both half-strokes symmetrically; skew warps the downstroke +s and the
// upstroke −s (endpoints stay pinned). No float anywhere.
static int16_t mushinShapeWave(uint32_t phaseQ16) {
  int32_t ferocity = mushinParam.ferocity;
  if (ferocity > 100) ferocity = 100;
  const uint32_t limiarQ16 = MUSHIN_TWO_PI_Q16 / 2; // v1 is symmetric

  bool descida = phaseQ16 < limiarQ16;
  uint32_t t;                              // normalised half-stroke position, Q14
  if (descida) t = (uint32_t)(((uint64_t)phaseQ16 * 16384) / limiarQ16);
  else         t = (uint32_t)(((uint64_t)(phaseQ16 - limiarQ16) * 16384) / (MUSHIN_TWO_PI_Q16 - limiarQ16));
  if (t > 16384) t = 16384;

  // skew warp: t' = t + s·t·(1−t), s = skew% in Q14, mirrored on the upstroke
  int32_t s = ((int32_t)mushinParam.skew * 16384) / 100;
  if (s > 16384) s = 16384;
  if (s < -16384) s = -16384;
  if (!descida) s = -s;
  int32_t tw = (int32_t)t + ((s * (int32_t)t >> 14) * (16384 - (int32_t)t) >> 14);
  if (tw < 0) tw = 0;
  if (tw > 16384) tw = 16384;

  // Keep all 101 transmitted ferocity levels, not just nine rounded f8 bins.
  int32_t d  = ferocity * 16056 / 100; // 0.98 in Q14
  int32_t dh = d / 2;
  // The phase warp already shifts the plateau in time. A second dwell shift
  // would oppose it and reverse the sign of skew at high ferocity.
  int32_t frontDwell = dh;
  int32_t backDwell  = dh;

  int32_t wave;
  if (tw < frontDwell) wave = MUSHIN_COS_ONE_Q14;
  else if (tw > 16384 - backDwell) wave = -MUSHIN_COS_ONE_Q14;
  else {
    // cos(π·x), x∈[0,1]: LUT index = x·128 → phaseQ16 = x·32768
    uint32_t thetaQ16 = (uint32_t)(((int64_t)(tw - frontDwell) * 32768) / (16384 - d));
    wave = mushinCosQ14(thetaQ16);
  }
  return descida ? (int16_t)wave : (int16_t)-wave;
}

// Move a servo value toward its target by at most maxDelta µs per tick.
static uint16_t mushinEaseTo(uint16_t cur, int32_t target, int32_t maxDelta) {
  int32_t d = target - (int32_t)cur;
  if (d > maxDelta) d = maxDelta;
  else if (d < -maxDelta) d = -maxDelta;
  return (uint16_t)((int32_t)cur + d);
}

// Apply the v1 parameter frame: wings from the local wave core (L = +wave,
// R = −wave — code common = physical differential), crest from the rate
// setpoint. KINCHO maps the stick directly; MANJI with gyro runs a true PID
// attitude-hold (P on rate error, I on accumulated LSB, all integer).
static void mushinWaveApply(uint32_t nowUs) {
  mushinWaveTick(nowUs);
  int16_t wave = mushinShapeWave((uint32_t)(mwPhaseAcc / 1000000));

  // amplitude: throttle‰ → µs (1000 → ±500 µs, inside the 988..2012 window)
  int32_t amp  = (int32_t)mushinParam.throttle / 2;
  int32_t wing = 1500 + (amp * (int32_t)wave) / 16384;
  if (mushinParam.throttle <= MUSHIN_TOTBAND_THROTTLE || mwEasing) wing = 1500;

  // velocity clamp from slew (ms/60° → µs/ms ≈ 333/slew); 0 = unlimited
  int32_t maxDelta = 0x7FFFFFFF;
  uint32_t dtUs = nowUs - mwClampUs;
  mwClampUs = nowUs; // also track time while unlimited
  if (dtUs > 100000UL) dtUs = 100000UL;
  if (mwClampSlew != mushinParam.slew) {
    mwClampRemainder = 0;
    mwClampSlew = mushinParam.slew;
  }
  if (mushinParam.slew > 0) {
    const uint32_t denominator = (uint32_t)mushinParam.slew * 1000UL;
    const uint32_t budget = 333UL * dtUs + mwClampRemainder;
    maxDelta = (int32_t)(budget / denominator);
    mwClampRemainder = budget % denominator;
  } else {
    mwClampRemainder = 0;
  }
  if (servoCount >= 1) {
    mwWingL = mushinEaseTo(mwWingL, wing, maxDelta);
    servos[0].writeMicroseconds((int)constrain((int32_t)mwWingL, (int32_t)PWM_MIN, (int32_t)PWM_MAX));
  }
  if (servoCount >= 2) {
    mwWingR = mushinEaseTo(mwWingR, 3000 - wing, maxDelta);
    servos[1].writeMicroseconds((int)constrain((int32_t)mwWingR, (int32_t)PWM_MIN, (int32_t)PWM_MAX));
  }

  // crest/rudder on the correction servo index (default 2): rate setpoint
  // mapped straight to µs (±250 dps → ±200 µs), unless MANJI+gyro runs the PID.
  if (servoCount > GYRO_CORRECTION_SERVO) {
    int32_t rudUs = 1500 + (int32_t)mushinParam.setRoll * 200 / 250;
#if YOSHI_GYRO
    if (stance == STANCE_MANJI_DRAGONFLY && gyroConnected) {
      int32_t errLsb = (int32_t)mushinParam.setRoll * GYRO_SCALE_LSB - gyroZRate();
      mwIterm += errLsb;
      if (mwIterm > MUSHIN_PID_I_MAX) mwIterm = MUSHIN_PID_I_MAX;
      else if (mwIterm < -MUSHIN_PID_I_MAX) mwIterm = -MUSHIN_PID_I_MAX;
      int32_t corrUs = errLsb * GYRO_GAIN / GYRO_SCALE_LSB + mwIterm * MUSHIN_PID_I_GAIN / 256;
      if (corrUs > 200) corrUs = 200;
      else if (corrUs < -200) corrUs = -200;
      rudUs = 1500 + corrUs;
    }
#endif
    servos[GYRO_CORRECTION_SERVO].writeMicroseconds((int)constrain(rudUs, (int32_t)PWM_MIN, (int32_t)PWM_MAX));
  }
}

// The muscle layer: intents from the spirit override the local CRSF path while
// the link is fresh; the local gyro PID holds the crest (MANJI only). If the
// wire falls quiet, YOSHIMITSU returns to its own CRSF muscle — grace in
// degradation, never a dead wing.
static void mushinApply() {
  // Rate gate: the wave core runs at ≤1 kHz. The phase accumulator is µs-exact
  // (64-bit Q16 rad·µs), so slower ticks lose nothing; without the gate a
  // free-spinning loop would burn most of core 0 on software divisions.
  uint32_t us = micros();
  if (us - mwLastApplyUs < 1000UL) return;
  mwLastApplyUs = us;

  if (motionActive) {
    // Packet arrival never advances/restarts phase. A complete recipe only
    // changes the velocity and shape of this autonomous local oscillator.
    if (millis() - motionLastMs > 100) {
      motionCancel();
      motionActive = false;
      mushinLinked = false;
      motionReceiver.reset();
      for (uint8_t i=0; i<servoCount; ++i)
        servos[i].writeMicroseconds(i<7 && motionIntent.kind[i]==6 ? 1000 : 1500);
      return;
    }
#if YOSHI_MOTION_CORE
    // The second core owns motion time and hardware output. The I2C/USB/UART
    // loop may be late without stopping that clock or its own local failsafe.
    return;
#endif
    const uint32_t elapsed = us - motionLastUs;
    motionLastUs = us;
    if (motionIntent.flapping) {
      motionPhase += elapsed * motionCyclesPerUs;
      motionPhase -= (uint32_t)motionPhase;
    } else motionPhase = 0;
    uint16_t output[7];
    Motion::outputs(motionIntent, motionPhase, output);
#if YOSHI_GYRO
    const int32_t correction = stance == STANCE_MANJI_DRAGONFLY && gyroConnected
        ? -(gyroZRate() * GYRO_GAIN) / GYRO_SCALE_LSB : 0;
#endif
    for (uint8_t i=0; i<servoCount; ++i) {
      int32_t value = i<7 ? output[i] : 1500;
#if YOSHI_GYRO
      if (i<7 && motionIntent.kind[i]==5) value += correction;
#endif
      servos[i].writeMicroseconds(Motion::safePulse(value));
    }
    return;
  }

  uint32_t now = millis();
  if (!mushinLinked || now - mushinLastIntentMs > MUSHIN_INTENT_STALE_MS) {
    if (mushinLinked && mushinV1) {
      // Damped failsafe: ease the wings to centre with the velocity clamp,
      // decay the cadence, then release the link — never a hard tear.
      if (!mwEasing) { mwEasing = 1; mwUnlinkMs = now; }
      if (now - mwUnlinkMs < MUSHIN_FAILSAFE_EASE_MS) {
        mushinWaveApply(micros());
        return;
      }
    }
    if (mushinLinked) {
      mushinLinked = false;
      mushinV1 = 0;      // drop the v1 latch so the next link re-seeds the wave core
                         // (fresh clocks → no velocity-clamp kick at re-link)
      mushinTale("MUSHIN link quiet — YOSHIMITSU returns to its own CRSF muscle.");
    }
    return;
  }
  if (mushinV1) {
    mushinWaveApply(micros());
    return;
  }
#if YOSHI_GYRO
  int32_t gyroUs = 0;
  if (stance == STANCE_MANJI_DRAGONFLY && gyroConnected)
    gyroUs = -(gyroZRate() * GYRO_GAIN) / GYRO_SCALE_LSB;
#endif
  for (uint8_t i = 0; i < servoCount; i++) {
    int32_t pwm = mushinIntent[i];
    if (pwm < PWM_MIN || pwm > PWM_MAX) pwm = 1500;   // never trust a broken intent
#if YOSHI_GYRO
    if (i == GYRO_CORRECTION_SERVO) pwm += gyroUs;
#endif
    servos[i].writeMicroseconds((int)constrain(pwm, (int32_t)PWM_MIN, (int32_t)PWM_MAX));
  }
}

static void mushinPoll() {
  if (!mushin) return;
  if (stance != STANCE_KINCHO && stance != STANCE_MANJI_DRAGONFLY) return;
  if (millis() - mushinLastAnnounceMs >= 1000) {
    mushinLastAnnounceMs = millis();
    mushinAnnounce();
  }
  if (mushinLinked) mushinApply();
}

static void pumpCrsf() {
  uint16_t byteBudget=256; // leave core-0 time for scheduled sensor work
  while (byteBudget-- && CRSF_SERIAL.available()) {
    uint8_t b = (uint8_t)CRSF_SERIAL.read();
    if (!companionParser.feed(b, millis())) continue;
    if (companionParser.type() == Motion::frameType && mushin) {
      Motion::Intent candidate;
      if (motionReceiver.accept(companionParser.payload(), companionParser.length(), millis(), candidate)) {
        bool supported=true;
        for(uint8_t i=servoCount;i<7;++i) if(candidate.kind[i]!=7) supported=false;
        if(!supported) continue; // never silently drop an actuator from a model
        motionIntent=candidate;
        if (!motionActive) { motionPhase=0; motionLastUs=micros(); }
        motionCyclesPerUs = motionIntent.hz * 0.000001f;
        motionActive = true;
        mushinLinked = true;
        mushinV1 = 0;
        motionLastMs = millis();
        motionPublish(motionLastMs);
        mushinLastIntentMs = motionLastMs;
        ++mushinFrames;
      }
    } else if (companionParser.type() == Companion::mushinType && mushin) {
      msState = MS_IDLE;
      for (uint8_t i = 0; i < companionParser.length(); ++i) mushinParse(companionParser.payload()[i]);
    } else if (companionParser.type() == Companion::rcType && companionParser.length() == 22) {
      Companion::unpackChannels(companionParser.payload(), channel, CHANNEL_COUNT);
      lastGoodMs = millis();
      goodFrames++;
      // Companion mode never falls through to an unrelated raw-RC mixer when
      // recipes are missing. Explicit MUSHIN OFF still supports a stock RX.
      if (!mushin) applyChannels();
    }
  }

  if (!mushinLinked && millis() - lastGoodMs > FAILSAFE_MS) {
    for (uint8_t i = 0; i < servoCount; i++)
      servos[i].writeMicroseconds(mushin && i<7 && motionIntent.kind[i]==6 ? 1000 : 1500);
    lastGoodMs = millis();
  }
}

// =============================================================================
//  FLEA + MEDITATION — power-cycle jig + transparent bridge
// =============================================================================

static void rxPower(bool on) {
#if RX_PWR_MANUAL
  // No P-MOSFET — the receiver's power/reset is controlled by hand. We only own
  // the BOOT line; rxPowered is tracked purely for the STATUS display.
  rxPowered = on;
#else
  digitalWrite(RX_PWR_PIN, on ? LOW : HIGH);   // gate LOW = P-MOSFET on
  rxPowered = on;
#endif
}

static void bootAssert(bool hold) {
  // Open-drain: we only ever pull LOW (to hold the receiver's GPIO0 in ROM
  // bootloader) or go high-impedance (INPUT) to release it. We NEVER drive HIGH
  // — the EP2's internal weak pull-up owns the "HIGH" state. This is electrically
  // identical for the receiver, but it means an external GPIO0→GND switch (the
  // classic ESP "FLASH" button) can be added in parallel WITHOUT shorting 3.3V
  // through the RP2040 pin.
  if (hold) {
    pinMode(RX_BOOT_PIN, OUTPUT);
    digitalWrite(RX_BOOT_PIN, LOW);
  } else {
    pinMode(RX_BOOT_PIN, INPUT);   // high-Z: EP2's weak pull-up pulls GPIO0 HIGH
  }
}

#if YOSHI_RP2040
// Flash-backed EEPROM byte 7 = the last stance. Unlike the AON RTC (which dies
// on power-off), flash survives a power cut — so a shared-rail power-cycle brings
// the board back where it was (e.g. MEDITATION) instead of the KINCHO default.
static void nvmPersistStance() {
  if (stance == STANCE_FLEA) return;       // FLEA is a transient move, never a boot stance
  EEPROM.begin(16);
  EEPROM.write(7, (uint8_t)stance);
  EEPROM.commit();
}
#else
static void nvmPersistStance() {}
#endif

static void enterStance(Stance next);      // forward — defined in STANCE TRANSITIONS below

// ── Power-cycle dance — async millis() machine, NEVER blocking ──────────────
enum : uint8_t { DANCE_IDLE, DANCE_BOOT_HOLD, DANCE_POWER_OFF, DANCE_POWER_ON, DANCE_SETTLE };
static uint8_t  dancePhase    = DANCE_IDLE;
static bool     danceHoldBoot = false;
static uint32_t danceT0       = 0;

static void danceBegin(bool holdBoot) {
  if (dancePhase != DANCE_IDLE) return;         // one dance at a time
  danceHoldBoot = holdBoot;
#if RX_PWR_MANUAL
  // Two-phase MEDITATION — no P-MOSFET, so power/reset is the user's hand. We
  // steer BOOT only and leave the bridge transparent the whole time.
  bootAssert(holdBoot);
  if (holdBoot) {
    Serial.println("YOSHIMITSU: MEDITATION phase 1 — BOOT held LOW, bridge transparent.");
    Serial.println("YOSHIMITSU:   now power-cycle the receiver (its own + pad) to drop it into ROM bootloader,");
    Serial.println("YOSHIMITSU:   then click Flash (esptool --before no_reset).");
    Serial.println("YOSHIMITSU:   exit via KINCHO to release BOOT, then power-cycle again to run the new soul.");
  }
#else
  dancePhase    = DANCE_BOOT_HOLD;
  danceT0       = millis();
#endif
}

static void pumpDance() {
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
      if (millis() - danceT0 >= (danceHoldBoot ? HOLD_ON_MS : RX_RESTART_MS)) {
        if (danceHoldBoot) bootAssert(false);   // 4. release BOOT
        dancePhase = DANCE_SETTLE;
        danceT0 = millis();
      }
      break;
    case DANCE_SETTLE:
      if (millis() - danceT0 >= SETTLE_MS) {
        dancePhase = DANCE_IDLE;
        if (danceHoldBoot) {
          Serial.println("YOSHIMITSU: receiver dropped into ROM bootloader — run esptool with --before no_reset now.");
          // The lift has landed: MEDITATION is already the bridged stance. Do NOT
          // re-enter via enterStance() here — its `previous != STANCE_FLEA` guard
          // would re-trigger danceBegin() forever, and the dance gate in handleUsb()
          // keeps esptool's SLIP from ever reaching the receiver ("Failed to
          // connect"). Land directly instead.
          stance = STANCE_MEDITATION;
          medAdmin = true;                       // the sponge-head wakes as the administrator
          nvmPersistStance();                    // a power cut mid-flash must not forget MEDITATION
          jigNarrateStance(stance);
          setStanceLed();
        } else {
          Serial.println("YOSHIMITSU: receiver restarted — the new soul should be running.");
        }
      }
      break;
  }
}

// PURE transparent bridge — no line parsing, so esptool's binary SLIP flows
// free. Byte-exact, never blocking, fast enough for 460800-baud transfers.
static void pumpBridge() {
  bool moved = false;
  uint8_t n = 0;
  while (BRIDGE_SERIAL.available() && Serial.availableForWrite() && n < BRIDGE_BURST) {
    Serial.write(BRIDGE_SERIAL.read());
    n++;
    moved = true;
  }
  if (n) bridgeRxToUsb += n;
  n = 0;
  while (Serial.available() && BRIDGE_SERIAL.availableForWrite() && n < BRIDGE_BURST) {
    BRIDGE_SERIAL.write(Serial.read());
    n++;
    moved = true;
  }
  if (n) bridgeUsbToRx += n;
  if (moved) lastBridgeMs = millis();
}

// BACK_TURNED — the deceptive idle: a living UART mirror. It looks dead, but
// whatever you send on one UART emerges on the other. No state, only reflection.
static void pumpMirror() {
  // Single-link BACK_TURNED is silent. Echoing this UART into itself would
  // feed received frames back to the receiver, not bridge another device.
}

// =============================================================================
//  PIN VALIDATION — boot POST, before anything touches a wire
// =============================================================================

static bool pinCollides(int a, int b) { return a >= 0 && b >= 0 && a == b; }

static void validatePins() {
  bool bad = false;
  int gpioMax = YOSHI_RP2040 ? 29 : 48;

#define CHECK_PIN(NAME, P)                                                    \
  if ((P) < 0 || (P) > gpioMax) {                                             \
    Serial.print("YOSHIMITSU PIN ERROR: " NAME " out of range = ");           \
    Serial.println((int)(P)); bad = true;                                     \
  }

  CHECK_PIN("CRSF_TX",   CRSF_TX_PIN);
  CHECK_PIN("CRSF_RX",   CRSF_RX_PIN);
  CHECK_PIN("BRIDGE_TX", BRIDGE_TX_PIN);
  CHECK_PIN("BRIDGE_RX", BRIDGE_RX_PIN);
  CHECK_PIN("RX_BOOT",   RX_BOOT_PIN);
  CHECK_PIN("RX_PWR",    RX_PWR_PIN);
#if YOSHI_GYRO
  CHECK_PIN("GYRO_SDA",  GYRO_SDA_PIN);
  CHECK_PIN("GYRO_SCL",  GYRO_SCL_PIN);
#endif
  for (uint8_t i = 0; i < SERVO_COUNT_MAX; i++) {
    if (SERVO_PIN[i] > gpioMax) {                    // -1 (unused) is fine
      Serial.print("YOSHIMITSU PIN ERROR: SERVO_PIN_");
      Serial.print((int)(i + 1));
      Serial.print(" out of range = ");
      Serial.println((int)SERVO_PIN[i]); bad = true;
    }
  }
  // Servo slots must be contiguous from 1 — a gap would make the auto counter
  // mis-wire later slots to -1 and hang the bench. Fail fast instead.
  for (uint8_t i = 0; i + 1 < SERVO_COUNT_MAX; i++) {
    if (SERVO_PIN[i] < 0 && SERVO_PIN[i + 1] >= 0) {
      Serial.print("YOSHIMITSU PIN ERROR: SERVO_PIN_");
      Serial.print((int)(i + 1));
      Serial.print(" is empty but SERVO_PIN_");
      Serial.print((int)(i + 2));
      Serial.println(" is set — servo slots must be contiguous (fill 1..N, then -1).");
      bad = true;
    }
  }

  // pairwise collisions across every active pin
  struct { const char* name; int pin; } pins[] = {
    {"CRSF_TX",   CRSF_TX_PIN}, {"CRSF_RX",   CRSF_RX_PIN},
    {"RX_BOOT",   RX_BOOT_PIN}, {"RX_PWR",    RX_PWR_PIN},
#if YOSHI_GYRO
    {"GYRO_SDA",  GYRO_SDA_PIN}, {"GYRO_SCL",  GYRO_SCL_PIN},
#endif
  };
  const uint8_t nPin = (uint8_t)(sizeof(pins) / sizeof(pins[0]));
  for (uint8_t a = 0; a < nPin; a++) {
    for (uint8_t b = a + 1; b < nPin; b++) {
      if (pinCollides(pins[a].pin, pins[b].pin)) {
        Serial.print("YOSHIMITSU PIN COLLISION: ");
        Serial.print(pins[a].name); Serial.print(" and ");
        Serial.println(pins[b].name); bad = true;
      }
    }
    for (uint8_t s = 0; s < SERVO_COUNT_MAX; s++) {
      if (pinCollides(pins[a].pin, SERVO_PIN[s])) {
        Serial.print("YOSHIMITSU PIN COLLISION: ");
        Serial.print(pins[a].name); Serial.print(" and SERVO_PIN_");
        Serial.println((int)(s + 1)); bad = true;
      }
    }
  }
  for (uint8_t s = 0; s < SERVO_COUNT_MAX; s++) {
    for (uint8_t t = s + 1; t < SERVO_COUNT_MAX; t++) {
      if (pinCollides(SERVO_PIN[s], SERVO_PIN[t])) {
        Serial.print("YOSHIMITSU PIN COLLISION: SERVO_PIN_");
        Serial.print((int)(s + 1)); Serial.print(" and SERVO_PIN_");
        Serial.println((int)(t + 1)); bad = true;
      }
    }
  }

  if (bad) {
    Serial.println("YOSHIMITSU: halted — fix the PIN MAP and re-flash. Never look back.");
    for (;;) { /* hold — the ronin refuses to fly on a broken harness */ }
  }
}

// =============================================================================
//  CONSOLE
// =============================================================================

static void printPinMap() {
  Serial.print("YOSHIMITSU: board = ");
  Serial.print(YOSHI_BOARD_NAME);
  Serial.println(" — active pin map (edit in the config block)");
  Serial.print("  CRSF    UART"); Serial.print(CRSF_UART_DISPLAY);
  Serial.print("  TX=GP"); Serial.print(CRSF_TX_PIN);
  Serial.print("  RX=GP"); Serial.println(CRSF_RX_PIN);
  Serial.print("  BRIDGE  UART"); Serial.print(BRIDGE_UART_DISPLAY);
  Serial.print("  TX=GP"); Serial.print(BRIDGE_TX_PIN);
  Serial.print("  RX=GP"); Serial.println(BRIDGE_RX_PIN);
  for (uint8_t i = 0; i < servoCount; i++) {
    Serial.print("  SERVO_"); Serial.print((int)(i + 1));
    Serial.print("=GP"); Serial.print((int)SERVO_PIN[i]);
    Serial.print("  ← CH"); Serial.println((int)CHANNEL_TO_SERVO[i]);
  }
  Serial.print("  RX_BOOT=GP"); Serial.print(RX_BOOT_PIN);
  Serial.print("  RX_PWR=GP"); Serial.println(RX_PWR_PIN);
#if YOSHI_GYRO
  Serial.print("  GYRO  SDA=GP"); Serial.print(GYRO_SDA_PIN);
  Serial.print("  SCL=GP"); Serial.println(GYRO_SCL_PIN);
#endif
}

static void printGates() {
  Serial.println("YOSHIMITSU: compile-time gates");
  Serial.print("  board = "); Serial.println(YOSHI_BOARD_NAME);
#if JIGUANG
  Serial.print("  JIGUANG = 1 · voice level = "); Serial.println((int)jigLevel);
  Serial.print("  JIGUANG_PROMPT = "); Serial.println(JIGUANG_PROMPT);
#else
  Serial.println("  JIGUANG = 0 (hermetically deactivated)");
#endif
  Serial.print("  YOSHI_GYRO = "); Serial.println((int)YOSHI_GYRO);
  Serial.print("  YOSHI_RGB  = "); Serial.println((int)YOSHI_RGB);
}

static void printStatus() {
#if YOSHI_MOTION_CORE
  Serial.print("Motion core: max compute us="); Serial.print(motionMaxComputeUs.load(std::memory_order_acquire));
  Serial.print(" missed deadlines="); Serial.println(motionDeadlineMisses.load(std::memory_order_acquire));
#endif
  Serial.print("YOSHIMITSU: stance = ");
  Serial.print(STANCE_NAME[stance]);
#if YOSHI_GYRO
  if (stance == STANCE_MANJI_DRAGONFLY) {
    Serial.print(gyroConnected ? " (gyro linked)" : " (no gyro — degrading to KINCHO)");
  }
#endif
  Serial.print(" · receiver power = ");
  Serial.print(rxPowered ? "ON" : "OFF");
  Serial.print(" · MUSHIN (無心) = ");
  Serial.print(mushin ? "ON" : "OFF");
  if (mushin && mushinLinked) { Serial.print(" linked · "); Serial.print(mushinFrames); Serial.print(" intents"); }
#if JIGUANG
  Serial.print(" · JIGUANG voice = "); Serial.print((int)jigLevel);
#endif
  Serial.println();
  Serial.print("  bridge: USB→RX "); Serial.print(bridgeUsbToRx);
  Serial.print(" bytes · RX→USB "); Serial.print(bridgeRxToUsb);
  Serial.print(" bytes · overflows "); Serial.print(bridgeOverflows);
  Serial.print(" · idle "); Serial.print(millis() - lastBridgeMs);
  Serial.println(" ms");
  printPinMap();
}

static void printHelp() {
  Serial.println("YOSHIMITSU stances (type one):");
  Serial.println("  KINCHO       CRSF→PWM converter (the parry)");
  Serial.println("  MANJI/GYRO   converter + Zephyrus gyro (the levitation)");
  Serial.println("  FLEA/JIG     power-cycle jig → bootloader → MEDITATION");
  Serial.println("  MEDITATION   pocket flasher (the sponge-head, ready to be flashed)");
  Serial.println("  NSS/BENCH    No-Sword bench — direct servo, no radio");
  Serial.println("  BACK/TURN    receiver off, shared UART silent");
  Serial.println("  POSE <n>     jump to stance 0..5");
  Serial.println("  MUSHIN       muscle-memory mode (無心): the spirit plans, the muscle strikes");
  Serial.println("  MUSHIN ON/OFF/?  arm / disarm / report the no-mind bridge");
  Serial.println("  STATUS       stance + counters + pin map");
  Serial.println("  GATES        compile-time flags (board, JIGUANG, gyro, RGB)");
  Serial.println("  SERVO i us   (NSS only) drive servo i to microseconds");
  Serial.println("  HELP         this list");
#if JIGUANG
  Serial.println("JIGUANG (極光) — the storyteller / commentator / administrator:");
  Serial.println("  Administrator commands need the 極光 prefix — type 極光 first.");
  Serial.println("  DOC          the cheatcode catalog (this whole scroll)");
  Serial.println("  SETUP        the meditation wizard — stance, voice, MUSHIN, power");
  Serial.println("  JIGUANG      toggle the voice (lamb · tale)");
  Serial.println("  JIGUANG 0..3 set level: 0 lamb · 1 tale · 2 scroll · 3 omni");
  Serial.println("  MUTE         silence the storyteller (the lamb)");
  Serial.println("  LEGEND       the whole legend as one ASCII scroll (the easter egg)");
  Serial.println("  CRSF         print the 16-channel scroll (raw → µs)");
  Serial.println("  SCORE        arcade HIGHSCORE ledger (frames, failsafes, bridge)");
  Serial.println("  ELRS         toggle the receiver's debug passthrough (omni)");
  Serial.println("  BRIDGE/ADMIN (MEDITATION) yield to / retake the flasher console");

#endif
}


#if JIGUANG
static void printDocs() {
  jigCmd(); Serial.println("DOC — the cheatcode catalog:");
  jigCmd(); Serial.println("  stances   KINCHO · MANJI · FLEA · MEDITATION · NSS · BACK · POSE n");
  jigCmd(); Serial.println("  voice     JIGUANG 0..3 · MUTE · CRSF · SCORE · ELRS · LEGEND");
  jigCmd(); Serial.println("  muscle    MUSHIN ON/OFF/? — the no-mind bridge");
  jigCmd(); Serial.println("  throne    ADMIN · BRIDGE — yield / retake the flasher console");
  jigCmd(); Serial.println("  bench     SERVO i us — direct servo drive (NSS)");
  jigCmd(); Serial.println("  gates     board · JIGUANG · YOSHI_GYRO · YOSHI_RGB · JIGUANG_PROMPT");
}

static void printSetup() {
  jigCmd(); Serial.println("SETUP — the meditation wizard:");
  jigCmd(); Serial.print("  stance    "); Serial.println(STANCE_NAME[stance]);
  jigCmd(); Serial.print("  voice     JIGUANG level "); Serial.println((int)jigLevel);
  jigCmd(); Serial.print("  muscle    MUSHIN "); Serial.println(mushin ? "ON — boots armed" : "OFF — disarmed");
  jigCmd(); Serial.print("  receiver  "); Serial.println(rxPowered ? "powered" : "powered down");
  jigCmd(); Serial.println("  Change any setting with " JIGUANG_PROMPT " + command (see " JIGUANG_PROMPT " DOC).");
}
#endif

static void printBootBanner() {
  Serial.println();
  Serial.println("YOSHIMITSU · the Hermetic Shinobi — one sketch, two targets, six stances.");
  Serial.println("  KINCHO · MANJI_DRAGONFLY · FLEA · MEDITATION · NSS · BACK_TURNED");
  Serial.println("  Configure once, flash once, never look back.");
  printPinMap();
  Serial.println("  Type HELP for the scroll of stances.");
}

// =============================================================================
//  STANCE TRANSITIONS
// =============================================================================

static void enterStance(Stance next) {
  if (next == stance && next != STANCE_FLEA) return;
  const Stance previous = stance;
  motionCancel();
  motionActive = false;
  motionReceiver.reset();
  // A new stance cancels an unfinished boot dance; it must not later force
  // runtime back into MEDITATION or power up a receiver in a silent stance.
  dancePhase = DANCE_IDLE;
  bootAssert(false);
  companionParser.reset();
  msState = MS_IDLE;
  mushinLinked = false;
  mushinV1 = 0;
  mushinParamDirty = 1;
  stance = next;

  switch (stance) {
    case STANCE_KINCHO:
    case STANCE_MANJI_DRAGONFLY:
      crsfSerialBegin();
      lastGoodMs = millis();
      bootAssert(false);
      if (previous == STANCE_MEDITATION || previous == STANCE_FLEA) danceBegin(false);
      rxPower(true);
      attachServos();
#if YOSHI_GYRO
      if (stance == STANCE_MANJI_DRAGONFLY && !gyroConnected) gyroInit();
#endif
      Serial.print("YOSHIMITSU: ");
      Serial.println(stance == STANCE_KINCHO
        ? "KINCHO — CRSF→PWM converter. The parry holds; CRC discards the false."
        : "MANJI_DRAGONFLY — converter + Zephyrus gyro. The levitation spins.");
      break;

    case STANCE_FLEA:
      bridgeSerialBegin();
      detachServos();
      rxPower(true);                       // the dance manages power from here
      Serial.println("YOSHIMITSU: FLEA — the lift. Holding BOOT and power-cycling the receiver…");
      danceBegin(true);                    // drop into bootloader, then settle
      break;

    case STANCE_MEDITATION:
      bridgeSerialBegin();
      detachServos();                      // energy saving: no servo drive
      rxPower(true);                       // receiver powered so esptool sees it
      if (previous != STANCE_FLEA) danceBegin(true);
      Serial.println("YOSHIMITSU: MEDITATION — the sponge-head, ready to be flashed.");
      Serial.println("YOSHIMITSU:   the bridge is live. Exit: long-press BOOT (ESP32) or RESET (RP2040).");
#if JIGUANG
      medAdmin = true;                     // the sponge-head wakes as the administrator
      jigCmd();
      Serial.println("JIGUANG the administrator takes the throne. 極光 — while the wire is idle, type 極光 DOC for the cheatcode catalog.");
#endif
      break;

    case STANCE_NSS:
      detachServos();
      rxPower(false);                      // no RF in the bench pose
      attachServos();
      Serial.println("YOSHIMITSU: NSS — No-Sword bench. Blade sheathed; drive servos via SERVO i us.");
      break;

    case STANCE_BACK_TURNED:
      detachServos();
      rxPower(false);
      Serial.println("YOSHIMITSU: BACK_TURNED — receiver off, shared UART silent.");
      break;
  }
  nvmPersistStance();                  // remember where we stand — survives power-off
  jigNarrateStance(stance);            // 極光 tells the stance change (level 1)
  setStanceLed();
}

// =============================================================================
//  USB CONSOLE
// =============================================================================

static void handleServoCmd(const char* arg) {
  if (stance != STANCE_NSS) { Serial.println("YOSHIMITSU: SERVO only obeys in NSS (no-sword bench)."); return; }
  int idx = atoi(arg);
  const char* us = arg;
  while (*us && *us != ' ') us++;
  while (*us == ' ') us++;
  int pw = atoi(us);
  if (idx < 1 || idx > (int)servoCount) { Serial.println("YOSHIMITSU: SERVO index out of range."); return; }
  if (pw < PWM_MIN || pw > PWM_MAX) { Serial.println("YOSHIMITSU: SERVO microseconds out of 988..2012."); return; }
  servos[idx - 1].writeMicroseconds(pw);
  Serial.print("YOSHIMITSU: servo "); Serial.print(idx);
  Serial.print(" → "); Serial.print(pw); Serial.println(" µs");
}

static void runCommand(const char* line) {

  bool admin = false;
#if JIGUANG
  {
    const size_t plen = sizeof(JIGUANG_PROMPT) - 1;
    if (strncmp(line, JIGUANG_PROMPT, plen) == 0) {
      admin = true;
      line += plen;
      while (*line == ' ') line++;
    }
    if (!admin && adminRequired(line)) {
      jigCmd(); Serial.println("the throne is guarded — type " JIGUANG_PROMPT " and then the command.");
      return;
    }
  }
#endif
  if      (strncmp(line, "KINCHO", 6) == 0) enterStance(STANCE_KINCHO);
  else if (strncmp(line, "MANJI",  5) == 0 || strncmp(line, "GYRO", 4) == 0) enterStance(STANCE_MANJI_DRAGONFLY);
  else if (strncmp(line, "JIGUANG", 7) == 0) {
#if JIGUANG
    // JIGUANG (極光) — the storyteller/administrator. `JIGUANG` toggles the
    // voice (lamb ↔ tale); `JIGUANG n` sets the level 0..3. It never seizes
    // the stance: CRSF, servos and the bridge keep obeying the pose.
    const char* a = line + 7;
    while (*a == ' ') a++;
    if (*a >= '0' && *a <= '3') jigLevel = (uint8_t)(*a - '0');
    else jigLevel = (jigLevel == 0) ? 1 : 0;
    jigCmd();
    Serial.print("voice = "); Serial.print((int)jigLevel);
    Serial.println(jigLevel == 0 ? " — 0 the lamb (silent)."
                 : jigLevel == 1 ? " — 1 tale (boot + stance narration)."
                 : jigLevel == 2 ? " — 2 scroll (+ CRSF readout)."
                                 : " — 3 omni (+ ELRS passthrough).");
#else
    Serial.println("YOSHIMITSU: JIGUANG (極光) is hermetically deactivated — recompile with JIGUANG=1.");
#endif
  }
#if JIGUANG
  else if (strncmp(line, "LEGEND", 6) == 0) jigLegend();
  else if (strncmp(line, "MUTE", 4) == 0) {
    jigLevel = 0;
    jigCmd(); Serial.println("the lamb sleeps. 極光 is silent.");
  }
  else if (strncmp(line, "CRSF", 4) == 0) printCrsfChannels();
  else if (strncmp(line, "SCORE", 5) == 0 || strncmp(line, "HIGHSCORE", 9) == 0) printHighscore();
  else if (strncmp(line, "ELRS", 4) == 0) {
    jigLevel = (jigLevel >= 3) ? 1 : 3;
    jigCmd();
    Serial.println(jigLevel >= 3 ? "ELRS passthrough ON — 極光 relays the receiver's debug bytes."
                                 : "ELRS passthrough OFF — the storyteller holds its breath.");
  }
  else if (strncmp(line, "BRIDGE", 6) == 0) {
    if (stance == STANCE_MEDITATION) { medAdmin = false; jigCmd(); Serial.println("yielding to the transparent flasher bridge."); }
    else { jigCmd(); Serial.println("BRIDGE only applies in MEDITATION."); }
  }
  else if (strncmp(line, "ADMIN", 5) == 0) {
    if (stance == STANCE_MEDITATION) { medAdmin = true; jigCmd(); Serial.println("JIGUANG the administrator returns to the throne."); }
    else { jigCmd(); Serial.println("ADMIN only applies in MEDITATION."); }
  }
  else if (strncmp(line, "DOC", 3) == 0) printDocs();
  else if (strncmp(line, "SETUP", 5) == 0) printSetup();
#endif
  else if (strncmp(line, "FLEA",   4) == 0 || strncmp(line, "JIG", 3) == 0)  enterStance(STANCE_FLEA);
  else if (strncmp(line, "MEDITATION", 10) == 0 || strncmp(line, "MED", 3) == 0 ||
           strncmp(line, "FLASH",   5) == 0 || strncmp(line, "FLASHER", 7) == 0) enterStance(STANCE_MEDITATION);
  else if (strncmp(line, "NSS",    3) == 0 || strncmp(line, "BENCH", 5) == 0) enterStance(STANCE_NSS);
  else if (strncmp(line, "BACK",   4) == 0 || strncmp(line, "TURN", 4) == 0)  enterStance(STANCE_BACK_TURNED);
  else if (strncmp(line, "POSE",   4) == 0) {
    int n = atoi(line + 4);
    if (n >= 0 && n < STANCE_COUNT) enterStance((Stance)n);
    else Serial.println("YOSHIMITSU: POSE must be 0..5.");
  }
  else if (strncmp(line, "MUSHIN", 6) == 0) {
    // MUSHIN (無心) — the muscle-memory mode. `MUSHIN` reports, `MUSHIN ON|OFF`
    // arms/disarms the no-mind bridge and persists the choice in flash. The
    // flasher bridge in MEDITATION is never touched by this.
    const char* a = line + 6;
    while (*a == ' ') a++;
    if (*a == '\0' || *a == '?') {
      Serial.print("YOSHIMITSU: MUSHIN (無心) = ");
      Serial.print(mushin ? "ON" : "OFF");
      Serial.print(mushinLinked ? " · linked · " : " · listening · ");
      Serial.print(mushinFrames); Serial.println(" intent frames");
      return;
    }
    bool v = mushin;
    if      (strncmp(a, "ON",  2) == 0 || *a == '1') v = true;
    else if (strncmp(a, "OFF", 3) == 0 || *a == '0') v = false;
    else { Serial.println("YOSHIMITSU: MUSHIN ON|OFF|? — 無心 the muscle-memory mode."); return; }
    if (v != mushin) {
      if(stance==STANCE_KINCHO || stance==STANCE_MANJI_DRAGONFLY) {
        Serial.println("Change MUSHIN mode in NSS or MEDITATION; no EEPROM writes in flight mode.");
        return;
      }
      motionCancel(); motionActive=false; motionReceiver.reset();
      mushin = v;
#if YOSHI_RP2040
      EEPROM.begin(16); EEPROM.write(6, mushin ? 1 : 0); EEPROM.commit();
#endif
      mushinLinked = false;                    // re-link on the next intent
    }
    Serial.println(mushin
      ? "YOSHIMITSU: MUSHIN (無心) ON — the muscle strikes before the thought; the bridge speaks the no-mind protocol."
      : "YOSHIMITSU: MUSHIN (無心) OFF — the mind stands alone; the bridge keeps silence (flasher only).");
    mushinTale(mushin
      ? "MUSHIN 無心 — intents cross the wire; the muscle obeys before the thought arrives."
      : "MUSHIN 無心 folds — the muscle sleeps until the spirit calls again.");
  }
  else if (strncmp(line, "STATUS", 6) == 0) printStatus();
  else if (strncmp(line, "GATES",  5) == 0) printGates();
  else if (strncmp(line, "HELP",   4) == 0) printHelp();
  else if (strncmp(line, "SERVO",  5) == 0) handleServoCmd(line + 5);
  else Serial.println("YOSHIMITSU: unknown — type HELP.");
}

static char consoleLine[24];
static uint8_t consoleN = 0;

static void parseConsoleLine() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (consoleN < sizeof(consoleLine) - 1) consoleLine[consoleN++] = c;
    if (c == '\n' || c == '\r') {
      consoleLine[consoleN] = '\0';
      runCommand(consoleLine);
      consoleN = 0;
    }
    if (consoleN >= sizeof(consoleLine) - 1) consoleN = 0;
  }
}

#if JIGUANG
// MEDITATION — the sponge-head is JIGUANG's throne. By default it is the admin
// console (line parsing); the instant esptool's SLIP (0xC0) speaks, it yields
// to the byte-exact flasher bridge and stays there until MEDITATION re-enters.
static void handleMeditationUsb() {
  if (!medAdmin) {
    pumpBridge();
    if (BRIDGE_SERIAL.overflow()) bridgeSerialRecover();
    return;
  }
  while (Serial.available()) {
    int c = Serial.read();
    if (c == 0xC0) {                          // SLIP END — esptool is calling
      medAdmin = false;
      while (BRIDGE_SERIAL.available()) (void)BRIDGE_SERIAL.read();  // drop stale bootloader banner
      BRIDGE_SERIAL.write((uint8_t)c);        // hand the frame start onward
      lastBridgeMs = millis();
      return;
    }
    if (c == '\n' || c == '\r') {
      consoleLine[consoleN] = '\0'; consoleN = 0;
      runCommand(consoleLine);
    } else if (consoleN < sizeof(consoleLine) - 1) {
      consoleLine[consoleN++] = (char)c;
    } else {
      consoleN = 0;
    }
  }
}
#endif

static void handleUsb() {
  if ((stance == STANCE_MEDITATION || stance == STANCE_FLEA) && dancePhase != DANCE_IDLE) return;
  if (stance == STANCE_MEDITATION) {
#if JIGUANG
    handleMeditationUsb();
#else
    pumpBridge();
    if (BRIDGE_SERIAL.overflow()) bridgeSerialRecover();
#endif
    return;
  }
  if (stance == STANCE_FLEA) {
    pumpBridge();
    if (BRIDGE_SERIAL.overflow()) bridgeSerialRecover();
    return;
  }
  parseConsoleLine();
}

// =============================================================================
//  PHYSICAL INPUT — ESP32 BOOT button · RP2040 RESET-tap
// =============================================================================

#if YOSHI_ESP32
static void handleBootButton() {
  static uint8_t  last = HIGH;
  static uint32_t pressStartMs = 0;
  static uint32_t lastTapMs = 0;
  static uint8_t  tapCount = 0;
  static bool     longFired = false;

  uint8_t  now = digitalRead(0);              // GPIO0 BOOT button, pulled up
  uint32_t ms  = millis();

  if (now != last) {
    last = now;
    if (now == LOW) { pressStartMs = ms; longFired = false; }
    else {
      if (!longFired) {
        uint32_t held = ms - pressStartMs;
        if (held >= LONG_PRESS_MS) {
          longFired = true;
          tapCount = 0;
          if (stance == STANCE_MEDITATION || stance == STANCE_FLEA) enterStance(STANCE_KINCHO);
          else enterStance(STANCE_MEDITATION);
        } else if (held >= DEBOUNCE_MS) {
          tapCount++;
          if (tapCount == 1) lastTapMs = ms;
        }
      }
    }
    return;
  }

  if (now == LOW) {
    if (!longFired && (ms - pressStartMs) >= LONG_PRESS_MS) {
      longFired = true;
      tapCount = 0;
      if (stance == STANCE_MEDITATION || stance == STANCE_FLEA) enterStance(STANCE_KINCHO);
      else enterStance(STANCE_MEDITATION);
    }
  } else {
    if (tapCount >= 2) {
      tapCount = 0;
      if (stance == STANCE_MEDITATION || stance == STANCE_FLEA) danceBegin(true);   // re-drop
      else {
        // double-tap cycles KINCHO → MANJI → NSS → BACK → KINCHO …
        switch (stance) {
          case STANCE_KINCHO:          enterStance(STANCE_MANJI_DRAGONFLY); break;
          case STANCE_MANJI_DRAGONFLY: enterStance(STANCE_NSS); break;
          case STANCE_NSS:             enterStance(STANCE_BACK_TURNED); break;
          default:                     enterStance(STANCE_KINCHO); break;
        }
      }
    } else if (tapCount == 1 && (ms - lastTapMs) > PRESS_WINDOW_MS) {
      tapCount = 0;
      if (stance == STANCE_MEDITATION || stance == STANCE_FLEA) danceBegin(false);  // restart RX
    }
  }
}
#else
static void handleBootButton() { /* RP2040-Tiny has no readable button */ }
#endif

#if YOSHI_RP2040
static uint32_t nvmReadU32(int addr) {
  return (uint32_t)EEPROM.read(addr)
       | ((uint32_t)EEPROM.read(addr + 1) << 8)
       | ((uint32_t)EEPROM.read(addr + 2) << 16)
       | ((uint32_t)EEPROM.read(addr + 3) << 24);
}
static void nvmWriteU32(int addr, uint32_t v) {
  EEPROM.write(addr,     (uint8_t)(v & 0xFF));
  EEPROM.write(addr + 1, (uint8_t)((v >> 8) & 0xFF));
  EEPROM.write(addr + 2, (uint8_t)((v >> 16) & 0xFF));
  EEPROM.write(addr + 3, (uint8_t)((v >> 24) & 0xFF));
}
static uint32_t rtcSeconds() {
#if YOSHI_RTC
  datetime_t t;
  rtc_get_datetime(&t);
  return t.sec + 60ul * (t.min + 60ul * t.hour);
#else
  return 0;
#endif
}
#define NVM_MAGIC 0x59
static const Stance TAP_STANCE[5] = {
  STANCE_KINCHO, STANCE_MANJI_DRAGONFLY, STANCE_NSS, STANCE_BACK_TURNED, STANCE_MEDITATION
};
// The AON RTC survives a RUN-pin (RESET) reset but not a power-off, so a quick
// RESET tap is measurable; flash-backed EEPROM persists the tap streak. The USB
// POSE command remains the canonical switch — this is the buttonless gesture.
static void applyResetTapStance() {
  EEPROM.begin(16);
  uint32_t now = rtcSeconds();
  uint8_t m = EEPROM.read(6);              // MUSHIN persistence: 1 = strike on boot
  if (m == 0) mushin = false;
  else if (m == 1) mushin = true;
  if (EEPROM.read(0) != NVM_MAGIC) {
    EEPROM.write(0, NVM_MAGIC);
    EEPROM.write(1, 0);
    nvmWriteU32(2, now);
    EEPROM.write(7, STANCE_KINCHO);              // first boot → default stance persisted
    EEPROM.commit();
#if YOSHI_RTC
    rtc_init();                              // start the AON RTC on first-ever boot
#endif
    return;                                  // first boot → default KINCHO
  }
  uint8_t taps  = EEPROM.read(1);
  uint32_t last = nvmReadU32(2);
  uint32_t diff = (now >= last) ? (now - last) : (now + 86400ul - last);
  if (diff <= RESET_TAP_WINDOW_SEC) taps = (uint8_t)((taps + 1) % 5);
  else taps = 0;
  EEPROM.write(1, taps);
  nvmWriteU32(2, now);
  EEPROM.commit();
  if (taps > 0) {
    stance = TAP_STANCE[taps];
    nvmPersistStance();                         // a tapped stance is a real stance — remember it
    Serial.print("YOSHIMITSU: RESET-tap "); Serial.print((int)taps);
    Serial.print(" → "); Serial.println(STANCE_NAME[stance]);
  } else {
    // No fresh tap → this is a plain boot (possibly after a power cut). Restore
    // the last stance from flash, so a shared-rail power-cycle does not yank the
    // board back to KINCHO mid-flash.
    uint8_t saved = EEPROM.read(7);
    if (saved < STANCE_COUNT && saved != STANCE_FLEA) {
      stance = (Stance)saved;
      Serial.print("YOSHIMITSU: resume "); Serial.println(STANCE_NAME[stance]);
    }
  }
}
#else
static void applyResetTapStance() { /* ESP32-S3 uses the GPIO0 button */ }
#endif

// =============================================================================

static bool postDone = false;

void setup() {
#if RX_PWR_MANUAL
  pinMode(RX_PWR_PIN, INPUT);          // GPIO6 free — receiver power is the user's hand
#else
  pinMode(RX_PWR_PIN, OUTPUT);
#endif
  pinMode(RX_BOOT_PIN, INPUT);              // BOOT line starts released (high-Z)
#if YOSHI_ESP32
  pinMode(0, INPUT_PULLUP);
#endif
#if YOSHI_RGB
  rgb.begin();
  rgb.setBrightness(64);                     // YOSHI — vivid enough to read, not blinding
#endif
#if defined(LED_PIN)
  if (LED_PIN >= 0) pinMode(LED_PIN, OUTPUT);
#endif

  bootAssert(false);                          // BOOT released
  rxPower(true);                              // receiver powered by default

  Serial.begin(115200);                       // USB CDC

  // count live servos from SERVO_PIN[] (a -1 is an empty slot)
  for (uint8_t i = 0; i < SERVO_COUNT_MAX; i++) {
    if (SERVO_PIN[i] >= 0) servoCount = (uint8_t)(i + 1);
  }
  if (servoCount == 0) {
    Serial.println("YOSHIMITSU: no servos configured — set at least SERVO_PIN_1.");
    for (;;) { /* hold */ }
  }

  validatePins();                             // boot POST: range + collision

  crsfSerialBegin();

  stance = STANCE_KINCHO;
  applyResetTapStance();          // a RESET-tap streak may override the boot stance

  // Bring the boot stance to life (FLEA is never a boot stance — it is a move).
  if (stance == STANCE_KINCHO || stance == STANCE_MANJI_DRAGONFLY) {
    rxPower(true);
    // RP2040 RESET does not itself reset an independently powered EP2.
    // Recover it from ROM bootloader after a flashing session as well.
    danceBegin(false);
    attachServos();
#if YOSHI_GYRO
    if (stance == STANCE_MANJI_DRAGONFLY) gyroInit();
#endif
  } else if (stance == STANCE_NSS) {
    rxPower(false);
    attachServos();
  } else if (stance == STANCE_MEDITATION) {
    bridgeSerialBegin();
    danceBegin(true);
    rxPower(true);
  } else {                         // BACK_TURNED (and any future silent pose)
    rxPower(false);
  }
  jigNarrateStance(stance);        // 極光 tells the boot stance (no-op when the lamb)
  setStanceLed();
}

void loop() {
#if YOSHI_MOTION_CORE
  motionSetupReady.store(true,std::memory_order_release);
#if YOSHI_GYRO
  static uint32_t lastMotionGyroUs=0;
  const uint32_t gyroNow=micros();
  if(gyroNow-lastMotionGyroUs>=1000) {
    lastMotionGyroUs=gyroNow;
    gyroCachedRate=stance==STANCE_MANJI_DRAGONFLY && gyroConnected ? gyroReadZRate() : 0;
    motionCorrection.store(stance==STANCE_MANJI_DRAGONFLY && gyroConnected
        ? constrain(-(gyroZRate()*GYRO_GAIN)/GYRO_SCALE_LSB,-200,200) : 0,std::memory_order_release);
  }
#endif
#endif
  if (!postDone && millis() >= BOOT_PRINT_MS && stance != STANCE_MEDITATION && stance != STANCE_FLEA) {
    postDone = true;
    printBootBanner();
  }
  handleBootButton();
  handleUsb();
  pumpDance();
  pumpRgb();                          // YOSHI — the always-on stance aurora
  if (stance == STANCE_BACK_TURNED) {
    pumpMirror();
  } else if (stance == STANCE_KINCHO || stance == STANCE_MANJI_DRAGONFLY || stance == STANCE_NSS) {
    if (stance != STANCE_NSS) pumpCrsf();     // NSS drives servos manually, no CRSF
    mushinPoll();                     // MUSHIN — the muscle-memory layer (no-op when off)
  }
#if JIGUANG
  if (stance == STANCE_KINCHO || stance == STANCE_MANJI_DRAGONFLY) {
    if (jigLevel >= 2 && millis() - lastJigCrsfMs >= JIG_CRSF_MS) {
      lastJigCrsfMs = millis();
      printCrsfLine();                        // level 2 — the CRSF scroll
    }
    if (jigLevel >= 3) pumpElrsDebug();       // level 3 — the ELRS debug passthrough
  }
#endif
}
#if YOSHI_MOTION_CORE
// Only the motion worker uses this state. No I2C, USB, EEPROM, logging or
// blocking SDK calls on this core. PWM compare updates are hardware buffered.
void setup1() {}
void loop1() {
  if(!motionSetupReady.load(std::memory_order_acquire)) return;
  static Motion::Intent active;
  static bool running=false;
  static uint32_t epoch=0, received=0, lastUs=0, nextUs=0;
  static float phase=0, cyclesPerUs=0;
  const uint32_t now=micros();
  if((int32_t)(now-nextUs)<0) return;
  if(nextUs && now-nextUs>=1000) motionDeadlineMisses.fetch_add(1,std::memory_order_relaxed);
  nextUs=now+1000;
  const uint32_t currentEpoch=motionEpoch.load(std::memory_order_acquire);
  if(epoch!=currentEpoch) {
    if(running) for(uint8_t i=0;i<servoCount;++i)
      servos[i].writeMicroseconds(i<7 && active.kind[i]==6?1000:1500);
    running=false; epoch=currentEpoch;
  }
  if(motionReady.load(std::memory_order_acquire)) {
    if(motionPendingEpoch==epoch) {
      active=motionPending; received=motionPendingMs;
      if(!running) { phase=0; lastUs=now; }
      cyclesPerUs=active.hz*0.000001f;
      running=true;
    }
    motionReady.store(false,std::memory_order_release);
  }
  if(!running) return;
  uint16_t output[7];
  const bool stale=millis()-received>100;
  if(stale) {
    for(unsigned i=0;i<7;++i) output[i]=active.kind[i]==6?1000:1500;
    running=false;
  } else {
    if(active.flapping) { phase+=(now-lastUs)*cyclesPerUs; phase-=(uint32_t)phase; }
    else phase=0;
    Motion::outputs(active,phase,output);
  }
  lastUs=now;
  const int32_t correction=motionCorrection.load(std::memory_order_acquire);
  for(uint8_t i=0;i<servoCount;++i) {
    if(motionEpoch.load(std::memory_order_acquire)!=epoch) return;
    int32_t value=i<7?output[i]:1500;
    if(!stale && i<7 && active.kind[i]==5) value+=correction;
    servos[i].writeMicroseconds(Motion::safePulse(value));
  }
  const uint32_t elapsed=micros()-now;
  if(elapsed>motionMaxComputeUs.load(std::memory_order_relaxed))
    motionMaxComputeUs.store(elapsed,std::memory_order_release);
}
#endif
#endif  // YOSHIMITSU_H