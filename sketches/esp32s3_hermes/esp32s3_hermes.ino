// =============================================================================
//  HERMES · the Hermetic Shinobi
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
//  Hermes is the Greek messenger and psychopomp — swift, stealthy, the guide
//  who carries souls between worlds; Trismegistus makes him thrice-great.
//  The two faces are the two serpents of his caduceus: the converter transmutes
//  CRSF frames into muscle, the flasher transmigrates firmware into a sleeping
//  receiver. One board, two faces, one caduceus.
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
//     FLASHER  switch to FACE II    ·    STATUS  show mode + receiver power
//
//  In FACE II the USB↔UART path is a PURE transparent bridge (no line parsing),
//  so esptool's SLIP-framed binary traffic passes through untouched.
//
// =============================================================================
//  WIRING (one permanent harness — the harness never changes):
//
//    S3 GPIO10  ──► gate of P-MOSFET (AO3401)          (LOW = receiver powered)
//    S3 3V3     ──► S  ── MOSFET ── D  ──► RX 3V3      (10 kΩ pull-up to 3V3)
//    S3 GPIO9   ──► RX BOOT pad                         (active low)
//    S3 GPIO44  ◄── RX TX                               (CRSF, FACE I)
//    S3 GPIO43  ──► RX RX                               (CRSF, unused but wired)
//    S3 GPIO18  ◄── RX TX                               (flash bridge, FACE II)
//    S3 GPIO17  ──► RX RX                               (flash bridge, FACE II)
//    S3 GND     ──► RX GND
//
//    Servos (FACE I): left wing → GPIO1 · right wing → GPIO2 · crest rudder → GPIO3
//    Status LED      : GPIO21 (solid = FACE I, off = FACE II; set -1 to disable)
//
//  ⚠️  Feed the receiver 3.3 V only while on the jig. Verify the MOSFET
//     orientation with a voltmeter before connecting a receiver.
//
// =============================================================================
//  FLASHING AN EP2-CLASS RECEIVER (FACE II):
//    1. Double-tap BOOT → HERMES enters FACE II and powers the receiver.
//    2. Drop the receiver into its bootloader (double-tap BOOT in FACE II), then:
//         python3 -m esptool --chip esp8285 --port /dev/cu.usbmodemXXXX \
//           --baud 115200 --before no_reset write_flash \
//           --flash_mode dout --flash_size 1MB --flash_freq 40m \
//           0x0 firmware.bin
//       The double-tap holds BOOT low + power-cycles the receiver; esptool
//       (--before no_reset) talks straight through the transparent bridge.
//    3. Single-tap BOOT → receiver restarts into its new soul.
//
//  NEVER use --before default_reset over this bridge — the power-cycle IS the
//  reset. NEVER use delay() inside the servo-driving face — HERMES is async.
//
//  Requires: arduino-esp32 core + the ESP32Servo library (Library Manager).
// =============================================================================

#include <Arduino.h>
#include <ESP32Servo.h>

// ── FACE I — converter pins ─────────────────────────────────────────────
#ifndef CRSF_UART_RX_PIN
#define CRSF_UART_RX_PIN 44        // S3 UART1 RX ← receiver TX
#endif
#ifndef CRSF_UART_TX_PIN
#define CRSF_UART_TX_PIN 43        // S3 UART1 TX → receiver RX
#endif
#ifndef CRSF_BAUD
#define CRSF_BAUD 420000UL
#endif

#ifndef SERVO_PIN_LEFT
#define SERVO_PIN_LEFT 1
#endif
#ifndef SERVO_PIN_RIGHT
#define SERVO_PIN_RIGHT 2
#endif
#ifndef SERVO_PIN_RUDDER
#define SERVO_PIN_RUDDER 3
#endif

// ── FACE II — flasher pins ───────────────────────────────────────────────
#ifndef BRIDGE_TX_PIN
#define BRIDGE_TX_PIN 17           // S3 UART2 TX → receiver RX
#endif
#ifndef BRIDGE_RX_PIN
#define BRIDGE_RX_PIN 18           // S3 UART2 RX ← receiver TX
#endif
#ifndef BRIDGE_BAUD
#define BRIDGE_BAUD 115200UL
#endif
#ifndef RX_PWR_PIN
#define RX_PWR_PIN 10              // P-MOSFET gate (active-low power switch)
#endif
#ifndef RX_BOOT_PIN
#define RX_BOOT_PIN 9              // receiver BOOT pad (active low)
#endif
#ifndef MODE_LED_PIN
#define MODE_LED_PIN 21            // status LED (set -1 to disable)
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

// ── Timing (FACE II + mode switch) ──────────────────────────────────────
#define PRESS_WINDOW_MS 1500
#define DEBOUNCE_MS     40
#define LONG_PRESS_MS   2000
#define HOLD_OFF_MS     120       // receiver power-off during the dance
#define HOLD_ON_MS      900       // wait inside bootloader after re-power
#define RX_RESTART_MS   700

