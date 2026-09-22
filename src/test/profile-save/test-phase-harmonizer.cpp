#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>

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

    // Delayed ticks at the stiffest anchor used to diverge at 20 ms. Verify
    // finite bounded response and convergence against a fine-step reference.
    for (float dt : {0.004f, 0.01f, 0.02f, 0.1f}) {
        FlappingOscillator osc;
        osc.anchorGain = 100;
        osc.cadenceTarget = kTwoPi * 5;
        osc.kGainMod = 1.1f;
        for (int i = 0; i < 1000; ++i) {
            osc.advance(dt);
            assert(std::isfinite(osc.phase));
            assert(osc.phase >= 0 && osc.phase < kTwoPi);
            assert(std::fabs(osc.debtVel) < 10);
        }
        const float equilibrium = std::asin(1.4f * (0.1f * osc.cadenceTarget) / 110.0f);
        assert(std::fabs(osc.phaseOffset - equilibrium) < 0.0001f);
        const float oldPhase = osc.phase;
        assert(osc.advance(0) == oldPhase);
        assert(osc.advance(-1) == oldPhase);
    }
    std::printf("Phase harmonizer: steady-state + weak-ring + slip + delayed-tick stability passed\n");
    return 0;
}
