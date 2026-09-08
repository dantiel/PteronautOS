// =============================================================================
//  YOSHIMITSU · the Hermetic Shinobi — ONE sketch, TWO targets, SIX stances
// -----------------------------------------------------------------------------
//  A single source of truth for the Waveshare RP2040-Tiny (the featherweight
//  workhorse) AND any native-USB ESP32-S3 devkit. Yoshimitsu never splits
//  himself into stray files: one blade, many poses.
//
//  Yoshimitsu is a ronin. He never looks back — a stance called BACK_TURNED
//  only ever opens a deceptive follow-up, never a retreat. Configure once,
//  flash once, and the harness stays untouched forever after.
//
//  STANCES (the board's modes — each a pose from the Manji scroll):
//
//    KINCHO          CRSF → PWM servo converter. The sword stance: its parry
//                    is the CRC check that discards every corrupt frame.
//                    Pure channel→servo mapping — no channel NAMES, no gyro.
//                    The mixer/kernel owns meaning; Yoshimitsu only obeys.
//
//    MANJI_DRAGONFLY Converter + Zephyrus gyro link. The levitation: it spins
//                    the MPU6050 (if one is wired) and feeds a yaw-rate
//                    correction into the crest servo. The standard pose for an
//                    ornithopter WITH a gyroscope. Without a gyro it degrades
//                    gracefully into a plain KINCHO.
//
//    FLEA            The power-cycle jig. It balances on the sword-hilt to
//                    evade low attacks — here it holds RX_BOOT and power-cycles
//                    the receiver so an EP2-class ESP8285 drops into its ROM
//                    bootloader. On completion it settles into MEDITATION.
//
//    MEDITATION      The pocket flasher. The sponge-head sits cross-legged,
//                    saving energy (servos detached, LED dimmed) — but it is
//                    NOT idle: it is READY TO BE FLASHED. A pure transparent
//                    USB↔UART bridge carries esptool's binary SLIP untouched.
//
//    NSS             No-Sword bench. Blade sheathed, speed up: direct servo
//                    PWM over USB, no RF, no receiver power. The test pose.
//
//    BACK_TURNED     The deceptive idle. It turns its back on every other
//                    stance and appears to do nothing — servos centred, LED
//                    near-dark. The delusional follow-up: it silently
//                    cross-wires the two UARTs (CRSF ↔ bridge) as a living
//                    mirror, so you can probe your wiring by simply sending
//                    bytes. It holds no state; it reflects the present only.
//
//  SWITCHING:
//
//    USB console (all boards — the canonical, reliable path):
//        KINCHO | MANJI | FLEA | MEDITATION | NSS | BACK | POSE <n>
//        STATUS | HELP | SERVO <i> <us>   (SERVO only works in NSS)
//
//    ESP32-S3 GPIO0 (BOOT) button:
//        double-tap  → cycle KINCHO → MANJI → NSS → BACK → KINCHO …
//        long-press  → MEDITATION
//        (in MEDITATION/FLEA) tap = restart receiver · double-tap = FLEA jig
//        long-press = back to KINCHO
//
//    RP2040-Tiny (no readable button — BOOT=BOOTSEL, RESET=RUN):
//        quick RESET taps (counted in flash-backed EEPROM against the AON RTC)
//        advance the stance: 1=MANJI 2=NSS 3=BACK 4=MEDITATION 5=KINCHO.
//        The adapter's BOOT is boot-strapping only; RESET reboots to KINCHO.
//
//  TIMING: this sketch NEVER calls delay(). The FLEA dance, the receiver
//  restart and every LED breath run on millis() state machines, so the bridge
//  stays byte-exact at all times.
//
//  JIGUANG (極光 · the aurora) — the YOSHI_RGB cheatcode. The single onboard
//  WS2812B never sits idle: every stance breathes its own psychedelic aurora,
//  a colour AND a rhythm that tell the pose's story. Read the ronin across
//  the room without one serial byte:
//    KINCHO          slow green breath + crisp double parry-flash
//    MANJI_DRAGONFLY a hue-wheel that spins like the gyro blades
//    FLEA            a frantic ascending amber strobe (the lift)
//    MEDITATION      near-dark violet + one slow heartbeat (ready to flash)
//    NSS             a sharp triple-tap blue blink (blade sheathed, speed up)
//    BACK_TURNED     near-black + an unpredictable crimson glint (the mirror)
//  Always on, always millis()-driven, throttled so it never disturbs the
//  byte-exact flasher bridge.
//
//  // homage to the Manji-clan shinobi of the soul — never print in docs.
//
// =============================================================================
//  PIN MAP — EDIT HERE  (validated at boot: range + collision, then a POST)
// -----------------------------------------------------------------------------
//  UART0 and UART1 are exposed on DIFFERENT pins. Pick your board profile and
//  set the pins you actually wired — the boot banner prints the active map.
// =============================================================================

