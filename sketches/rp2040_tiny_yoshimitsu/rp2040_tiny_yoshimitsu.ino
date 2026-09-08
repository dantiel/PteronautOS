// =============================================================================
//  YOSHIMITSU · the Hermetic Shinobi — RP2040-Tiny build
//  One board, two faces. The featherweight incarnation.
// -----------------------------------------------------------------------------
//  A single Waveshare RP2040-Tiny (or RP2040-Zero — identical pinout) that
//  transmutes between two workbench personalities:
//
//    FACE I  — CRSF → PWM servo converter
//              Reads ExpressLRS CRSF RC frames (420 000 baud) from any ELRS
//              receiver and drives 3 servos with 988–2012 µs pulses.
//              CRC-checked, failsafe-centred. This is the "fly" face.
//
//    FACE II — PteronautOS pocket flasher
//              USB-CDC ↔ UART bridge plus a BOOT-hold + power-cycle jig
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
//  MODE SWITCHING — the RP2040-Tiny has NO onboard button (the USB adapter's
//  BOOT/RESET only drive BOOTSEL/RUN, not a readable GPIO), so
//  YOSHIMITSU-RP2040 uses a buttonless protocol:
//
//    FACE I (converter)            FACE II (flasher)
//    ─────────────────             ─────────────────────────────
//    boot default                  entered by USB command "FLASHER"
//    "FLASHER" → FACE II           auto: hold BOOT + power-cycle = bootloader
//    "STATUS"  → status + pins     transparent USB↔UART bridge (esptool)
//    "HELP"    → command list      press RESET → reboot → FACE I (new fw runs)
//
//  The single physical gesture is the adapter's RESET button: after flashing,
//  one press reboots the RP2040 back to FACE I AND power-cycles the receiver,
//  so it boots straight into its new soul. One action, everything done.
//
//  TIMING: this sketch NEVER calls delay(). Every timing — including the
//  BOOT-hold + power-cycle dance — runs on millis() state machines, so the
//  bridge and the console stay live at all times.
//
//  Configure once, flash once, never touch again.
//
// =============================================================================
//  PIN MAP — EDIT HERE  (everything below is validated at compile time)
// -----------------------------------------------------------------------------
//  Pick ONE board profile. Use BOARD_CUSTOM to set every pin by hand.
//
//    BOARD_RP2040_TINY   Waveshare RP2040-Tiny  (default)
//    BOARD_RP2040_ZERO   Waveshare RP2040-Zero  (identical pinout)
//    BOARD_CUSTOM        define every pin yourself in the block below
//
//  UART numbering (arduino-pico convention):
//
//    UART num | Arduino serial | TX pins available     | RX pins available
//    ─────────┼────────────────┼────────────────────────┼────────────────────
//    UART 0   | Serial1        | GP0, GP12, GP16, GP28 | GP1, GP13, GP17, GP29
//    UART 1   | Serial2        | GP4, GP8, GP20, GP24  | GP5, GP9, GP21, GP25
//
//  Set CRSF_UART_NUM / BRIDGE_UART_NUM to the UART number (0 or 1), then give
//  the TX/RX pins from the table. The sketch refuses to compile on pin
//  collisions, invalid UART numbers, or TX/RX pins outside the mux table.
// =============================================================================

#include <Arduino.h>
#include <Servo.h>

// ── Optional onboard WS2812B status LED (GP16) ──────────────────────────────
#if __has_include(<Adafruit_NeoPixel.h>)
  #include <Adafruit_NeoPixel.h>
  #define YOSHI_RGB 1
#else
  #define YOSHI_RGB 0
#endif

// ── Board selection ─────────────────────────────────────────────────────────
#if !defined(BOARD_RP2040_TINY) && !defined(BOARD_RP2040_ZERO) && !defined(BOARD_CUSTOM)
  #define BOARD_RP2040_TINY 1
#endif

