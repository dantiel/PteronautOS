// Functional test for the SPIRIT half of MUSHIN v1: MushinNoShin framing +
// parser. Verifies emitIntentV1/emitIntents byte layout + xor, and the
// ANNOUNCE/TELEMETRY parser (version handshake). Compiled with the real
// MushinNoShin.cpp against a minimal Stream surrogate.
//
// Build + run:
//   g++ -std=c++17 -O2 -Itools/spirit_test -Isrc/lib/Mushin \
//       tools/spirit_test/main.cpp -o /tmp/mushin_spirit_test && /tmp/mushin_spirit_test
#include <cstdio>
#include <cstring>
#include <Arduino.h>

uint8_t  SerialType::tx[512];
int16_t  SerialType::txlen = 0;
uint8_t  SerialType::rx[512];
int16_t  SerialType::rxlen = 0;
int16_t  SerialType::rxpos = 0;
SerialType Serial1;

#define MUSHIN_ENABLED 1
#define PLATFORM_ESP32 1
#include "MushinNoShin.h"
#include "MushinNoShin.cpp"

static int g_checks = 0, g_fails = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fails++; printf("FAIL %d  %s\n", __LINE__, #cond); } } while (0)
#define CHECK_EQ(a,b) do { g_checks++; long long _a=(long long)(a),_b=(long long)(b); if (_a!=_b){ g_fails++; printf("FAIL %d  %s==%s (got %lld want %lld)\n",__LINE__,#a,#b,_a,_b);} } while(0)

static void tx_reset() { SerialType::txlen = 0; }

static void feed_bytes(const uint8_t* p, int n) {
    for (int i = 0; i < n; i++) SerialType::rx[SerialType::rxlen++] = p[i];
}

int main() {
    MushinNoShin mushin;
    mushin.begin(Serial1);

    // ── [1] emitIntentV1 byte layout + xor ──
    tx_reset();
    MushinIntentV1 p;
    p.throttle = 750; p.flapFreq = 40; p.ferocity = 63; p.skew = -25;
    p.slew = 30; p.stance = 1; p.setpointRoll = 120; p.setpointPitch = -80;
    mushin.emitIntentV1(p);

    const uint8_t* T = SerialType::tx;
    CHECK_EQ(T[0], 0x9B);       // sync
    CHECK_EQ(T[1], 11);         // len = MUSHIN_INTENT_V1_LEN
    CHECK_EQ(T[2], 0x01);       // type = INTENT
    CHECK_EQ(T[3], 750 & 0xFF);      // throttle lo
    CHECK_EQ(T[4], (750 >> 8) & 0xFF);
    CHECK_EQ(T[5], 40);              // flapFreq
    CHECK_EQ(T[6], 63);              // ferocity
    CHECK_EQ(T[7], (uint8_t)-25);    // skew i8
    CHECK_EQ(T[8], 30);              // slew
    CHECK_EQ(T[9], 1);               // stance
    CHECK_EQ(T[10], 120 & 0xFF);     // roll lo
    CHECK_EQ(T[11], (120 >> 8) & 0xFF);
    CHECK_EQ(T[12], (uint8_t)((uint16_t)-80 & 0xFF));   // pitch lo
    CHECK_EQ(T[13], (uint8_t)(((uint16_t)-80 >> 8) & 0xFF));
    uint8_t x = 11 ^ 0x01;
    for (int i = 3; i < 14; i++) x ^= T[i];
    CHECK_EQ(T[14], x);          // xor over len+type+payload
    CHECK_EQ(SerialType::txlen, 15);   // 3 hdr + 11 payload + 1 xor

    // ── [2] emitIntents (v0 µs) layout + xor ──
    tx_reset();
    uint16_t us[3] = {1400, 1500, 1600};
    mushin.emitIntents(us, 3);
    T = SerialType::tx;
    CHECK_EQ(T[0], 0x9B); CHECK_EQ(T[1], 6); CHECK_EQ(T[2], 0x01);
    CHECK_EQ(T[3], 1400 & 0xFF); CHECK_EQ(T[4], (1400 >> 8) & 0xFF);
    CHECK_EQ(T[5], 1500 & 0xFF); CHECK_EQ(T[6], (1500 >> 8) & 0xFF);
    CHECK_EQ(T[7], 1600 & 0xFF); CHECK_EQ(T[8], (1600 >> 8) & 0xFF);
    uint8_t x0 = 6 ^ 0x01;
    for (int i = 3; i < 9; i++) x0 ^= T[i];
    CHECK_EQ(T[9], x0);
    CHECK_EQ(SerialType::txlen, 10);

    // ── [3] ANNOUNCE parse → version handshake (muscle v1) ──
    SerialType::rxlen = 0; SerialType::rxpos = 0;
    {
        // [0x9B, 6, 0x02, ver=1, servos=3, rp2040=1, manji=0, gyro=1, linked=1, xor]
        uint8_t a[10] = {0x9B, 6, 0x02, 1, 3, 1, 0, 1, 1, 0};
        a[9] = 6 ^ 0x02;
        for (int i = 3; i < 9; i++) a[9] ^= a[i];
        feed_bytes(a, 10);
        mushin.update(0);
        CHECK(mushin.isLinked());
        CHECK_EQ(mushin.muscleVersion(), 1);
        CHECK_EQ(mushin.announcedServoCount(), 3);
    }

    // ── [4] ANNOUNCE with v0 muscle → version 0 (fallback trigger) ──
    mushin = MushinNoShin(); mushin.begin(Serial1);
    SerialType::rxlen = 0; SerialType::rxpos = 0;
    {
        uint8_t a[10] = {0x9B, 6, 0x02, 0, 8, 1, 0, 0, 1, 0};
        a[9] = 6 ^ 0x02;
        for (int i = 3; i < 9; i++) a[9] ^= a[i];
        feed_bytes(a, 10);
        mushin.update(0);
        CHECK(mushin.isLinked());
        CHECK_EQ(mushin.muscleVersion(), 0);
        CHECK_EQ(mushin.announcedServoCount(), 8);
    }

    // ── [5] TELEMETRY parse (gyro rate, correction, version) ──
    SerialType::rxlen = 0; SerialType::rxpos = 0;
    {
        // [0x9B, 6, 0x03, r0 r1 c0 c1 linked ver xor]
        int16_t rate = -320; int16_t corr = 45;
        uint8_t t[10] = {0x9B, 6, 0x03,
                         (uint8_t)((uint16_t)rate & 0xFF), (uint8_t)(((uint16_t)rate >> 8) & 0xFF),
                         (uint8_t)((uint16_t)corr & 0xFF), (uint8_t)(((uint16_t)corr >> 8) & 0xFF),
                         1, 1, 0};
        t[9] = 6 ^ 0x03;
        for (int i = 3; i < 9; i++) t[9] ^= t[i];
        feed_bytes(t, 10);
        mushin.update(1000);
        CHECK(mushin.telemetryFresh(1000));
        CHECK_EQ(mushin.telemetry().gyroRate, rate);
        CHECK_EQ(mushin.telemetry().correction, corr);
        CHECK_EQ(mushin.telemetry().version, 1);
        CHECK_EQ(mushin.telemetry().linked, 1);
    }

    // ── [6] heartbeat timeout unlinks ──
    // [4] re-created mushin and fed announce at ms 0; [5] telemetry at ms 1000
    // must not extend the announce freshness, so at ms 2000 the link is stale.
    CHECK(mushin.isLinked());
    mushin.update(2000);   // > 1500 ms stale
    CHECK(!mushin.isLinked());

    printf("\n%d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