#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

// ── Target detection ────────────────────────────────────────────────────────
#if defined(ARDUINO_ARCH_RP2040)
  #define YOSHI_RP2040 1
  #define YOSHI_ESP32   0
  #include <Servo.h>
#elif defined(ARDUINO_ARCH_ESP32)
  #define YOSHI_RP2040 0
  #define YOSHI_ESP32   1
  #include <ESP32Servo.h>
#else
  #error "YOSHIMITSU targets ESP32-S3 or RP2040 only — no other core."
#endif

// ── Optional onboard WS2812B (RP2040) ───────────────────────────────────────
#if YOSHI_RP2040 && __has_include(<Adafruit_NeoPixel.h>)
  #include <Adafruit_NeoPixel.h>
  #define YOSHI_RGB 1
#else
  #define YOSHI_RGB 0
#endif

// ── Buttonless RESET-tap bookkeeping (RP2040) ───────────────────────────────
#if YOSHI_RP2040
  #include <EEPROM.h>
  #if __has_include(<hardware/rtc.h>)
    #include <hardware/rtc.h>
    #define YOSHI_RTC 1
  #else
    #define YOSHI_RTC 0
  #endif
#endif

// ── Optional Zephyrus gyro link (MPU6050 over Wire) ─────────────────────────
#if !defined(YOSHI_GYRO)
  #define YOSHI_GYRO 1
#endif
#if YOSHI_GYRO
  #include <Wire.h>
#endif

// =============================================================================
//  BOARD PROFILES
// =============================================================================

#if YOSHI_RP2040
  #if !defined(BOARD_RP2040_TINY) && !defined(BOARD_RP2040_ZERO) && !defined(BOARD_CUSTOM)
    #define BOARD_RP2040_TINY 1
  #endif

  #if defined(BOARD_RP2040_TINY) || defined(BOARD_RP2040_ZERO)
    // Waveshare RP2040-Tiny / -Zero (identical pinout, featherweight).
    #define CRSF_UART_NUM   0        // UART0 = Serial1
    #define CRSF_TX_PIN     0        // UART0 TX (wired for completeness)
    #define CRSF_RX_PIN     1        // UART0 RX ← receiver CRSF TX
    #define CRSF_BAUD       420000UL

    #define BRIDGE_UART_NUM 1        // UART1 = Serial2
    #define BRIDGE_TX_PIN   8        // UART1 TX → receiver RX
    #define BRIDGE_RX_PIN   9        // UART1 RX ← receiver TX
    #define BRIDGE_BAUD     115200UL

    #define SERVO_PIN_1     2
    #define SERVO_PIN_2     3
    #define SERVO_PIN_3     4
    #define SERVO_PIN_4    -1
    #define SERVO_PIN_5    -1
    #define SERVO_PIN_6    -1
    #define SERVO_PIN_7    -1
    #define SERVO_PIN_8    -1

    #define RX_BOOT_PIN     5        // receiver GPIO0 (active low)
    #define RX_PWR_PIN      6        // P-MOSFET gate (LOW = receiver powered)
    #define GYRO_SDA_PIN    10       // MPU6050 SDA (optional, Wire)
    #define GYRO_SCL_PIN    11       // MPU6050 SCL
    #define RGB_LED_PIN     16       // onboard WS2812B
  #endif

  #ifdef BOARD_CUSTOM
    #define CRSF_UART_NUM   0        // 0 = Serial1 (UART0), 1 = Serial2 (UART1)
    #define CRSF_TX_PIN     0
    #define CRSF_RX_PIN     1
    #define CRSF_BAUD       420000UL
    #define BRIDGE_UART_NUM 1
    #define BRIDGE_TX_PIN   8
    #define BRIDGE_RX_PIN   9
    #define BRIDGE_BAUD     115200UL
    #define SERVO_PIN_1     2
    #define SERVO_PIN_2     3
    #define SERVO_PIN_3     4
    #define SERVO_PIN_4    -1
    #define SERVO_PIN_5    -1
    #define SERVO_PIN_6    -1
    #define SERVO_PIN_7    -1
    #define SERVO_PIN_8    -1
    #define RX_BOOT_PIN     5
    #define RX_PWR_PIN      6
    #define GYRO_SDA_PIN    10
    #define GYRO_SCL_PIN    11
    #define RGB_LED_PIN     16
  #endif