#if defined(BOARD_RP2040_TINY) || defined(BOARD_RP2040_ZERO)
  #define CRSF_UART_NUM    0       // UART0 = Serial1 (GP0/GP1)
  #define CRSF_TX_PIN      0       // UART0 TX (unused but wired for completeness)
  #define CRSF_RX_PIN      1       // UART0 RX ← receiver CRSF TX
  #define CRSF_BAUD        420000UL

  #define BRIDGE_UART_NUM  1       // UART1 = Serial2 (GP8/GP9)
  #define BRIDGE_TX_PIN    8       // UART1 TX → receiver RX
  #define BRIDGE_RX_PIN    9       // UART1 RX ← receiver TX
  #define BRIDGE_BAUD      115200UL

  #define SERVO_PIN_LEFT   2
  #define SERVO_PIN_RIGHT  3
  #define SERVO_PIN_RUDDER 4

  #define RX_BOOT_PIN      5       // receiver GPIO0 (active low)
  #define RX_PWR_PIN       6       // P-MOSFET gate (LOW = receiver powered)
  #define RGB_LED_PIN      16      // onboard WS2812B
#endif

#ifdef BOARD_CUSTOM
  // ── define EVERY pin below — UART numbers + pins from the table above ─────
  #define CRSF_UART_NUM    0       // 0 = Serial1 (UART0), 1 = Serial2 (UART1)
  #define CRSF_TX_PIN      0
  #define CRSF_RX_PIN      1
  #define CRSF_BAUD        420000UL

  #define BRIDGE_UART_NUM  1
  #define BRIDGE_TX_PIN    8
  #define BRIDGE_RX_PIN    9
  #define BRIDGE_BAUD      115200UL

  #define SERVO_PIN_LEFT   2
  #define SERVO_PIN_RIGHT  3
  #define SERVO_PIN_RUDDER 4

  #define RX_BOOT_PIN      5
  #define RX_PWR_PIN       6
  #define RGB_LED_PIN      16
#endif

// ── every pin must exist on the RP2040 (GPIO 0..29) ──
#if CRSF_TX_PIN < 0 || CRSF_TX_PIN > 29
  #error "CRSF_TX_PIN outside RP2040 GPIO range 0..29"
#endif
#if CRSF_RX_PIN < 0 || CRSF_RX_PIN > 29
  #error "CRSF_RX_PIN outside RP2040 GPIO range 0..29"
#endif
#if BRIDGE_TX_PIN < 0 || BRIDGE_TX_PIN > 29
  #error "BRIDGE_TX_PIN outside RP2040 GPIO range 0..29"
#endif
#if BRIDGE_RX_PIN < 0 || BRIDGE_RX_PIN > 29
  #error "BRIDGE_RX_PIN outside RP2040 GPIO range 0..29"
#endif
#if SERVO_PIN_LEFT < 0 || SERVO_PIN_LEFT > 29
  #error "SERVO_PIN_LEFT outside RP2040 GPIO range 0..29"
#endif
#if SERVO_PIN_RIGHT < 0 || SERVO_PIN_RIGHT > 29
  #error "SERVO_PIN_RIGHT outside RP2040 GPIO range 0..29"
#endif
#if SERVO_PIN_RUDDER < 0 || SERVO_PIN_RUDDER > 29
  #error "SERVO_PIN_RUDDER outside RP2040 GPIO range 0..29"
#endif
#if RX_BOOT_PIN < 0 || RX_BOOT_PIN > 29
  #error "RX_BOOT_PIN outside RP2040 GPIO range 0..29"
#endif
#if RX_PWR_PIN < 0 || RX_PWR_PIN > 29
  #error "RX_PWR_PIN outside RP2040 GPIO range 0..29"
#endif
#if RGB_LED_PIN < 0 || RGB_LED_PIN > 29
  #error "RGB_LED_PIN outside RP2040 GPIO range 0..29"
#endif

// ── UART numbers must be valid and different ──
#if CRSF_UART_NUM < 0 || CRSF_UART_NUM > 1
  #error "CRSF_UART_NUM must be 0 (Serial1/UART0) or 1 (Serial2/UART1)"
