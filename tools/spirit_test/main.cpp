// Functional test for the SPIRIT half of MUSHIN: MushinNoShin framing +
// parser. Verifies the prepared-motion intent emitter (bounded backpressure,
// snapshot immutability, chunked reassembly) and the ANNOUNCE/TELEMETRY parser
// (version handshake, heartbeat timeout). Compiled with the real
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
SerialType Serial;

#define MUSHIN_ENABLED 1
#define PLATFORM_ESP32 1
#include "MushinNoShin.h"
#include "MushinNoShin.cpp"

static int g_checks = 0, g_fails = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_fails++; printf("FAIL %d  %s\n", __LINE__, #cond); } } while (0)
#define CHECK_EQ(a,b) do { g_checks++; long long _a=(long long)(a),_b=(long long)(b); if (_a!=_b){ g_fails++; printf("FAIL %d  %s==%s (got %lld want %lld)\n",__LINE__,#a,#b,_a,_b);} } while(0)

static void tx_reset() { SerialType::txlen = 0; }

static void feed_bytes(const uint8_t* p, int n) {
    uint8_t frame[64];
    int size = Companion::encode(Companion::mushinType, p, n, frame);
    for (int i = 0; i < size; i++) SerialType::rx[SerialType::rxlen++] = frame[i];
}

int main() {
    MushinNoShin mushin;
    mushin.begin(Serial1);

    // ── [1] ANNOUNCE parse → version handshake (muscle v1) ──
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

    // ── [2] ANNOUNCE with v0 muscle → version 0 (fallback trigger) ──
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

    // ── [3] TELEMETRY parse (gyro rate, correction, version) ──
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

    // ── [4] heartbeat timeout unlinks ──
    // [2] re-created mushin and fed announce at ms 0; [3] telemetry at ms 1000
    // must not extend the announce freshness, so at ms 2000 the link is stale.
    CHECK(mushin.isLinked());
    mushin.update(2000);   // > 1500 ms stale
    CHECK(!mushin.isLinked());

    // ── [5] ANNOUNCE len < 6 ignored; corrupt xor ignored ──
    mushin = MushinNoShin(); mushin.begin(Serial1);
    SerialType::rxlen = 0; SerialType::rxpos = 0;
    {
        uint8_t a[7] = {0x9B, 5, 0x02, 1, 3, 1, 0};   // len 5 < 6 → not enough for announce
        a[6] = 5 ^ 0x02;
        for (int i = 3; i < 6; i++) a[6] ^= a[i];
        feed_bytes(a, 7);
        mushin.update(0);
        CHECK(!mushin.isLinked());
    }
    SerialType::rxlen = 0; SerialType::rxpos = 0;
    {
        uint8_t a[10] = {0x9B, 6, 0x02, 1, 3, 1, 0, 1, 1, 0x00};  // wrong xor
        feed_bytes(a, 10);
        mushin.update(0);
        CHECK(!mushin.isLinked());
    }

    // ── [6] TELEMETRY len < 6 ignored ──
    SerialType::rxlen = 0; SerialType::rxpos = 0;
    {
        uint8_t t[7] = {0x9B, 5, 0x03, 0, 0, 0, 0};
        t[6] = 5 ^ 0x03;
        for (int i = 3; i < 6; i++) t[6] ^= t[i];
        feed_bytes(t, 7);
        mushin.update(0);
        CHECK(!mushin.telemetryFresh(0));
    }

    // ── [7] Prepared-motion transport: exact framing, bounded backpressure,
    //        atomic reassembly and no unbounded backlog of stale snapshots ──
    tx_reset();
    Motion::Intent recipe;
    recipe.hz=12;
    motionPart=Motion::chunks; motionSentMs=0;
    SerialType::capacity=0;
    mushinEmitIntent(recipe,100);
    CHECK_EQ(SerialType::txlen,0);
    SerialType::capacity=128;
    recipe.hz=18; // the in-flight snapshot must remain 12 Hz
    mushinEmitIntent(recipe,101);
    CHECK_EQ(SerialType::txlen,Motion::wireSize+Motion::chunks*7);
    Motion::Receiver receiver;
    Motion::Intent active; active.hz=3;
    Companion::Parser parser;
    int accepted=0;
    for(int i=0;i<SerialType::txlen;++i) {
        if(parser.feed(SerialType::tx[i],101)) {
            CHECK_EQ(parser.type(),Motion::frameType);
            if(receiver.accept(parser.payload(),parser.length(),101,active)) ++accepted;
        }
    }
    CHECK_EQ(accepted,1); CHECK_EQ(active.hz,12);
    tx_reset(); mushinEmitIntent(recipe,104); CHECK_EQ(SerialType::txlen,0);
    SerialType::capacity=0; mushinEmitIntent(recipe,111);
    CHECK_EQ(motionPart,0);
    mushinStop(); CHECK_EQ(motionPart,Motion::chunks);
    SerialType::capacity=128;
    printf("\n%d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