#endif

#if YOSHI_ESP32
  #if !defined(BOARD_S3_WAVESHARE) && !defined(BOARD_CUSTOM)
    #define BOARD_S3_WAVESHARE 1
  #endif

  #if defined(BOARD_S3_WAVESHARE)
    // Waveshare ESP32-S3-Tiny / -Micro / -Nano (native USB).
    #define CRSF_RX_PIN     44       // UART1 RX ← receiver TX
    #define CRSF_TX_PIN     43       // UART1 TX → receiver RX
    #define CRSF_BAUD       420000UL

    #define BRIDGE_RX_PIN   18       // UART2 RX ← receiver TX
    #define BRIDGE_TX_PIN   17       // UART2 TX → receiver RX
    #define BRIDGE_BAUD     115200UL

    #define SERVO_PIN_1     1
    #define SERVO_PIN_2     2
    #define SERVO_PIN_3     3
    #define SERVO_PIN_4     4
    #define SERVO_PIN_5     5
    #define SERVO_PIN_6     6
    #define SERVO_PIN_7     7
    #define SERVO_PIN_8     8

    #define RX_PWR_PIN      10       // P-MOSFET gate (LOW = receiver powered)
    #define RX_BOOT_PIN     9        // receiver BOOT pad (active low)
    #define GYRO_SDA_PIN    15       // MPU6050 SDA (optional, Wire)
    #define GYRO_SCL_PIN    16       // MPU6050 SCL
    #define LED_PIN         21       // status LED (-1 to disable)
  #endif

  #ifdef BOARD_CUSTOM
    #define CRSF_RX_PIN     44
    #define CRSF_TX_PIN     43
    #define CRSF_BAUD       420000UL
    #define BRIDGE_RX_PIN   18
    #define BRIDGE_TX_PIN   17
    #define BRIDGE_BAUD     115200UL
    #define SERVO_PIN_1     1
    #define SERVO_PIN_2     2
    #define SERVO_PIN_3     3
    #define SERVO_PIN_4     4
    #define SERVO_PIN_5     5
    #define SERVO_PIN_6     6
    #define SERVO_PIN_7     7
    #define SERVO_PIN_8     8
    #define RX_PWR_PIN      10
    #define RX_BOOT_PIN     9
    #define GYRO_SDA_PIN    15
    #define GYRO_SCL_PIN    16
    #define LED_PIN         21
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
  #if CRSF_UART_NUM == BRIDGE_UART_NUM
    #error "CRSF and BRIDGE need two DIFFERENT UARTs"
  #endif
  #define CRSF_UART_DISPLAY   CRSF_UART_NUM
  #define BRIDGE_UART_DISPLAY BRIDGE_UART_NUM
#else
  #define CRSF_SERIAL   Serial1
  #define BRIDGE_SERIAL Serial2
  #define CRSF_UART_DISPLAY   1
  #define BRIDGE_UART_DISPLAY 2
#endif

static void crsfSerialBegin() {
#if YOSHI_RP2040
  CRSF_SERIAL.setTX(CRSF_TX_PIN);
  CRSF_SERIAL.setRX(CRSF_RX_PIN);
  CRSF_SERIAL.begin(CRSF_BAUD);
#else
  CRSF_SERIAL.begin(CRSF_BAUD, SERIAL_8N1, CRSF_RX_PIN, CRSF_TX_PIN);
#endif
}

