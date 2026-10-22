#include <cassert>
#include <cmath>
#include <cstdio>
#include "Ornithopter.h"

// Regression test for the complete tail-surface mixers (profiles 8–12) and the
// Z-Wing 9-PWM authority matrix. Compiled WITHOUT ZEPHYRUS_ENABLED, so every
// gyro correction is 0 and the mixer is pure pilot-stick math — exact µs.
//
// CRSF norm (Ornithopter::_crsfToNorm): 172 → −1, 992 → 0, 1811 → +1.
// (992 is actually +0.0006 — the exact centre is raw 991.5 — so assertions
// that must compare two near-equal servos use a ±2 µs tolerance.)

uint32_t ChannelData[16];
static uint32_t clockUs = 1000;
unsigned long micros() { return clockUs; }

static bool near(int a, int b, int tol = 2) { return std::abs(a - b) <= tol; }

int main() {
    ornithopter.enabled = true;
    ornithopter.linkUp = true;
    ornithopter.stickOverride = true;
    for (int i = 0; i < SF_COUNT; ++i) ornithopter.servoTrimUs[i] = 0;
    ornithopter.rudderYawWeight  = 100;   // head rudder = pure yaw (not asserted)
    ornithopter.rudderRollWeight = 0;
    ornithopter.tailYawCoupling  = 100;   // full pitch-gated yaw coupling
    ornithopter.tailDihedralMix  = 0;     // flat elevons by default

    // Arm once: arm high + throttle zero (below the pre-arm threshold).
    ornithopter.setStickChannel(STK_ARM, 1811);
    ornithopter.setStickChannel(STK_THROTTLE, 172);
    ornithopter.setStickChannel(STK_PROFILE, 992);   // flight profile 1 (no switch)
    ornithopter.update();

    const int C = 1500;
    const uint16_t LO = 172, MID = 992, HI = 1811;

    // ── 3-servo rotation tail (profile 8) ─────────────────────────────
    // neutral pitch + full rudder → rotation is pure roll (zero here) → zero yaw
    setOrnithopterProfile(GEARBOX_1MOT_1RUD_TAIL3);
    ornithopter.setStickChannel(STK_AILERON,  MID);
    ornithopter.setStickChannel(STK_ELEVATOR, MID);
    ornithopter.setStickChannel(STK_RUDDER,   HI);
    ornithopter.update();
    assert(near(ornithopter.funcValue(SF_TAIL_ROLL_L), C));
    assert(near(ornithopter.funcValue(SF_TAIL_ROLL_R), C));
    // pitch UP + rudder → yaw rides the rotation differential (positive)
    ornithopter.setStickChannel(STK_ELEVATOR, HI);
    ornithopter.update();
    assert(ornithopter.funcValue(SF_TAIL_ROLL_L) > ornithopter.funcValue(SF_TAIL_ROLL_R));
    // pitch DOWN + rudder → yaw differential INVERTS
    ornithopter.setStickChannel(STK_ELEVATOR, LO);
    ornithopter.update();
    assert(ornithopter.funcValue(SF_TAIL_ROLL_L) < ornithopter.funcValue(SF_TAIL_ROLL_R));

    // ── 1-pitch + 1-rotate tail (profile 10) — same physics, one rotate ──
    setOrnithopterProfile(GEARBOX_1MOT_1RUD_TAIL11);
    ornithopter.setStickChannel(STK_AILERON,  MID);
    ornithopter.setStickChannel(STK_ELEVATOR, MID);
    ornithopter.setStickChannel(STK_RUDDER,   HI);
    ornithopter.update();
    assert(near(ornithopter.funcValue(SF_TAIL_ROLL_L), C));   // neutral → no yaw
    ornithopter.setStickChannel(STK_ELEVATOR, HI);
    ornithopter.update();
    assert(ornithopter.funcValue(SF_TAIL_ROLL_L) > C);        // pitched up → yaw +
    ornithopter.setStickChannel(STK_ELEVATOR, LO);
    ornithopter.update();
    assert(ornithopter.funcValue(SF_TAIL_ROLL_L) < C);        // pitched down → yaw −

    // ── merged tail (profile 11): pitch = common mode, rotation = differential ──
    setOrnithopterProfile(GEARBOX_1MOT_1RUD_TAILMERGED);
    ornithopter.setStickChannel(STK_AILERON,  MID);
    ornithopter.setStickChannel(STK_ELEVATOR, HI);   // pitch up, no yaw
    ornithopter.setStickChannel(STK_RUDDER,   MID);
    ornithopter.update();
    assert(ornithopter.funcValue(SF_TAIL_PITCHROT_L) > C);    // both rise together
    assert(ornithopter.funcValue(SF_TAIL_PITCHROT_R) > C);
    assert(near(ornithopter.funcValue(SF_TAIL_PITCHROT_L),
                ornithopter.funcValue(SF_TAIL_PITCHROT_R)));
    ornithopter.setStickChannel(STK_RUDDER, HI);              // now yaw + pitch
    ornithopter.update();
    assert(ornithopter.funcValue(SF_TAIL_PITCHROT_L) > ornithopter.funcValue(SF_TAIL_PITCHROT_R));

    // ── split elevon tail (profile 9): di/anhedral slider gates yaw ──
    setOrnithopterProfile(GEARBOX_1MOT_1RUD_ELEVON);
    ornithopter.setStickChannel(STK_AILERON,  MID);
    ornithopter.setStickChannel(STK_ELEVATOR, MID);
    ornithopter.setStickChannel(STK_RUDDER,   HI);
    ornithopter.tailDihedralMix = 0;      // flat → yaw rides nothing
    ornithopter.update();
    assert(near(ornithopter.funcValue(SF_ELEVON_L), ornithopter.funcValue(SF_ELEVON_R)));
    ornithopter.tailDihedralMix = 100;    // dihedral → yaw rides roll differential +
    ornithopter.update();
    assert(ornithopter.funcValue(SF_ELEVON_L) > ornithopter.funcValue(SF_ELEVON_R));
    ornithopter.tailDihedralMix = -100;   // anhedral → inverted
    ornithopter.update();
    assert(ornithopter.funcValue(SF_ELEVON_L) < ornithopter.funcValue(SF_ELEVON_R));

    // ── Z-Wing 9-PWM (profile 12) ─────────────────────────────────────
    setOrnithopterProfile(ZWING_9PWM);
    // aileron roll: wings 75%, tail 35%
    ornithopter.setStickChannel(STK_AILERON,  HI);
    ornithopter.setStickChannel(STK_ELEVATOR, MID);
    ornithopter.setStickChannel(STK_RUDDER,   MID);
    ornithopter.setStickChannel(STK_FREQ,     MID);
    ornithopter.update();
    assert(ornithopter.funcValue(SF_WING_AIL_L) > C);
    assert(ornithopter.funcValue(SF_WING_AIL_R) < C);
    assert(ornithopter.funcValue(SF_TAIL_ROLL_L) > C);
    assert(ornithopter.funcValue(SF_TAIL_ROLL_R) < C);
    // drag rudders: yaw differential (60%)
    ornithopter.setStickChannel(STK_AILERON, MID);
    ornithopter.setStickChannel(STK_RUDDER,  HI);
    ornithopter.setStickChannel(STK_FREQ,    LO);   // airbrake retracted
    ornithopter.update();
    assert(ornithopter.funcValue(SF_WING_DRAG_L) > C);
    assert(ornithopter.funcValue(SF_WING_DRAG_R) < C);
    // airbrake: common spread (≤300 µs) lifts both
    ornithopter.setStickChannel(STK_RUDDER, MID);
    ornithopter.setStickChannel(STK_FREQ,   HI);    // airbrake full
    ornithopter.update();
    assert(ornithopter.funcValue(SF_WING_DRAG_L) > C);
    assert(ornithopter.funcValue(SF_WING_DRAG_R) > C);
    assert(near(ornithopter.funcValue(SF_WING_DRAG_L), ornithopter.funcValue(SF_WING_DRAG_R)));

    puts("tail mixers: rotation zero-yaw, pitch-gated yaw inversion, merged common-mode, "
         "elevon dihedral gate, and Z-Wing authority matrix passed");
}