enum Mode : uint8_t { MODE_CONVERTER, MODE_FLASHER };

// ── FACE I state ────────────────────────────────────────────────────────
static Servo servos[SERVO_COUNT_MAX];
static uint16_t channel[CHANNEL_COUNT];
static uint8_t  pinOf[SERVO_COUNT_MAX] = {
  SERVO_PIN_LEFT, SERVO_PIN_RIGHT, SERVO_PIN_RUDDER,
  4, 5, 6, 7, 8                                // extend as needed
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
  left  = constrain(left,  PWM_MIN, PWM_MAX);
  right = constrain(right, PWM_MIN, PWM_MAX);

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

static void restartRx() {
  rxPower(false);
  delay(HOLD_OFF_MS);
  rxPower(true);
  delay(RX_RESTART_MS);
  Serial.println("HERMES: receiver restarted — new firmware should be running.");
}

static void enterFlashMode() {
  Serial.println("HERMES: FLASH MODE — cycling receiver power with BOOT held...");
  bootAssert(true);                             // 1. hold BOOT low
  rxPower(false);                               // 2. kill power
  delay(HOLD_OFF_MS);
  rxPower(true);                                // 3. power up into bootloader
  delay(HOLD_ON_MS);
  bootAssert(false);                            // 4. release BOOT
  delay(80);
  Serial.println("HERMES: receiver in ROM bootloader — run esptool with --before no_reset now.");
}

// PURE transparent bridge — no line parsing, so esptool's binary SLIP flows free.
static void pumpBridge() {
  if (Serial2.available() && Serial.availableForWrite()) {
    Serial.write(Serial2.read());
  }
  if (Serial.available() && Serial2.availableForWrite()) {
    Serial2.write(Serial.read());
  }
}

// =============================================================================
//  Mode switching + console (FACE I)
// =============================================================================

static void setLed() {
  if (MODE_LED_PIN >= 0) {
    digitalWrite(MODE_LED_PIN, mode == MODE_CONVERTER ? HIGH : LOW);
  }
}

static void printStatus() {
  Serial.print("HERMES: mode = ");
  Serial.print(mode == MODE_CONVERTER ? "FACE I (converter)" : "FACE II (flasher)");
  Serial.print(" · receiver power = ");
  Serial.println(rxPowered ? "ON" : "OFF");
}

static void enterMode(Mode next) {
  if (next == mode) return;
  mode = next;

  if (mode == MODE_CONVERTER) {
    // Receiver powers up; CRSF resumes on UART1. The bridge UART idles.
    rxPower(true);
    attachServos();
    setLed();
    Serial.println("HERMES: FACE I — CRSF→PWM converter. Servos live, receiver powered.");
  } else {
    // Kill servo drive, power the receiver so esptool can see it through the bridge.
    detachServos();
    rxPower(true);
    setLed();
    Serial.println("HERMES: FACE II — pocket flasher. USB↔UART bridge live.");
    Serial.println("HERMES:   tap = restart · double-tap = bootloader · long-press = FACE I");
  }
}

static void handleUsb() {
  if (mode == MODE_FLASHER) {
    pumpBridge();          // transparent, no line parsing
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
      if (mode == MODE_CONVERTER) enterMode(MODE_FLASHER);       // FACE I ×2 → FACE II
      else enterFlashMode();                                     // FACE II ×2 → bootloader
    } else if (tapCount == 1 && (ms - lastTapMs) > PRESS_WINDOW_MS) {
      tapCount = 0;
      if (mode == MODE_FLASHER) restartRx();                     // FACE II single → restart
    }
  }
}

// =============================================================================

void setup() {
  pinMode(RX_PWR_PIN, OUTPUT);
  pinMode(RX_BOOT_PIN, OUTPUT);
  pinMode(0, INPUT_PULLUP);                       // BOOT button
  if (MODE_LED_PIN >= 0) pinMode(MODE_LED_PIN, OUTPUT);

  bootAssert(false);                              // BOOT released
  rxPower(true);                                  // receiver powered by default
  delay(50);

  Serial.begin(115200);                           // USB CDC
  Serial1.begin(CRSF_BAUD, SERIAL_8N1, CRSF_UART_RX_PIN, CRSF_UART_TX_PIN);   // FACE I
  Serial2.begin(BRIDGE_BAUD, SERIAL_8N1, BRIDGE_RX_PIN, BRIDGE_TX_PIN);       // FACE II

  mode = MODE_CONVERTER;
  attachServos();
  setLed();

  Serial.println();
  Serial.println("HERMES · the Hermetic Shinobi — one board, two faces.");
  Serial.println("  FACE I  CRSF→PWM converter   (active)");
  Serial.println("  FACE II PteronautOS flasher   (double-tap BOOT to enter)");
  Serial.println("  STATUS / FLASHER");
}

void loop() {
  handleBootButton();
  handleUsb();

  if (mode == MODE_CONVERTER) {
    pumpCrsf();
  }
  // FACE II bridging happens inside handleUsb() → pumpBridge().
}