static void bridgeSerialBegin() {
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
//  CONSTANTS
// =============================================================================

#define SERVO_COUNT_MAX   8
#define CHANNEL_COUNT     16
#define CRSF_RC_TYPE      0x16
#define CRSF_PAYLOAD      22        // 16 ch × 11 bit = 22 bytes

#define RAW_MIN           172       // CRSF numeric window (PteronautOS)
#define RAW_MAX           1811
#define PWM_MIN           988
#define PWM_MAX           2012
#define FAILSAFE_MS       500

#define BRIDGE_BURST      8         // max bytes per direction per pass

// ── Timing ──────────────────────────────────────────────────────────────────
#define HOLD_OFF_MS       120       // receiver power-off during the dance
#define HOLD_ON_MS        900       // wait inside bootloader after re-power
#define RX_RESTART_MS     700       // settle after a plain restart
#define SETTLE_MS         80        // BOOT release settle
#define BOOT_PRINT_MS     150       // USB settle gate before the boot banner

// ── Button (ESP32) ──────────────────────────────────────────────────────────
#define PRESS_WINDOW_MS   1500
#define DEBOUNCE_MS       40
#define LONG_PRESS_MS     2000

// ── Buttonless RESET-tap (RP2040) ───────────────────────────────────────────
#define RESET_TAP_WINDOW_SEC 2

// ── Zephyrus gyro link ──────────────────────────────────────────────────────
#define MPU_ADDR          0x68
#define MPU_WHOAMI        0x75
#define MPU_PWR           0x6B
#define MPU_GYRO_CFG      0x1B
#define MPU_GZ_H          0x47
#define GYRO_SCALE_LSB    131       // ±250 dps
#define GYRO_GAIN         4         // µs of correction per dps (tune me)
#define GYRO_CORRECTION_SERVO 2     // servo index fed by the gyro (crest)
#define ARM_CHANNEL       4         // CRSF channel that arms (0-based)

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

static Servo servos[SERVO_COUNT_MAX];

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

#if YOSHI_RGB
static Adafruit_NeoPixel rgb(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
#endif

#if YOSHI_GYRO
static bool gyroConnected = false;
#endif

// =============================================================================
//  JIGUANG (極光) — the YOSHI_RGB aurora cheatcode · always-on stance story
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

static uint32_t rgbLast   = 0xFFFFFFFF;   // sentinel → force first paint
static uint32_t rgbLastMs = 0;

// One frame of the current stance's aurora. Call every loop().
static void pumpRgb() {
  uint32_t ms = millis();
  uint32_t c  = 0;

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
static bool gyroInit() {
#if YOSHI_RP2040
  Wire.setSDA(GYRO_SDA_PIN);
  Wire.setSCL(GYRO_SCL_PIN);
  Wire.begin();
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

static int32_t gyroZRate() {                       // raw yaw rate, ±250 dps
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_GZ_H);
  Wire.endTransmission(false);
  Wire.requestFrom((int)MPU_ADDR, 2);
  if (Wire.available() < 2) return 0;
  return (int32_t)(int16_t)((Wire.read() << 8) | Wire.read());
}
#endif

static void applyChannels() {
  if (channel[ARM_CHANNEL] <= 992) {               // disarmed: everything centred
    for (uint8_t i = 0; i < servoCount; i++) servos[i].writeMicroseconds(1500);
    return;
  }

#if YOSHI_GYRO
  int32_t gyroUs = 0;
  if (stance == STANCE_MANJI_DRAGONFLY && gyroConnected) {
    gyroUs = -(gyroZRate() * GYRO_GAIN) / GYRO_SCALE_LSB;
  }
#endif

  for (uint8_t i = 0; i < servoCount; i++) {
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
    if (!servos[i].attached()) servos[i].attach(SERVO_PIN[i], PWM_MIN, PWM_MAX);
    servos[i].writeMicroseconds(1500);
  }
}

static void detachServos() {
  for (uint8_t i = 0; i < SERVO_COUNT_MAX; i++) {
    if (servos[i].attached()) servos[i].detach();
  }
}

static void pumpCrsf() {
  while (CRSF_SERIAL.available()) {
    uint8_t b = (uint8_t)CRSF_SERIAL.read();

    switch (crsfState) {
      case S_HEADER:
        if (b == 0xC8 || b == 0xEE) crsfState = S_LEN;
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
        uint8_t crcBuf[CRSF_PAYLOAD + 2];
        crcBuf[0] = frameLen;
        crcBuf[1] = frameType;
        memcpy(crcBuf + 2, payload, CRSF_PAYLOAD);
        if (frameType == CRSF_RC_TYPE && crsfCrc8(crcBuf, CRSF_PAYLOAD + 2) == b) {
          for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
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
//  FLEA + MEDITATION — power-cycle jig + transparent bridge
// =============================================================================

static void rxPower(bool on) {
  digitalWrite(RX_PWR_PIN, on ? LOW : HIGH);   // gate LOW = P-MOSFET on
  rxPowered = on;
}

static void bootAssert(bool hold) {
  digitalWrite(RX_BOOT_PIN, hold ? LOW : HIGH);
}

// ── Power-cycle dance — async millis() machine, NEVER blocking ──────────────
enum : uint8_t { DANCE_IDLE, DANCE_BOOT_HOLD, DANCE_POWER_OFF, DANCE_POWER_ON, DANCE_SETTLE };
static uint8_t  dancePhase    = DANCE_IDLE;
static bool     danceHoldBoot = false;
static uint32_t danceT0       = 0;

static void danceBegin(bool holdBoot) {
  if (dancePhase != DANCE_IDLE) return;         // one dance at a time
  danceHoldBoot = holdBoot;
  dancePhase    = DANCE_BOOT_HOLD;
  danceT0       = millis();
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
          stance = STANCE_MEDITATION;           // the lift settles into the sponge-head
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
  while (CRSF_SERIAL.available() && BRIDGE_SERIAL.availableForWrite()) {
    BRIDGE_SERIAL.write(CRSF_SERIAL.read());
    lastBridgeMs = millis();
  }
  while (BRIDGE_SERIAL.available() && CRSF_SERIAL.availableForWrite()) {
    CRSF_SERIAL.write(BRIDGE_SERIAL.read());
    lastBridgeMs = millis();
  }
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

  // pairwise collisions across every active pin
  struct { const char* name; int pin; } pins[] = {
    {"CRSF_TX",   CRSF_TX_PIN}, {"CRSF_RX",   CRSF_RX_PIN},
    {"BRIDGE_TX", BRIDGE_TX_PIN}, {"BRIDGE_RX", BRIDGE_RX_PIN},
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
  Serial.println("YOSHIMITSU: active pin map (edit in the PIN MAP block)");
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

static void printStatus() {
  Serial.print("YOSHIMITSU: stance = ");
  Serial.print(STANCE_NAME[stance]);
#if YOSHI_GYRO
  if (stance == STANCE_MANJI_DRAGONFLY) {
    Serial.print(gyroConnected ? " (gyro linked)" : " (no gyro — degrading to KINCHO)");
  }
#endif
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
  Serial.println("YOSHIMITSU stances (type one):");
  Serial.println("  KINCHO       CRSF→PWM converter (the parry)");
  Serial.println("  MANJI/GYRO   converter + Zephyrus gyro (the levitation)");
  Serial.println("  FLEA/JIG     power-cycle jig → bootloader → MEDITATION");
  Serial.println("  MEDITATION   pocket flasher (the sponge-head, ready to be flashed)");
  Serial.println("  NSS/BENCH    No-Sword bench — direct servo, no radio");
  Serial.println("  BACK/TURN    deceptive idle — the UART mirror, never looks back");
  Serial.println("  POSE <n>     jump to stance 0..5");
  Serial.println("  STATUS       stance + counters + pin map");
  Serial.println("  SERVO i us   (NSS only) drive servo i to microseconds");
  Serial.println("  HELP         this list");
}

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

  stance = next;

  switch (stance) {
    case STANCE_KINCHO:
    case STANCE_MANJI_DRAGONFLY:
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
      detachServos();
      rxPower(true);                       // the dance manages power from here
      Serial.println("YOSHIMITSU: FLEA — the lift. Holding BOOT and power-cycling the receiver…");
      danceBegin(true);                    // drop into bootloader, then settle
      break;

    case STANCE_MEDITATION:
      detachServos();                      // energy saving: no servo drive
      rxPower(true);                       // receiver powered so esptool sees it
      Serial.println("YOSHIMITSU: MEDITATION — the sponge-head, ready to be flashed.");
      Serial.println("YOSHIMITSU:   the bridge is live. Exit: long-press BOOT (ESP32) or RESET (RP2040).");
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
      Serial.println("YOSHIMITSU: BACK_TURNED — the deceptive idle. It looks dead, but the UARTs mirror each other.");
      Serial.println("YOSHIMITSU:   send bytes on one UART and they emerge on the other. It never looks back.");
      break;
  }
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
  if      (strncmp(line, "KINCHO", 6) == 0) enterStance(STANCE_KINCHO);
  else if (strncmp(line, "MANJI",  5) == 0 || strncmp(line, "GYRO", 4) == 0) enterStance(STANCE_MANJI_DRAGONFLY);
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
  else if (strncmp(line, "STATUS", 6) == 0) printStatus();
  else if (strncmp(line, "HELP",   4) == 0) printHelp();
  else if (strncmp(line, "SERVO",  5) == 0) handleServoCmd(line + 5);
  else Serial.println("YOSHIMITSU: unknown — type HELP.");
}

static void handleUsb() {
  if (stance == STANCE_MEDITATION || stance == STANCE_FLEA) {
    pumpBridge();                            // transparent, no line parsing
    if (BRIDGE_SERIAL.overflow()) bridgeSerialRecover();
    return;
  }

  static char line[24];
  static uint8_t n = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (n < sizeof(line) - 1) line[n++] = c;
    if (c == '\n' || c == '\r') {
      line[n] = '\0';
      runCommand(line);
      n = 0;
    }
    if (n >= sizeof(line) - 1) n = 0;
  }
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
  if (EEPROM.read(0) != NVM_MAGIC) {
    EEPROM.write(0, NVM_MAGIC);
    EEPROM.write(1, 0);
    nvmWriteU32(2, now);
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
    Serial.print("YOSHIMITSU: RESET-tap "); Serial.print((int)taps);
    Serial.print(" → "); Serial.println(STANCE_NAME[stance]);
  }
}
#else
static void applyResetTapStance() { /* ESP32-S3 uses the GPIO0 button */ }
#endif

// =============================================================================

static bool postDone = false;

void setup() {
  pinMode(RX_PWR_PIN, OUTPUT);
  pinMode(RX_BOOT_PIN, OUTPUT);
#if YOSHI_ESP32
  pinMode(0, INPUT_PULLUP);
#endif
#if YOSHI_RGB
  rgb.begin();
  rgb.setBrightness(64);                     // JIGUANG — vivid enough to read, not blinding
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
  bridgeSerialBegin();

  stance = STANCE_KINCHO;
  applyResetTapStance();          // a RESET-tap streak may override the boot stance

  // Bring the boot stance to life (FLEA is never a boot stance — it is a move).
  if (stance == STANCE_KINCHO || stance == STANCE_MANJI_DRAGONFLY) {
    rxPower(true);
    attachServos();
#if YOSHI_GYRO
    if (stance == STANCE_MANJI_DRAGONFLY) gyroInit();
#endif
  } else if (stance == STANCE_NSS) {
    rxPower(false);
    attachServos();
  } else if (stance == STANCE_MEDITATION) {
    rxPower(true);
  } else {                         // BACK_TURNED (and any future silent pose)
    rxPower(false);
  }
  setStanceLed();
}

void loop() {
  if (!postDone && millis() >= BOOT_PRINT_MS) {
    postDone = true;
    printBootBanner();
  }
  handleBootButton();
  handleUsb();
  pumpDance();
  pumpRgb();                          // JIGUANG — the always-on stance aurora
  if (stance == STANCE_BACK_TURNED) {
    pumpMirror();
  } else if (stance == STANCE_KINCHO || stance == STANCE_MANJI_DRAGONFLY || stance == STANCE_NSS) {
    if (stance != STANCE_NSS) pumpCrsf();     // NSS drives servos manually, no CRSF
  }
}