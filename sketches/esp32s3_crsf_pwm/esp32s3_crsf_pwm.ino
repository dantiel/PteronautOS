// esp32s3_crsf_pwm.ino — ESP32-S3 CRSF → PWM servo converter
// ------------------------------------------------------------
// Reads ExpressLRS CRSF RC frames (420000 baud) on a hardware UART and
// drives up to 8 servos with 988–2012 µs pulses.
//
//  Wiring (crossed):
//    receiver TX  →  GPIO44  (this sketch's UART RX)
//    receiver RX  →  GPIO43  (this sketch's UART TX, unused but wired)
//    receiver 5V  →  shared 5V rail   ·   GND → GND
//    servos       →  GPIO1 (left wing) · GPIO2 (right wing) · GPIO3 (crest rudder)
//
//  Channel map (PteronautOS-style AETR defaults, all overrideable below):
//    CH1 roll  → right wing      CH2 pitch → crest rudder   CH3 throttle (ignored)
//    CH4 yaw   → left wing       CH5 arm   → gates output
//  Failsafe: no valid frame for 500 ms  →  all servos centre to 1500 µs.
//
//  Requires: arduino-esp32 core + the ESP32Servo library (Library Manager).
//  Target board: any ESP32-S3 (Waveshare Tiny/Micro/Nano, generic devkit).

#include <ESP32Servo.h>

// ── Pins ────────────────────────────────────────────────────────────
#ifndef CRSF_UART_RX_PIN
#define CRSF_UART_RX_PIN 44        // S3 UART RX  ← receiver TX
#endif
#ifndef CRSF_UART_TX_PIN
#define CRSF_UART_TX_PIN 43        // S3 UART TX  → receiver RX
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

static Servo servos[SERVO_COUNT_MAX];
static uint16_t channel[CHANNEL_COUNT];
static uint8_t  pinOf[SERVO_COUNT_MAX] = {
  SERVO_PIN_LEFT, SERVO_PIN_RIGHT, SERVO_PIN_RUDDER,
  4, 5, 6, 7, 8                                // extend as needed
};
static uint8_t  servoCount = 3;                 // set higher for 4–8 channels

// ── CRSF frame state machine ───────────────────────────────────────
enum { S_HEADER, S_LEN, S_TYPE, S_PAYLOAD, S_CRC };
static uint8_t  state = S_HEADER;
static uint8_t  frameLen = 0, frameType = 0;
static uint8_t  payload[CRSF_PAYLOAD];
static uint8_t  payloadIdx = 0;
static uint32_t lastGoodMs = 0;

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
  uint16_t roll    = mapRaw(channel[0]);
  uint16_t pitch   = mapRaw(channel[1]);
  uint16_t yaw     = mapRaw(channel[3]);
  uint16_t arm     = channel[4];
  bool     armed   = (arm > 992);

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

void setup() {
  Serial1.begin(CRSF_BAUD, SERIAL_8N1, CRSF_UART_RX_PIN, CRSF_UART_TX_PIN);

  // attach all configured servo channels
  for (uint8_t i = 0; i < servoCount && i < SERVO_COUNT_MAX; i++) {
    servos[i].attach(pinOf[i], PWM_MIN, PWM_MAX);
    servos[i].writeMicroseconds(1500);
  }

  lastGoodMs = millis();
}

void loop() {
  while (Serial1.available()) {
    uint8_t b = (uint8_t)Serial1.read();

    switch (state) {
      case S_HEADER:
        if (b == 0xC8 || b == 0xEE) state = S_LEN;        // device / extended header
        break;
      case S_LEN:
        frameLen = b;
        state = (b >= 2 && b <= 64) ? S_TYPE : S_HEADER;  // sane length
        break;
      case S_TYPE:
        frameType = b;
        payloadIdx = 0;
        state = S_PAYLOAD;
        break;
      case S_PAYLOAD:
        if (payloadIdx < CRSF_PAYLOAD) {
          payload[payloadIdx++] = b;
          if (payloadIdx == CRSF_PAYLOAD) state = S_CRC;
        } else {
          state = S_HEADER;
        }
        break;
      case S_CRC: {
        uint8_t crcBuf[CRSF_PAYLOAD + 2];                 // len + type + payload
        crcBuf[0] = frameLen;
        crcBuf[1] = frameType;
        memcpy(crcBuf + 2, payload, CRSF_PAYLOAD);
        if (frameType == CRSF_RC_TYPE && crsfCrc8(crcBuf, CRSF_PAYLOAD + 2) == b) {
          // decode 16 × 11-bit little-endian channels
          for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            uint8_t  byteIdx = (uint8_t)((i * 11) >> 3);
            uint8_t  shift   = (uint8_t)((i * 11) & 7);
            channel[i] = (uint16_t)((payload[byteIdx] | (payload[byteIdx + 1] << 8)) >> shift) & 0x07FF;
          }
          lastGoodMs = millis();
          applyChannels();
        }
        state = S_HEADER;
        break;
      }
    }
  }

  // failsafe: centre everything when the stream goes silent
  if (millis() - lastGoodMs > FAILSAFE_MS) {
    for (uint8_t i = 0; i < servoCount; i++) servos[i].writeMicroseconds(1500);
    lastGoodMs = millis();
  }
}