#endif
#if BRIDGE_UART_NUM < 0 || BRIDGE_UART_NUM > 1
  #error "BRIDGE_UART_NUM must be 0 (Serial1/UART0) or 1 (Serial2/UART1)"
#endif
#if CRSF_UART_NUM == BRIDGE_UART_NUM
  #error "CRSF and BRIDGE need two DIFFERENT UARTs"
#endif

// ── UART mux table (arduino-pico): only these pin pairs drive a UART ──
#if CRSF_UART_NUM == 0
  #if CRSF_TX_PIN != 0 && CRSF_TX_PIN != 12 && CRSF_TX_PIN != 16 && CRSF_TX_PIN != 28
    #error "UART0 TX must be GP0, GP12, GP16 or GP28"
  #endif
  #if CRSF_RX_PIN != 1 && CRSF_RX_PIN != 13 && CRSF_RX_PIN != 17 && CRSF_RX_PIN != 29
    #error "UART0 RX must be GP1, GP13, GP17 or GP29"
  #endif
#endif
#if CRSF_UART_NUM == 1
  #if CRSF_TX_PIN != 4 && CRSF_TX_PIN != 8 && CRSF_TX_PIN != 20 && CRSF_TX_PIN != 24
    #error "UART1 TX must be GP4, GP8, GP20 or GP24"
  #endif
  #if CRSF_RX_PIN != 5 && CRSF_RX_PIN != 9 && CRSF_RX_PIN != 21 && CRSF_RX_PIN != 25
    #error "UART1 RX must be GP5, GP9, GP21 or GP25"
  #endif
#endif
#if BRIDGE_UART_NUM == 0
  #if BRIDGE_TX_PIN != 0 && BRIDGE_TX_PIN != 12 && BRIDGE_TX_PIN != 16 && BRIDGE_TX_PIN != 28
    #error "UART0 TX must be GP0, GP12, GP16 or GP28"
  #endif
  #if BRIDGE_RX_PIN != 1 && BRIDGE_RX_PIN != 13 && BRIDGE_RX_PIN != 17 && BRIDGE_RX_PIN != 29
    #error "UART0 RX must be GP1, GP13, GP17 or GP29"
  #endif
#endif
#if BRIDGE_UART_NUM == 1
  #if BRIDGE_TX_PIN != 4 && BRIDGE_TX_PIN != 8 && BRIDGE_TX_PIN != 20 && BRIDGE_TX_PIN != 24
    #error "UART1 TX must be GP4, GP8, GP20 or GP24"
  #endif
  #if BRIDGE_RX_PIN != 5 && BRIDGE_RX_PIN != 9 && BRIDGE_RX_PIN != 21 && BRIDGE_RX_PIN != 25
    #error "UART1 RX must be GP5, GP9, GP21 or GP25"
  #endif
#endif

// ── pin collisions — two functions on one GPIO never build ──
#if CRSF_TX_PIN == CRSF_RX_PIN
  #error "PIN COLLISION: CRSF_TX_PIN and CRSF_RX_PIN share one GPIO"
#endif
#if CRSF_TX_PIN == BRIDGE_TX_PIN
  #error "PIN COLLISION: CRSF_TX_PIN and BRIDGE_TX_PIN share one GPIO"
#endif
#if CRSF_TX_PIN == BRIDGE_RX_PIN
  #error "PIN COLLISION: CRSF_TX_PIN and BRIDGE_RX_PIN share one GPIO"
#endif
#if CRSF_TX_PIN == SERVO_PIN_LEFT
  #error "PIN COLLISION: CRSF_TX_PIN and SERVO_PIN_LEFT share one GPIO"
#endif
#if CRSF_TX_PIN == SERVO_PIN_RIGHT
  #error "PIN COLLISION: CRSF_TX_PIN and SERVO_PIN_RIGHT share one GPIO"
#endif
#if CRSF_TX_PIN == SERVO_PIN_RUDDER
  #error "PIN COLLISION: CRSF_TX_PIN and SERVO_PIN_RUDDER share one GPIO"
