// =============================================================================
//  HERMES · the Hermetic Shinobi — RP2040-Tiny build
//  One board, two faces. The featherweight incarnation.
// -----------------------------------------------------------------------------
//  A single Waveshare RP2040-Tiny (or RP2040-Zero — same pinout) that transmutes
//  between two workbench personalities:
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
//  Hermes is the Greek messenger and psychopomp — swift, stealthy, the guide
//  who carries souls between worlds; Trismegistus makes him thrice-great.
//  The two faces are the two serpents of his caduceus.
//
//  MODE SWITCHING — the RP2040-Tiny has NO onboard button (the USB adapter's
//  BOOT/RESET only drive BOOTSEL/RUN, not a readable GPIO), so HERMES-RP2040
//  uses a buttonless protocol:
//
//    FACE I (converter)            FACE II (flasher)
//    ─────────────────             ─────────────────────────────
//    boot default                  entered by USB command "FLASHER"
//    "FLASHER" → FACE II           auto: hold BOOT + power-cycle = bootloader
//    "STATUS"  → status            transparent USB↔UART bridge (esptool)
//                                  press RESET → reboot → FACE I (runs new fw)
//
//  The single physical gesture is the adapter's RESET button: after flashing,
//  one press reboots the RP2040 back to FACE I AND power-cycles the receiver,
//  so it boots straight into its new soul. One action, everything done.
//
// =============================================================================
//  WIRING (one permanent harness — the harness never changes):
//
//    RP2040-Tiny 3V3 ──► S ── P-MOSFET (AO3401) ── D ──► RX 3V3
//    RP2040-Tiny GP6 ──► gate   (10 kΩ pull-up to 3V3; LOW = receiver powered)
//    RP2040-Tiny GP5 ──► RX BOOT pad (GPIO0, active low)
//    RP2040-Tiny GP1 ◄── RX TX        (CRSF, FACE I)
//    RP2040-Tiny GP0 ──► RX RX        (CRSF, unused but wired)
//    RP2040-Tiny GP9 ◄── RX TX        (flash bridge, FACE II)
//    RP2040-Tiny GP8 ──► RX RX        (flash bridge, FACE II)
//    RP2040-Tiny GND ──► RX GND
//
//    Servos (FACE I): left wing → GP2 · right wing → GP3 · crest rudder → GP4
//    Onboard RGB    : GP16 (WS2812B — used for status if Adafruit_NeoPixel
//                     is installed; otherwise status is console-only)
//
//  ⚠️  Feed the receiver 3.3 V only while on the jig. Verify the MOSFET
//     orientation with a voltmeter before connecting a receiver. Servos must
//     be powered from their own rail, never from the RP2040's 3V3 pin.
//
// =============================================================================
//  FLASHING AN EP2-CLASS RECEIVER (FACE II):
//    1. Open the USB-CDC console and type FLASHER — HERMES powers the receiver
//       and automatically drops it into its ROM bootloader.
//    2. Run esptool through the bridge (same port the console used):
//         python3 -m esptool --chip esp8285 --port /dev/cu.usbmodemXXXX \
//           --baud 115200 --before no_reset write_flash \
//           --flash_mode dout --flash_size 1MB --flash_freq 40m \
//           0x0 firmware.bin
//       The bridge is PURE transparent (no line parsing) so esptool's binary
//       SLIP flows untouched. --baud MUST match BRIDGE_BAUD (115200).
//    3. Press RESET on the USB adapter — the RP2040 reboots into FACE I and
//       power-cycles the receiver, which boots into its new firmware.
//
//  NEVER use --before default_reset over this bridge — the power-cycle IS the
//  reset. NEVER use delay() inside the servo-driving face — HERMES is async.
//
// =============================================================================
//  TO FLASH HERMES ONTO THE RP2040-TINY ITSELF (BOOTSEL):
//    Press and hold BOOT, tap RESET once, then release BOOT (or simply hold
//    BOOT while plugging the USB cable). A drive named RPI-RP2 appears — drag
//    the compiled .uf2 onto it. The board reboots into the sketch.
//
//  Requires: arduino-pico core (earlephilhower/arduino-pico), Board =
//  "Waveshare RP2040 Zero" (or any generic RP2040 board — pins are set
//  explicitly). No extra library is required; Adafruit_NeoPixel is optional
//  for the onboard RGB status LED.
// =============================================================================

#include <Arduino.h>
#include <Servo.h>

// ── Optional onboard WS2812B status LED (GP16) ──────────────────────────────
#if __has_include(<Adafruit_NeoPixel.h>)
  #include <Adafruit_NeoPixel.h>
  #define HERMES_RGB 1
#else
  #define HERMES_RGB 0
#endif

// ── Pin map ────────────────────────────────────────────────────────────────
#define CRSF_TX_PIN       0     // UART0 TX (unused but wired for completeness)
#define CRSF_RX_PIN       1     // UART0 RX ← receiver CRSF TX
#define CRSF_BAUD         420000

