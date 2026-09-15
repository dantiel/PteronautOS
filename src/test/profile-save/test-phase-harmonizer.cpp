#include <cassert>
#include <cmath>
#include <cstdio>

#include "../../lib/Ornithopter/OrnithopterWaveform.h"

static constexpr float kTwoPi = 6.283185307f;
static constexpr float kDt    = 0.0005f;               // 2 kHz simulation
static constexpr float kFreq  = kTwoPi * 2.0f;         // 2 Hz flap (rad/s)

static float wrap(float x)
{
    x = fmodf(x, kTwoPi);
    if (x < 0.0f) x += kTwoPi;
    return x;
}

int main()
{
    // 1. Steady state: no ONDAS demand → the debt stays on beat (≈0) and the
    //    instantaneous cadence equals the commanded base frequency.
    {
        FlappingOscillator osc;
        osc.cadenceTarget = kFreq;
        osc.kGainMod = 1.0f;
        for (int i = 0; i < 4000; ++i) osc.advance(kDt);   // 2 s settle
        assert(std::fabs(osc.phaseOffset) < 0.02f);
        assert(std::fabs(osc.cadence - kFreq) < 0.01f);
    }

    // 2. Weak nudge: demand below the slip barrier (extraTarget < ω₀/(2ζ))
    //    bends the phase but RINGS back to the SAME beat — no whole-stroke
    //    slip, and the debt velocity crosses zero (underdamped inertia).
    {
        FlappingOscillator osc;
        osc.cadenceTarget = kFreq;
        osc.kGainMod = 1.0f;
        for (int i = 0; i < 4000; ++i) osc.advance(kDt);
        osc.kGainMod = 1.2f;                       // extraTarget = 2.51 rad/s < 7.14
        for (int i = 0; i < 500; ++i) osc.advance(kDt);   // 0.25 s demand
        osc.kGainMod = 1.0f;
        float debtVelMin = 1e9f;
        for (int i = 0; i < 2000; ++i) {           // 1 s ring-down
            osc.advance(kDt);
            if (osc.debtVel < debtVelMin) debtVelMin = osc.debtVel;
        }
        assert(std::fabs(osc.phaseOffset) < 0.05f);   // back on the SAME beat
        assert(debtVelMin < -0.001f);                 // rang (ζ<1 → inertial)
    }

    // 3. Strong kick: demand above the barrier whips the debt over a whole
    //    stroke — the offset settles at an integer multiple of 2π (quantized),
    //    never a fractional beat, and the cadence returns to the base.
    {
        FlappingOscillator osc;
        osc.cadenceTarget = kFreq;
        osc.kGainMod = 1.0f;
        for (int i = 0; i < 4000; ++i) osc.advance(kDt);
        osc.kGainMod = 2.0f;                       // extraTarget = 12.57 > 7.14
        for (int i = 0; i < 1100; ++i) osc.advance(kDt);  // ~0.55 s → one slip
        osc.kGainMod = 1.0f;
        for (int i = 0; i < 4000; ++i) osc.advance(kDt);  // 2 s ring-down
        assert(osc.phaseOffset > kTwoPi - 0.3f);          // slipped ≥ 1 stroke
        assert(wrap(osc.phaseOffset) < 0.02f);            // ... and lands ON a beat
        assert(std::fabs(osc.cadence - kFreq) < 0.01f);   // base cadence restored
    }

    std::printf("Phase-quantized harmonizer: steady-state + weak-ring + whole-stroke slip passed\n");
    return 0;
}