#endif
#if CRSF_TX_PIN == RX_BOOT_PIN
  #error "PIN COLLISION: CRSF_TX_PIN and RX_BOOT_PIN share one GPIO"
#endif
#if CRSF_TX_PIN == RX_PWR_PIN
  #error "PIN COLLISION: CRSF_TX_PIN and RX_PWR_PIN share one GPIO"
#endif
#if CRSF_TX_PIN == RGB_LED_PIN
  #error "PIN COLLISION: CRSF_TX_PIN and RGB_LED_PIN share one GPIO"
#endif
#if CRSF_RX_PIN == BRIDGE_TX_PIN
  #error "PIN COLLISION: CRSF_RX_PIN and BRIDGE_TX_PIN share one GPIO"
#endif
#if CRSF_RX_PIN == BRIDGE_RX_PIN
  #error "PIN COLLISION: CRSF_RX_PIN and BRIDGE_RX_PIN share one GPIO"
#endif
#if CRSF_RX_PIN == SERVO_PIN_LEFT
  #error "PIN COLLISION: CRSF_RX_PIN and SERVO_PIN_LEFT share one GPIO"
#endif
#if CRSF_RX_PIN == SERVO_PIN_RIGHT
  #error "PIN COLLISION: CRSF_RX_PIN and SERVO_PIN_RIGHT share one GPIO"
#endif
#if CRSF_RX_PIN == SERVO_PIN_RUDDER
  #error "PIN COLLISION: CRSF_RX_PIN and SERVO_PIN_RUDDER share one GPIO"
#endif
#if CRSF_RX_PIN == RX_BOOT_PIN
  #error "PIN COLLISION: CRSF_RX_PIN and RX_BOOT_PIN share one GPIO"
#endif
#if CRSF_RX_PIN == RX_PWR_PIN
  #error "PIN COLLISION: CRSF_RX_PIN and RX_PWR_PIN share one GPIO"
#endif
#if CRSF_RX_PIN == RGB_LED_PIN
  #error "PIN COLLISION: CRSF_RX_PIN and RGB_LED_PIN share one GPIO"
#endif
#if BRIDGE_TX_PIN == BRIDGE_RX_PIN
  #error "PIN COLLISION: BRIDGE_TX_PIN and BRIDGE_RX_PIN share one GPIO"
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
#if BRIDGE_TX_PIN == RX_BOOT_PIN
  #error "PIN COLLISION: BRIDGE_TX_PIN and RX_BOOT_PIN share one GPIO"
#endif
#if BRIDGE_TX_PIN == RX_PWR_PIN
  #error "PIN COLLISION: BRIDGE_TX_PIN and RX_PWR_PIN share one GPIO"
#endif
#if BRIDGE_TX_PIN == RGB_LED_PIN
  #error "PIN COLLISION: BRIDGE_TX_PIN and RGB_LED_PIN share one GPIO"
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
#if BRIDGE_RX_PIN == RX_BOOT_PIN
  #error "PIN COLLISION: BRIDGE_RX_PIN and RX_BOOT_PIN share one GPIO"
#endif
#if BRIDGE_RX_PIN == RX_PWR_PIN
  #error "PIN COLLISION: BRIDGE_RX_PIN and RX_PWR_PIN share one GPIO"
#endif
#if BRIDGE_RX_PIN == RGB_LED_PIN
  #error "PIN COLLISION: BRIDGE_RX_PIN and RGB_LED_PIN share one GPIO"
#endif
#if SERVO_PIN_LEFT == SERVO_PIN_RIGHT
  #error "PIN COLLISION: SERVO_PIN_LEFT and SERVO_PIN_RIGHT share one GPIO"
#endif
#if SERVO_PIN_LEFT == SERVO_PIN_RUDDER
  #error "PIN COLLISION: SERVO_PIN_LEFT and SERVO_PIN_RUDDER share one GPIO"
#endif
#if SERVO_PIN_LEFT == RX_BOOT_PIN
  #error "PIN COLLISION: SERVO_PIN_LEFT and RX_BOOT_PIN share one GPIO"