#define BRIDGE_TX_PIN     8     // UART1 TX → receiver RX
#define BRIDGE_RX_PIN     9     // UART1 RX ← receiver TX
#define BRIDGE_BAUD       115200

#define SERVO_PIN_LEFT    2
#define SERVO_PIN_RIGHT   3
#define SERVO_PIN_RUDDER  4

#define RX_BOOT_PIN       5     // receiver GPIO0 (active low)
#define RX_PWR_PIN        6     // P-MOSFET gate (LOW = receiver powered)
#define RGB_LED_PIN       16    // onboard WS2812B

// ── FACE II timing ─────────────────────────────────────────────────────────
#define HOLD_OFF_MS       120   // receiver power-off during the dance
#define HOLD_ON_MS        900   // wait inside bootloader after re-power

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

#if HERMES_RGB
static Adafruit_NeoPixel rgb(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
#endif

// =============================================================================
//  Status LED
// =============================================================================

static void setStatus(uint32_t color) {
#if HERMES_RGB
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
  left  = constrain(left,  PWM_MIN, PWM_MAX);
  right = constrain(right, PWM_MIN, PWM_MAX);

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

// Hold BOOT low + power-cycle: the receiver wakes up inside its ROM bootloader.
static void enterBootloader(void) {
  setStatus(0x201000);                          // dim orange
  Serial.println("HERMES: dropping receiver into ROM bootloader (BOOT held + power-cycle)...");
  bootAssert(true);                             // 1. hold BOOT low
  rxPower(false);                               // 2. kill power
  delay(HOLD_OFF_MS);
  rxPower(true);                                // 3. power up into bootloader
  delay(HOLD_ON_MS);
  bootAssert(false);                            // 4. release BOOT
  delay(80);
  Serial.println("HERMES: receiver in ROM bootloader — run esptool with --before no_reset now.");
  setStatus(0x200000);                          // dim red = FACE II
}

// PURE transparent bridge — no line parsing, so esptool's binary SLIP flows free.
static void pumpBridge(void) {
  if (Serial2.available() && Serial.availableForWrite()) {
    Serial.write(Serial2.read());
  }
  if (Serial.available() && Serial2.availableForWrite()) {
    Serial2.write(Serial.read());
  }
}

// =============================================================================
//  Mode switching + console
// =============================================================================

static void printStatus(void) {
  Serial.print("HERMES: mode = ");
  Serial.print(mode == MODE_CONVERTER ? "FACE I (converter)" : "FACE II (flasher)");
  Serial.print(" · receiver power = ");
  Serial.println(rxPowered ? "ON" : "OFF");
}

static void enterMode(Mode next) {
  if (next == mode) return;
  mode = next;

  if (mode == MODE_CONVERTER) {
    rxPower(true);                              // receiver powered for FACE I
    attachServos();
    setStatus(0x002000);                        // dim green
    Serial.println("HERMES: FACE I — CRSF→PWM converter. Servos live, receiver powered.");
    Serial.println("HERMES:   FLASHER = enter flasher · STATUS = status");
  } else {
    detachServos();                             // kill servo drive in flasher mode
    rxPower(true);                              // power receiver so esptool sees it
    Serial.println("HERMES: FACE II — pocket flasher. USB↔UART bridge live.");
    enterBootloader();                          // auto: drop EP2 into bootloader
  }
}

static void handleUsb(void) {
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

// =============================================================================

void setup() {
  pinMode(RX_PWR_PIN, OUTPUT);
  pinMode(RX_BOOT_PIN, OUTPUT);
  digitalWrite(RX_PWR_PIN, HIGH);               // P-MOSFET gate high = receiver OFF (safe)
  digitalWrite(RX_BOOT_PIN, HIGH);              // BOOT released

#if HERMES_RGB
  rgb.begin();
  rgb.setBrightness(16);                        // keep the LED unobtrusive
#endif

  delay(50);

  Serial.begin(115200);                         // USB CDC (baud ignored on USB)
  // Explicit pins make this board-agnostic regardless of the selected variant.
  Serial1.setTX(CRSF_TX_PIN);
  Serial1.setRX(CRSF_RX_PIN);
  Serial1.begin(CRSF_BAUD);                     // FACE I
  Serial2.setTX(BRIDGE_TX_PIN);
  Serial2.setRX(BRIDGE_RX_PIN);
  Serial2.begin(BRIDGE_BAUD);                   // FACE II

  mode = MODE_CONVERTER;
  rxPower(true);                                // receiver powered by default
  attachServos();
  setStatus(0x002000);                          // dim green = FACE I

  Serial.println();
  Serial.println("HERMES · the Hermetic Shinobi — RP2040-Tiny, one board, two faces.");
  Serial.println("  FACE I  CRSF→PWM converter   (active)");
  Serial.println("  FACE II PteronautOS flasher   (type FLASHER)");
  Serial.println("  STATUS / FLASHER");
}

void loop() {
  handleUsb();

  if (mode == MODE_CONVERTER) {
    pumpCrsf();
  }
  // FACE II bridging happens inside handleUsb() → pumpBridge().
}
