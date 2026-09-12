// Functional test harness for the MUSHIN v1 muscle wave core.
// Drives the real Yoshimitsu.h code with a controllable clock + recording
// servos/serial, and asserts the fixed-point math, protocol round-trip,
// KINCHO laws, damped failsafe, and version handshake.
#include <stdio.h>
#include <math.h>
#include <stdint.h>

#include "Yoshimitsu.h"

// ── controllable clock ──
volatile uint32_t g_ms = 0;
volatile uint32_t g_us = 0;

// ── instance state (stub) ──
uint8_t  SerialType::tx[3][1024];
int16_t  SerialType::txlen[3] = {0, 0, 0};
int16_t  TwoWire::rate = 0;
uint8_t  TwoWire::whoami = 0x68;
SerialType Serial, Serial1, Serial2;
TwoWire Wire;

unsigned long millis(void) { return g_ms; }
unsigned long micros(void) { return g_us; }
void delay(unsigned long) {}
void pinMode(int, int) {}
int  digitalRead(int) { return HIGH; }
void digitalWrite(int, int) {}

#define PI 3.14159265358979323846

static int g_checks = 0;
static int g_fails  = 0;

#define CHECK(cond) do { \
  g_checks++; \
  if (!(cond)) { g_fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(a, b) do { \
  g_checks++; \
  long long _a = (long long)(a), _b = (long long)(b); \
  if (_a != _b) { g_fails++; printf("FAIL %s:%d  %s == %s  (got %lld, want %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } \
} while (0)

#define CHECK_NEAR(a, b, tol) do { \
  g_checks++; \
  double _a = (double)(a), _b = (double)(b); \
  if (fabs(_a - _b) > (tol)) { g_fails++; printf("FAIL %s:%d  |%s - %s| <= %s  (got %.3f, want %.3f)\n", __FILE__, __LINE__, #a, #b, #tol, _a, _b); } \
} while (0)

static void reset_all() {
  msState = MS_IDLE; msLen = 0; msType = 0; msIdx = 0; msXor = 0;
  mushinLinked = false; mushinV1 = 0; mushinParamDirty = 0; mwEasing = 0;
  mushinLastIntentMs = 0;
  mwLastUs = 0; mwClampUs = 0; mwCadence = 0; mwPhaseAcc = 0; mwIterm = 0;
  mwWingL = 1500; mwWingR = 1500; mwUnlinkMs = 0;
  for (int i = 0; i < SERVO_COUNT_MAX; i++) { mushinIntent[i] = 1500; servos[i].last_us = 1500; servos[i].write_count = 0; }
  servoCount = 3;
  stance = STANCE_KINCHO;
#if YOSHI_GYRO
  gyroConnected = false;
#endif
  TwoWire::rate = 0;
}

// Emit a v1 parameter intent frame exactly as the spirit's emitIntentV1 does,
// then push it byte-by-byte through the muscle's parser.
static void feed_v1(uint16_t throttle, uint8_t flapFreq, uint8_t ferocity, int8_t skew,
                    uint8_t slew, uint8_t stanceB, int16_t roll, int16_t pitch) {
  uint8_t b[11];
  b[0] = throttle & 0xFF;       b[1] = (throttle >> 8) & 0xFF;
  b[2] = flapFreq;              b[3] = ferocity;
  b[4] = (uint8_t)skew;         b[5] = slew;   b[6] = stanceB;
  b[7] = (uint16_t)roll & 0xFF; b[8] = ((uint16_t)roll >> 8) & 0xFF;
  b[9] = (uint16_t)pitch & 0xFF; b[10] = ((uint16_t)pitch >> 8) & 0xFF;
  uint8_t x = 11 ^ 0x01;
  mushinParse(0x9B); mushinParse(11); mushinParse(0x01);
  for (int i = 0; i < 11; i++) { mushinParse(b[i]); x ^= b[i]; }
  mushinParse(x);
}

static void feed_v0(const uint16_t* us, int n) {
  uint8_t len = (uint8_t)(n * 2);
  uint8_t x = len ^ 0x01;
  mushinParse(0x9B); mushinParse(len); mushinParse(0x01);
  for (int i = 0; i < n; i++) {
    uint8_t lo = us[i] & 0xFF, hi = (us[i] >> 8) & 0xFF;
    mushinParse(lo); x ^= lo;
    mushinParse(hi); x ^= hi;
  }
  mushinParse(x);
}

static void test_cos_lut_and_interp() {
  printf("\n[1] cos LUT + 8-bit interpolation\n");
  // LUT anchors (full circle, Q14)
  CHECK_EQ(mushinCosLut[0], 16384);
  CHECK_EQ(mushinCosLut[64], 0);
  CHECK_EQ(mushinCosLut[128], -16384);
  CHECK_EQ(mushinCosLut[192], 0);
  // full-circle accuracy of the interpolator vs double-precision cos
  int maxerr = 0;
  for (uint32_t p = 0; p < 65536; p += 7) {
    int ref = (int)lround(cos(2.0 * PI * (double)p / 65536.0) * 16384.0);
    int got = (int)mushinCosQ14(p);
    int e = got > ref ? got - ref : ref - got;
    if (e > maxerr) maxerr = e;
  }
  printf("  max |interp error| over 9363 samples = %d Q14 (2-bit would be ~98)\n", maxerr);
  CHECK(maxerr <= 2);
}

static void test_phase_cadence() {
  printf("\n[2] phase accumulator + unity-gain cadence filter\n");
  reset_all();
  mushinParam.flapFreq = 50;   // 5.0 Hz
  mushinParamDirty = 1;
  g_us = 1000000;
  mushinWaveTick(g_us);        // seed
  const int64_t target = (int64_t)50 * 41177;   // 2058850 Q16 rad/s
  for (int i = 0; i < 1000; i++) {
    g_us += 1000;
    mushinWaveTick(g_us);
    CHECK(mwCadence <= target);          // unity-gain: never overshoots
  }
  CHECK(mwCadence > (target * 999) / 1000);   // settled >= 99.9%
  uint32_t ph = (uint32_t)(mwPhaseAcc / 1000000);
  CHECK(ph < MUSHIN_TWO_PI_Q16);         // phase stays wrapped in [0, 2π)
  printf("  cadence settled to %lld / %lld, phase %u\n", (long long)mwCadence, (long long)target, ph);
}

static void test_shapewave() {
  printf("\n[3] shapeWave (plateau + cos, skew mirror, pinned endpoints)\n");
  reset_all();
  // ferocity 0 → pure cosine (no plateau)
  mushinParam.ferocity = 0; mushinParam.skew = 0;
  CHECK_EQ(mushinShapeWave(0), 16384);
  CHECK_NEAR(mushinShapeWave(MUSHIN_TWO_PI_Q16 / 4), 0, 3);
  CHECK_NEAR(mushinShapeWave(MUSHIN_TWO_PI_Q16 / 2), -16384, 3);
  // half-cycle antisymmetry: shape(p) == -shape(p + π)
  for (uint32_t p = 0; p < MUSHIN_TWO_PI_Q16 / 2; p += 5000) {
    int32_t a = mushinShapeWave(p);
    int32_t b = mushinShapeWave(p + MUSHIN_TWO_PI_Q16 / 2);
    CHECK_NEAR(a, -b, 16);
  }
  // ferocity 100 → broad plateau
  mushinParam.ferocity = 100; mushinParam.skew = 0;
  CHECK_EQ(mushinShapeWave(0), 16384);
  CHECK_EQ(mushinShapeWave(MUSHIN_TWO_PI_Q16 / 8), 16384);   // inside plateau
  CHECK_NEAR(mushinShapeWave(MUSHIN_TWO_PI_Q16 / 2), -16384, 3);
  // skew keeps endpoints pinned
  mushinParam.skew = 90;
  CHECK_EQ(mushinShapeWave(0), 16384);
  CHECK_NEAR(mushinShapeWave(MUSHIN_TWO_PI_Q16 / 2), -16384, 3);
}

static void test_protocol_roundtrip() {
  printf("\n[4] protocol round-trip (v1 + v0 fallback + corrupt xor)\n");
  reset_all();
  feed_v1(750, 40, 63, -25, 30, 1, 120, -80);
  CHECK(mushinLinked == true);
  CHECK_EQ(mushinV1, 1);
  CHECK_EQ(mushinParam.throttle, 750);
  CHECK_EQ(mushinParam.flapFreq, 40);
  CHECK_EQ(mushinParam.ferocity, 63);
  CHECK_EQ((int)mushinParam.skew, -25);
  CHECK_EQ(mushinParam.slew, 30);
  CHECK_EQ(mushinParam.stance, 1);
  CHECK_EQ((int)mushinParam.setRoll, 120);
  CHECK_EQ((int)mushinParam.setPitch, -80);

  reset_all();
  uint16_t us[3] = {1400, 1500, 1600};
  feed_v0(us, 3);
  CHECK(mushinLinked == true);
  CHECK_EQ(mushinV1, 0);
  CHECK_EQ(mushinIntent[0], 1400);
  CHECK_EQ(mushinIntent[1], 1500);
  CHECK_EQ(mushinIntent[2], 1600);

  reset_all();
  mushinParse(0x9B); mushinParse(11); mushinParse(0x01);
  for (int i = 0; i < 11; i++) mushinParse(0xAA);
  mushinParse(0x00);   // wrong xor
  CHECK(mushinLinked == false);
  CHECK_EQ(mushinV1, 0);
}

static void test_kincho_laws() {
  printf("\n[5] KINCHO laws (deadband + velocity clamp)\n");
  // deadband: throttle <= 20 parks the wings at centre
  reset_all();
  feed_v1(10, 50, 50, 0, 0, 0, 0, 0);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinWaveApply(g_us);
  CHECK_EQ(mwWingL, 1500);
  CHECK_EQ(mwWingR, 1500);
  CHECK_EQ(servos[0].last_us, 1500);

  // velocity clamp: slew 30 ms/60° → ~11 µs per 1 ms tick toward the target.
  // mwClampUs is deliberately NOT pre-seeded: the first tick after (re)link
  // must seed it (alongside mwLastUs) so the clamp holds with no kick.
  reset_all();
  feed_v1(1000, 50, 100, 0, 30, 0, 0, 0);
  mushinParamDirty = 1; g_us = 1000000; mwLastUs = 1000000;   // mwClampUs left at 0
  mushinWaveApply(g_us);          // seeds both clocks; wing target 2000, first step tiny
  CHECK_NEAR(mwWingL, 1500, 2);   // no link-establishment kick (was ~2000 before the fix)
  uint16_t prev = mwWingL;
  g_us += 1000;
  mushinWaveApply(g_us);
  int32_t delta = (int32_t)mwWingL - (int32_t)prev;
  printf("  slew=30: one 1ms tick moved wing %d µs (expect ~11)\n", delta);
  CHECK(delta >= 1 && delta <= 12);
}

static void test_damped_failsafe() {
  printf("\n[6] damped failsafe (ease to centre, then release)\n");
  reset_all();
  feed_v1(1000, 50, 100, 0, 30, 0, 0, 0);
  mushinLinked = true; mushinV1 = 1; mushinParamDirty = 0;  // steady flapping, dirty cleared
  mwWingL = 2000; mwWingR = 1000;                            // parked at extremes
  mushinLastIntentMs = 0;                                    // stale
  g_ms = 1000; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinApply();
  CHECK_EQ(mwEasing, 1);
  CHECK(mushinLinked == true);
  CHECK(mwWingL <= 2000 && mwWingL > 1500);                  // eased a step, not torn to centre
  for (int i = 0; i < 20; i++) { g_ms += 10; g_us += 10000; mushinApply(); }  // +200 ms
  CHECK(mushinLinked == true);
  CHECK_NEAR(mwWingL, 1500, 3);                              // reached centre within the window
  CHECK_NEAR(mwWingR, 1500, 3);
  for (int i = 0; i < 10; i++) { g_ms += 10; g_us += 10000; mushinApply(); }  // +100 ms → >250 ms
  CHECK(mushinLinked == false);                              // link released
}

static void test_manji_pid() {
  printf("\n[7] MANJI crest PID (gyro attitude-hold on the correction servo)\n");
  reset_all();
  stance = STANCE_MANJI_DRAGONFLY;
#if YOSHI_GYRO
  gyroConnected = true;
#endif
  TwoWire::rate = 0;                 // gyro reports 0 dps
  feed_v1(1000, 50, 100, 0, 0, 1, 10, 0);   // setRoll = 10 dps, slew 0
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinWaveApply(g_us);
  // errLsb = 10*131 - 0 = 1310; iterm -> 1310
  // corrUs = 1310*4/131 + 1310*4/256 = 40 + 20 = 60 → rudUs = 1560
  CHECK_EQ(servos[GYRO_CORRECTION_SERVO].last_us, 1560);
}

static void test_handshake() {
  printf("\n[8] version handshake (announce p[0]=1, telemetry t[5]=1)\n");
  SerialType::txlen[0] = SerialType::txlen[1] = SerialType::txlen[2] = 0;
  reset_all();
  mushinLinked = true;
  mushinAnnounce();                 // writes ANNOUNCE + TELEMETRY to BRIDGE_SERIAL (Serial2)
  // ANNOUNCE: [0x9B, 6, 0x02, ver, servos, rp2040, manji, gyro, linked, xor]
  const uint8_t* T = SerialType::tx[2];   // Serial2 is the BRIDGE_SERIAL on ESP32S3
  CHECK_EQ(T[0], 0x9B);
  CHECK_EQ(T[1], 6);
  CHECK_EQ(T[2], 0x02);
  CHECK_EQ(T[3], 1);       // p[0] = MUSHIN_VER
  CHECK_EQ(T[4], 3);       // servo count
  uint8_t x = 6 ^ 0x02;
  for (int i = 3; i < 9; i++) x ^= T[i];
  CHECK_EQ(T[9], x);       // xor correct
  // TELEMETRY starts at offset 10: [0x9B, 6, 0x03, r0 r1 c0 c1 linked ver xor]
  CHECK_EQ(T[10], 0x9B);
  CHECK_EQ(T[11], 6);
  CHECK_EQ(T[12], 0x03);
  CHECK_EQ(T[18], 1);      // t[5] = MUSHIN_VER
}

static void test_full_flap() {
  printf("\n[9] full-flap integration (mirrored wings, within PWM window)\n");
  reset_all();
  feed_v1(1000, 50, 50, 20, 0, 0, 0, 0);   // throttle 1000, 5 Hz, ferocity 50, skew 20
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  int minL = 9999, maxL = -9999, minR = 9999, maxR = -9999;
  for (int i = 0; i < 400; i++) {          // 400 ms = 2 full 5 Hz cycles
    g_us += 1000;
    mushinWaveApply(g_us);
    int L = servos[0].last_us, R = servos[1].last_us;
    if (L < minL) minL = L; if (L > maxL) maxL = L;
    if (R < minR) minR = R; if (R > maxR) maxR = R;
    CHECK(L >= PWM_MIN && L <= PWM_MAX);
    CHECK(R >= PWM_MIN && R <= PWM_MAX);
  }
  printf("  L range [%d..%d], R range [%d..%d]\n", minL, maxL, minR, maxR);
  CHECK(maxL - minL > 400);           // meaningful amplitude (throttle 1000 → ±500 µs)
  CHECK(maxR - minR > 400);
  CHECK_NEAR(maxL + minR, 3000, 3);   // mirrored about 1500
  CHECK_NEAR(minL + maxR, 3000, 3);
}

static void test_parser_robustness() {
  printf("\n[10] parser robustness (oversized len, zero-len, sync-in-payload, odd v0)\n");
  // Oversized length (17 > MUSHIN_MAX_PAY) is rejected; the parser recovers.
  reset_all();
  mushinParse(0x9B); mushinParse(17);
  feed_v1(500, 50, 50, 0, 0, 0, 0, 0);
  CHECK(mushinLinked == true);
  CHECK_EQ(mushinParam.throttle, 500);
  CHECK_EQ(mushinV1, 1);

  // Zero-length INTENT frame links but writes nothing (n = 0, no crash).
  reset_all();
  mushinParse(0x9B); mushinParse(0); mushinParse(0x01); mushinParse(0x01);   // xor = 0^1
  CHECK(mushinLinked == true);
  CHECK_EQ(mushinV1, 0);

  // A 0x9B inside the payload is data, not a new sync.
  reset_all();
  feed_v1(0x019B, 50, 50, 0, 0, 0, 0, 0);   // throttle low byte = 0x9B
  CHECK_EQ(mushinParam.throttle, 0x019B);

  // Odd non-11 length with valid xor → v0 path, n = floor(len/2), clamped to
  // servoCount, never overreads the 16-byte buffer.
  reset_all();
  {
    uint8_t len = 13;
    uint8_t x = len ^ 0x01;
    mushinParse(0x9B); mushinParse(len); mushinParse(0x01);
    for (int i = 0; i < 13; i++) { uint8_t b = (uint8_t)(i + 1); mushinParse(b); x ^= b; }
    mushinParse(x);
    CHECK(mushinLinked == true);
    CHECK_EQ(mushinV1, 0);
    CHECK_EQ(mushinIntent[0], (uint16_t)(1 | (2 << 8)));   // bytes 1,2
    CHECK_EQ(mushinIntent[2], (uint16_t)(5 | (6 << 8)));   // bytes 5,6
    CHECK_EQ(mushinIntent[3], 1500);                        // clamped at servoCount=3
  }
}

static void test_boundary_values() {
  printf("\n[11] boundary values (throttle 0/1000/deadband, flapFreq 0/200, slew 0/255, skew ±127)\n");
  // throttle 1000 + ferocity 100 plateau → wing hits 2000 (slew 0 = unlimited)
  reset_all();
  feed_v1(1000, 50, 100, 0, 0, 0, 0, 0);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinWaveApply(g_us);
  CHECK_NEAR(mwWingL, 2000, 2);   // amp 500 at phase 0 (plateau top)

  // throttle 0 → parked at centre (deadband)
  reset_all();
  feed_v1(0, 50, 100, 0, 0, 0, 0, 0);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinWaveApply(g_us);
  CHECK_EQ(mwWingL, 1500);

  // deadband boundary: 20 parks, 21 flaps (tiny amplitude)
  reset_all();
  feed_v1(20, 50, 100, 0, 0, 0, 0, 0);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinWaveApply(g_us);
  CHECK_EQ(mwWingL, 1500);
  reset_all();
  feed_v1(21, 50, 100, 0, 0, 0, 0, 0);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinWaveApply(g_us);
  CHECK(mwWingL > 1500);   // amp 10 → 1510

  // flapFreq 0 → glide: cadence decays toward zero, phase halts
  reset_all();
  feed_v1(500, 50, 50, 0, 0, 0, 0, 0);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  for (int i = 0; i < 200; i++) { g_us += 1000; mushinWaveTick(g_us); }
  CHECK(mwCadence > 0);
  mushinParam.flapFreq = 0;
  for (int i = 0; i < 1000; i++) { g_us += 1000; mushinWaveTick(g_us); }
  CHECK(mwCadence >= 0);
  CHECK(mwCadence < 1000);   // ~e^-10 of ~2.06e6

  // flapFreq 200 → target 8.2e6 fits int32, unity-gain never overshoots
  reset_all();
  feed_v1(500, 200, 50, 0, 0, 0, 0, 0);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  for (int i = 0; i < 500; i++) { g_us += 1000; mushinWaveTick(g_us); }
  CHECK(mwCadence > 0);
  CHECK(mwCadence <= (int32_t)(200 * 41177));

  // slew 0 → unlimited (wing jumps straight to target)
  reset_all();
  feed_v1(1000, 50, 0, 0, 0, 0, 0, 0);   // ferocity 0 → pure cosine, phase 0 = +16384
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinWaveApply(g_us);
  CHECK_NEAR(mwWingL, 2000, 2);

  // slew 255 → slowest clamp (~1 µs per 1 kHz tick)
  reset_all();
  feed_v1(1000, 50, 100, 0, 255, 0, 0, 0);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinWaveApply(g_us);   // seed
  CHECK_NEAR(mwWingL, 1500, 1);
  g_us += 1000; mushinWaveApply(g_us);
  CHECK(mwWingL <= 1502);

  // skew extremes (±127) keep endpoints pinned, no fixed-point overflow
  reset_all();
  mushinParam.ferocity = 0; mushinParam.skew = 127;
  CHECK_EQ(mushinShapeWave(0), 16384);
  CHECK_NEAR(mushinShapeWave(MUSHIN_TWO_PI_Q16 / 2), -16384, 3);
  mushinParam.skew = -128;
  CHECK_EQ(mushinShapeWave(0), 16384);
  CHECK_NEAR(mushinShapeWave(MUSHIN_TWO_PI_Q16 / 2), -16384, 3);
}

static void test_pid_bounds() {
  printf("\n[12] crest PID bounds (setpoint ±250 → rudder ±200 µs, I-term clamp)\n");
  // KINCHO: direct rate→µs mapping, ±250 dps → ±200 µs
  reset_all();
  stance = STANCE_KINCHO;
  feed_v1(1000, 50, 100, 0, 0, 0, 250, 0);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinWaveApply(g_us);
  CHECK_EQ(servos[GYRO_CORRECTION_SERVO].last_us, 1700);
  reset_all();
  stance = STANCE_KINCHO;
  feed_v1(1000, 50, 100, 0, 0, 0, -250, 0);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinWaveApply(g_us);
  CHECK_EQ(servos[GYRO_CORRECTION_SERVO].last_us, 1300);

  // MANJI + gyro: I-term saturates at MUSHIN_PID_I_MAX (4000), never unbounded
  reset_all();
  stance = STANCE_MANJI_DRAGONFLY;
#if YOSHI_GYRO
  gyroConnected = true;
#endif
  TwoWire::rate = 0;
  feed_v1(1000, 50, 100, 0, 0, 1, 10, 0);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  for (int i = 0; i < 20; i++) { g_us += 1000; mushinWaveApply(g_us); }
  CHECK_EQ(mwIterm, MUSHIN_PID_I_MAX);
}

static void test_failsafe_edge() {
  printf("\n[13] failsafe edge cases (v0 immediate unlink, re-link cancels easing)\n");
  // v0 link loss → immediate unlink (the µs path has no local wave to ease)
  reset_all();
  uint16_t us[3] = {1400, 1500, 1600};
  feed_v0(us, 3);
  CHECK_EQ(mushinV1, 0);
  mushinLastIntentMs = 0;
  g_ms = 1000;
  mushinApply();
  CHECK(mushinLinked == false);

  // re-link during a v1 ease cancels it and resumes flapping
  reset_all();
  feed_v1(1000, 50, 100, 0, 30, 0, 0, 0);
  mushinLinked = true; mushinV1 = 1; mushinParamDirty = 0;
  mwWingL = 2000; mwWingR = 1000;
  mushinLastIntentMs = 0;
  g_ms = 1000; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  mushinApply();
  CHECK_EQ(mwEasing, 1);
  feed_v1(1000, 50, 100, 0, 30, 0, 0, 0);   // fresh frame cancels the ease
  CHECK_EQ(mwEasing, 0);
  CHECK(mushinLinked == true);
  mushinApply();   // now = 1000, lastIntentMs = 1000 — fresh, flaps
  CHECK_EQ(mwEasing, 0);

  // full unlink → re-link with v1: the v1 latch drops on unlink, so the
  // re-link frame re-seeds both clocks — no velocity-clamp kick (regression:
  // without the latch reset the first post-relink tick could move the wing
  // up to 33300/slew µs on the stale clamp clock).
  reset_all();
  feed_v1(1000, 50, 100, 0, 30, 0, 0, 0);
  mushinLinked = true; mushinV1 = 1; mushinParamDirty = 0;
  mwWingL = 2000; mwWingR = 1000;
  mushinLastIntentMs = 0;
  g_ms = 1000; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  for (int i = 0; i < 40; i++) { g_ms += 10; g_us += 10000; mushinApply(); }  // ease + release
  CHECK(mushinLinked == false);
  CHECK_EQ(mushinV1, 0);                     // unlink drops the v1 latch
  g_ms += 100;                               // the link was down a while
  feed_v1(1000, 50, 100, 0, 30, 0, 0, 0);    // re-link, first v1 frame
  CHECK_EQ(mushinV1, 1);
  CHECK_EQ(mushinParamDirty, 1);             // re-link requests a wave-core seed
  g_us += 500000;                            // gap >> 1 kHz gate
  mushinApply();                             // seeds both clocks; wing target 2000
  CHECK_NEAR(mwWingL, 1500, 2);              // no re-link clamp kick (was up to 1110 µs)
  uint16_t prev = mwWingL;
  g_us += 1000;
  mushinApply();
  int32_t delta = (int32_t)mwWingL - (int32_t)prev;
  CHECK(delta >= 1 && delta <= 12);          // gentle first step after re-link (~11 µs)

  // 1 kHz rate gate: back-to-back applies at the same µs are no-ops, the
  // wave core runs again only after a full millisecond.
  reset_all();
  feed_v1(1000, 50, 100, 0, 0, 0, 0, 0);
  mushinParamDirty = 1; g_us = 2000000; mwClampUs = 2000000; mwLastUs = 2000000;
  mushinApply();
  CHECK(servos[0].write_count > 0);
  uint16_t wc = servos[0].write_count;
  mushinApply();                             // same µs → gated
  CHECK_EQ(servos[0].write_count, wc);
  g_us += 999;                               // 999 µs later → still gated
  mushinApply();
  CHECK_EQ(servos[0].write_count, wc);
  g_us += 1;                                 // exactly 1 ms → runs again
  mushinApply();
  CHECK(servos[0].write_count > wc);
}

static void test_forged_params() {
  printf("\n[14] forged v1 params (throttle 65535, setpoints +-32767) clamped at parse\n");
  reset_all();

  // slew 0, forged throttle: the spec envelope [1000,2000] must still hold,
  // the full +-500 us amplitude must be reached (clamped to 1000), and the
  // easing anchors must never leave the envelope (a forged throttle would
  // overshoot them into uint16 wrap and pin a wing at the rail).
  feed_v1(65535, 50, 100, 0, 0, STANCE_KINCHO, 32767, -32767);
  CHECK_EQ(mushinParam.throttle, 1000);
  CHECK_EQ(mushinParam.setRoll, 250);
  CHECK_EQ(mushinParam.setPitch, -250);
  mushinParamDirty = 1; g_us = 1000000; mwClampUs = 1000000; mwLastUs = 1000000;
  int Lmin = 9999, Lmax = 0, Rmin = 9999, Rmax = 0;
  for (int i = 0; i < 100; i++) {
    g_us += 1000;
    mushinWaveApply(g_us);
    int L = servos[0].last_us, R = servos[1].last_us;
    if (L < Lmin) Lmin = L; if (L > Lmax) Lmax = L;
    if (R < Rmin) Rmin = R; if (R > Rmax) Rmax = R;
  }
  CHECK(Lmin >= 1000 && Lmax <= 2000);
  CHECK(Rmin >= 1000 && Rmax <= 2000);
  CHECK(Lmax >= 1900 && Rmin <= 1100);
  CHECK(mwWingL >= 1000 && mwWingL <= 2000);
  CHECK(mwWingR >= 1000 && mwWingR <= 2000);
}

int main() {
  Serial.id = 0; Serial1.id = 1; Serial2.id = 2;
  printf("=== MUSHIN v1 muscle wave-core functional test ===\n");
  test_cos_lut_and_interp();
  test_phase_cadence();
  test_shapewave();
  test_protocol_roundtrip();
  test_kincho_laws();
  test_damped_failsafe();
  test_manji_pid();
  test_handshake();
  test_full_flap();
  test_parser_robustness();
  test_boundary_values();
  test_pid_bounds();
  test_failsafe_edge();
  test_forged_params();
  printf("\n%d checks, %d failures\n", g_checks, g_fails);
  return g_fails ? 1 : 0;
}