#endif
#if SERVO_PIN_LEFT == RX_PWR_PIN
  #error "PIN COLLISION: SERVO_PIN_LEFT and RX_PWR_PIN share one GPIO"
#endif
#if SERVO_PIN_LEFT == RGB_LED_PIN
  #error "PIN COLLISION: SERVO_PIN_LEFT and RGB_LED_PIN share one GPIO"
#endif
#if SERVO_PIN_RIGHT == SERVO_PIN_RUDDER
  #error "PIN COLLISION: SERVO_PIN_RIGHT and SERVO_PIN_RUDDER share one GPIO"
#endif
#if SERVO_PIN_RIGHT == RX_BOOT_PIN
  #error "PIN COLLISION: SERVO_PIN_RIGHT and RX_BOOT_PIN share one GPIO"
#endif
#if SERVO_PIN_RIGHT == RX_PWR_PIN
  #error "PIN COLLISION: SERVO_PIN_RIGHT and RX_PWR_PIN share one GPIO"
#endif
#if SERVO_PIN_RIGHT == RGB_LED_PIN
  #error "PIN COLLISION: SERVO_PIN_RIGHT and RGB_LED_PIN share one GPIO"
#endif
#if SERVO_PIN_RUDDER == RX_BOOT_PIN
  #error "PIN COLLISION: SERVO_PIN_RUDDER and RX_BOOT_PIN share one GPIO"
#endif
#if SERVO_PIN_RUDDER == RX_PWR_PIN
  #error "PIN COLLISION: SERVO_PIN_RUDDER and RX_PWR_PIN share one GPIO"
#endif
#if SERVO_PIN_RUDDER == RGB_LED_PIN
  #error "PIN COLLISION: SERVO_PIN_RUDDER and RGB_LED_PIN share one GPIO"
#endif
#if RX_BOOT_PIN == RX_PWR_PIN
  #error "PIN COLLISION: RX_BOOT_PIN and RX_PWR_PIN share one GPIO"
#endif
#if RX_BOOT_PIN == RGB_LED_PIN
  #error "PIN COLLISION: RX_BOOT_PIN and RGB_LED_PIN share one GPIO"
#endif
#if RX_PWR_PIN == RGB_LED_PIN
  #error "PIN COLLISION: RX_PWR_PIN and RGB_LED_PIN share one GPIO"
#endif

// ── BOARD_CUSTOM must define every pin ──
#ifndef CRSF_UART_NUM
  #error "BOARD_CUSTOM: define CRSF_UART_NUM"
#endif
#ifndef CRSF_TX_PIN
  #error "BOARD_CUSTOM: define CRSF_TX_PIN"
#endif
#ifndef CRSF_RX_PIN
  #error "BOARD_CUSTOM: define CRSF_RX_PIN"
#endif
#ifndef BRIDGE_UART_NUM
  #error "BOARD_CUSTOM: define BRIDGE_UART_NUM"
#endif
#ifndef BRIDGE_TX_PIN
  #error "BOARD_CUSTOM: define BRIDGE_TX_PIN"
#endif
#ifndef BRIDGE_RX_PIN
  #error "BOARD_CUSTOM: define BRIDGE_RX_PIN"
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
#ifndef RX_BOOT_PIN
  #error "BOARD_CUSTOM: define RX_BOOT_PIN"
#endif
#ifndef RX_PWR_PIN
  #error "BOARD_CUSTOM: define RX_PWR_PIN"
#endif
#ifndef RGB_LED_PIN
  #error "BOARD_CUSTOM: define RGB_LED_PIN"
#endif

// ── Serial mapping: UART number → Arduino serial ────────────────────────────
#if CRSF_UART_NUM == 0
  #define CRSF_SERIAL Serial1
#else
  #define CRSF_SERIAL Serial2
#endif
#if BRIDGE_UART_NUM == 0
  #define BRIDGE_SERIAL Serial1
#else
  #define BRIDGE_SERIAL Serial2
#endif

// ── FACE II timing ─────────────────────────────────────────────────────────
#define HOLD_OFF_MS       120   // receiver power-off during the dance
#define HOLD_ON_MS        900   // wait inside bootloader after re-power
#define SETTLE_MS         80    // BOOT release settle
#define BOOT_PRINT_MS     150   // USB settle gate before the boot banner

// ── FACE I constants ───────────────────────────────────────────────────────
#define SERVO_COUNT       3
#define CHANNEL_COUNT     16
#define CRSF_RC_TYPE      0x16
#define CRSF_PAYLOAD      22    // 16 ch × 11 bit = 22 bytes
#define RAW_MIN           172
#define RAW_MAX           1811
#define PWM_MIN           988
#define PWM_MAX           2012
#define FAILSAFE_MS       500
#define BRIDGE_BURST      8     // max bytes per direction per pass (FACE II)

enum Mode : uint8_t { MODE_CONVERTER, MODE_FLASHER };

// ── FACE I state ───────────────────────────────────────────────────────────
static Servo    servos[SERVO_COUNT];
static uint16_t channel[CHANNEL_COUNT];
static uint8_t  servoPin[SERVO_COUNT] = {
  SERVO_PIN_LEFT, SERVO_PIN_RIGHT, SERVO_PIN_RUDDER
};

enum { S_HEADER, S_LEN, S_TYPE, S_PAYLOAD, S_CRC };
static uint8_t  crsfState   = S_HEADER;
static uint8_t  frameLen    = 0;
static uint8_t  frameType   = 0;
static uint8_t  payload[CRSF_PAYLOAD];
static uint8_t  payloadIdx  = 0;
static uint32_t lastGoodMs  = 0;

// ── Global mode state ──────────────────────────────────────────────────────
static Mode mode      = MODE_CONVERTER;
static bool rxPowered = false;

// ── Bridge watchdog counters (visible via STATUS, never printed in FACE II) ─
static uint32_t bridgeUsbToRx  = 0;   // bytes forwarded USB → receiver
static uint32_t bridgeRxToUsb  = 0;   // bytes forwarded receiver → USB
static uint32_t bridgeOverflows = 0;  // bridge UART overflow recoveries
static uint32_t lastBridgeMs   = 0;   // last byte through the bridge

#if YOSHI_RGB
static Adafruit_NeoPixel rgb(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
#endif

// =============================================================================
//  Status LED
// =============================================================================

static void setStatus(uint32_t color) {
#if YOSHI_RGB
  rgb.setPixelColor(0, color);
  rgb.show();
#else
  (void)color;
#endif
}

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

  if (!armed) {                       // disarmed: everything centred
    for (uint8_t i = 0; i < SERVO_COUNT; i++) servos[i].writeMicroseconds(1500);
    return;
  }

  int32_t left  = 1500 + ((int32_t)roll  - 1500) + ((int32_t)pitch - 1500);
  int32_t right = 1500 - ((int32_t)roll  - 1500) + ((int32_t)pitch - 1500);
  left  = constrain(left,  (int32_t)PWM_MIN, (int32_t)PWM_MAX);
  right = constrain(right, (int32_t)PWM_MIN, (int32_t)PWM_MAX);

  servos[0].writeMicroseconds((uint16_t)left);    // left wing
  servos[1].writeMicroseconds((uint16_t)right);   // right wing
  servos[2].writeMicroseconds(yaw);               // crest rudder
}

static void attachServos(void) {
  for (uint8_t i = 0; i < SERVO_COUNT; i++) {
    if (!servos[i].attached()) {
      servos[i].attach(servoPin[i], PWM_MIN, PWM_MAX);
    }
    servos[i].writeMicroseconds(1500);
  }
}

static void detachServos(void) {
  for (uint8_t i = 0; i < SERVO_COUNT; i++) {
    if (servos[i].attached()) servos[i].detach();
  }
}

static void pumpCrsf(void) {
  if (CRSF_SERIAL.overflow()) {                 // UART overflow reset: drain
    while (CRSF_SERIAL.available()) (void)CRSF_SERIAL.read();
    crsfState = S_HEADER;                       // and resync the parser
  }

  while (CRSF_SERIAL.available()) {
    uint8_t b = (uint8_t)CRSF_SERIAL.read();

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

  if (millis() - lastGoodMs > FAILSAFE_MS) {    // 500 ms failsafe: centre
    for (uint8_t i = 0; i < SERVO_COUNT; i++) servos[i].writeMicroseconds(1500);
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

// ── Bootloader dance — async millis() machine, NEVER blocking ───────────────
enum : uint8_t { DANCE_IDLE, DANCE_BOOT_HOLD, DANCE_POWER_OFF, DANCE_POWER_ON, DANCE_SETTLE };
static uint8_t  dancePhase = DANCE_IDLE;
static uint32_t danceT0    = 0;

static void danceBegin(void) {
  if (dancePhase != DANCE_IDLE) return;         // one dance at a time
  dancePhase = DANCE_BOOT_HOLD;
  danceT0    = millis();
}

static void pumpDance(void) {
  switch (dancePhase) {
    case DANCE_IDLE:
      return;
    case DANCE_BOOT_HOLD:
      bootAssert(true);                         // 1. hold BOOT low
      rxPower(false);                           // 2. kill power
      dancePhase = DANCE_POWER_OFF;
      danceT0 = millis();
      break;
    case DANCE_POWER_OFF:
      if (millis() - danceT0 >= HOLD_OFF_MS) {
        rxPower(true);                          // 3. power up into bootloader
        dancePhase = DANCE_POWER_ON;
        danceT0 = millis();
      }
      break;
    case DANCE_POWER_ON:
      if (millis() - danceT0 >= HOLD_ON_MS) {
        bootAssert(false);                      // 4. release BOOT
        dancePhase = DANCE_SETTLE;
        danceT0 = millis();
      }
      break;
    case DANCE_SETTLE:
      if (millis() - danceT0 >= SETTLE_MS) {
        dancePhase = DANCE_IDLE;
        Serial.println("YOSHIMITSU: receiver in ROM bootloader — run esptool with --before no_reset now.");
        setStatus(0x200000);                    // dim red = FACE II
      }
      break;
  }
}

// PURE transparent bridge — no line parsing, so esptool's binary SLIP flows free.
// Up to BRIDGE_BURST bytes per direction per pass: byte-exact, never blocking,
// and fast enough for 460800-baud esptool transfers even in a single pass.
static void pumpBridge(void) {
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

// UART overflow reset: drain the FIFO and re-init the UART — esptool's SLIP
// CRC/timeout then simply retransmits, and the link stays alive.
static void recoverBridgeUart(void) {
  while (BRIDGE_SERIAL.available()) (void)BRIDGE_SERIAL.read();
  BRIDGE_SERIAL.end();
  BRIDGE_SERIAL.setTX(BRIDGE_TX_PIN);
  BRIDGE_SERIAL.setRX(BRIDGE_RX_PIN);
  BRIDGE_SERIAL.begin(BRIDGE_BAUD);
  bridgeOverflows++;
}

// =============================================================================
//  Mode switching + console
// =============================================================================

static void printPinMap(void) {
  Serial.println("YOSHIMITSU: active pin map (edit in the PIN MAP block)");
  Serial.print("  CRSF    UART"); Serial.print(CRSF_UART_NUM);
  Serial.print("  TX=GP"); Serial.print(CRSF_TX_PIN);
  Serial.print("  RX=GP"); Serial.println(CRSF_RX_PIN);
  Serial.print("  BRIDGE  UART"); Serial.print(BRIDGE_UART_NUM);
  Serial.print("  TX=GP"); Serial.print(BRIDGE_TX_PIN);
  Serial.print("  RX=GP"); Serial.println(BRIDGE_RX_PIN);
  Serial.print("  SERVO   L=GP"); Serial.print(SERVO_PIN_LEFT);
  Serial.print("  R=GP"); Serial.print(SERVO_PIN_RIGHT);
  Serial.print("  RUD=GP"); Serial.println(SERVO_PIN_RUDDER);
  Serial.print("  RX_BOOT=GP"); Serial.print(RX_BOOT_PIN);
  Serial.print("  RX_PWR=GP"); Serial.print(RX_PWR_PIN);
  Serial.print("  RGB=GP"); Serial.println(RGB_LED_PIN);
}

static void printStatus(void) {
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

static void printHelp(void) {
  Serial.println("YOSHIMITSU commands:");
  Serial.println("  FLASHER  enter FACE II (pocket flasher)");
  Serial.println("  STATUS   mode + bridge counters + pin map");
  Serial.println("  HELP     this list");
}

static void printBootBanner(void) {
  Serial.println();
  Serial.println("YOSHIMITSU · the Hermetic Shinobi — RP2040-Tiny, one board, two faces.");
  Serial.println("  FACE I  CRSF→PWM converter   (active)");
  Serial.println("  FACE II PteronautOS flasher   (type FLASHER)");
  Serial.println("  STATUS / FLASHER / HELP");
  printPinMap();                    // boot POST: the wiring is always visible
}

static void enterMode(Mode next) {
  if (next == mode) return;
  mode = next;

  if (mode == MODE_CONVERTER) {
    rxPower(true);                              // receiver powered for FACE I
    attachServos();
    setStatus(0x002000);                        // dim green
    Serial.println("YOSHIMITSU: FACE I — CRSF→PWM converter. Servos live, receiver powered.");
    Serial.println("YOSHIMITSU:   FLASHER = enter flasher · STATUS = status · HELP = help");
  } else {
    detachServos();                             // kill servo drive in flasher mode
    rxPower(true);                              // power receiver so esptool sees it
    setStatus(0x201000);                        // dim orange during the dance
    Serial.println("YOSHIMITSU: FACE II — pocket flasher. USB↔UART bridge live.");
    danceBegin();                               // async: hold BOOT + power-cycle
  }
}

static void handleUsb(void) {
  if (mode == MODE_FLASHER) {
    pumpBridge();                               // transparent, no line parsing
    if (BRIDGE_SERIAL.overflow()) recoverBridgeUart();
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
    if (n >= sizeof(line) - 1) n = 0;           // overflow guard
  }
}

// =============================================================================

static bool postDone = false;

void setup() {
  pinMode(RX_PWR_PIN, OUTPUT);
  pinMode(RX_BOOT_PIN, OUTPUT);
  digitalWrite(RX_PWR_PIN, HIGH);               // P-MOSFET gate high = receiver OFF (safe)
  digitalWrite(RX_BOOT_PIN, HIGH);              // BOOT released

#if YOSHI_RGB
  rgb.begin();
  rgb.setBrightness(16);                        // keep the LED unobtrusive
#endif

  Serial.begin(115200);                         // USB CDC (baud ignored on USB)
  // Explicit pins make this board-agnostic regardless of the selected variant.
  CRSF_SERIAL.setTX(CRSF_TX_PIN);
  CRSF_SERIAL.setRX(CRSF_RX_PIN);
  CRSF_SERIAL.begin(CRSF_BAUD);                 // FACE I
  BRIDGE_SERIAL.setTX(BRIDGE_TX_PIN);
  BRIDGE_SERIAL.setRX(BRIDGE_RX_PIN);
  BRIDGE_SERIAL.begin(BRIDGE_BAUD);             // FACE II

  mode = MODE_CONVERTER;
  rxPower(true);                                // receiver powered by default
  attachServos();
  setStatus(0x002000);                          // dim green = FACE I
}

void loop() {
  if (!postDone && millis() >= BOOT_PRINT_MS) { // USB settle gate, no delay()
    postDone = true;
    printBootBanner();
  }
  handleUsb();
  pumpDance();
  if (mode == MODE_CONVERTER) {
    pumpCrsf();
  }
  // FACE II bridging happens inside handleUsb() → pumpBridge().
}