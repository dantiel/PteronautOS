#include "Ornithopter.h"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────
//  ONDAS — Nigredo waveform modulation (Mahony/Zephyrus path only)
// ─────────────────────────────────────────────────────────────────────
// Cadence P → phase advance, ferocity PD-blend with the inertial
// harmonizer, balance I → asymmetry, resonance lock-in pump, and the
// stroke-synchronous feed-forward (SSFF). All build-variant guards live
// here; the mixer just sums the returned terms. Compiled out under
// MESOZOIC_ONLY (the rate-only brain owns its own correction paths).

void Ornithopter::_ondasCadenceApply(float dt)
{
#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
    // Real Ondas wiring (Nigredo): Zephyrus bridges the raw pitch PID terms
    // at 250 Hz (ZephyrusFilter.h) and NaN-guards them upstream (Validatio),
    // so the values consumed here are live and bounded.
    // Cadence P → Phase Advance: nose-up asks for a brief faster flap,
    // nose-down a brief slower one (clamped to [0.5, 2.0]). The demand
    // feeds the phase-quantized Josephson washboard in advance(): a weak
    // demand bends the phase and rings back to the same beat, a strong one
    // slips a WHOLE stroke — the flap always lands on a beat, never between.
    _osc.kGainMod = 1.0f + gyroPitchPTerm * aeroGainScale * cadenceGain * 0.00005f;
    if (_osc.kGainMod < 0.5f) _osc.kGainMod = 0.5f;
    if (_osc.kGainMod > 2.0f) _osc.kGainMod = 2.0f;
    _osc.anchorGain = anchorGain;
#else
    _osc.anchorGain = 0.0f;
#endif
}

void Ornithopter::_ondasZeroStaleBiases()
{
#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
    if (ssffGain <= 0.0f) {
        // Purificatio: zero stale biases when SSFF is disabled
        _ssffFerocityUpBias   = 0.0f;
        _ssffFerocityDownBias = 0.0f;
    }
#endif
}

float Ornithopter::_ondasFerocityBlend(float dt)
{
#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
    // Ferocity PD-blend: P-term + D-term → dwell ratio (clamped ±0.5).
    float ferocitySignal = (gyroPitchPTerm * ferocityPGain * 0.00015f
                          + gyroPitchDTerm * ferocityDGain * 0.0003f) * aeroGainScale;
    if (ferocitySignal > 0.5f) ferocitySignal = 0.5f;
    if (ferocitySignal < -0.5f) ferocitySignal = -0.5f;
    // Inertial harmonizer: the held dwell bias is a damped pendulum
    // (ω₀=10, ζ=0.7) tracking the live PD blend. It catches the demanded
    // ferocity with pendulum momentum and decays back inertially, so the
    // dwell change lands in step with the phase-quantized cadence above
    // rather than snapping the reversal boundary mid-stroke.
    {
        constexpr float fOmega = 10.0f;
        constexpr float fZeta  = 0.7f;
        _ferHoldVel += (-fOmega * fOmega * (_ferHold - ferocitySignal)
                        - 2.0f * fZeta * fOmega * _ferHoldVel) * dt;
        _ferHold += _ferHoldVel * dt;
    }
    return _ferHold;
#else
    (void)dt;
    return 0.0f;
#endif
}

float Ornithopter::_ondasBalanceBias()
{
#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
    // Balance I → asymmetry: accumulated pitch error shifts the
    // stroke centre (clamped to ±3.0 ferocity units).
    float iBias = gyroPitchITerm * aeroGainScale * balanceGain * 0.0001f;
    if (iBias > 3.0f) iBias = 3.0f;
    if (iBias < -3.0f) iBias = -3.0f;
    return iBias;
#else
    return 0.0f;
#endif
}

float Ornithopter::_ondasResonanceBias()
{
#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
    // Resonance lock-in pump feeds both half-strokes symmetrically.
    return _resonanceAccum;
#else
    return 0.0f;
#endif
}

void Ornithopter::_ondasStrokeSyncUpdate(float dt, float rawWave, float limiarShared)
{
#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
    // SSFF — stroke-synchronous feed-forward: use exactly the boundary
    // consumed by both shapeWave calls, not π. Updated biases take effect on
    // the NEXT mixer tick: do not recompute this tick's boundary from its
    // own feedback event.
    const float strokeSign = rawWave < limiarShared ? 1.0f : -1.0f;
    if (_prevFlappingSin != strokeSign) {
        if (_ssffAccumCount > 0 && ssffGain > 0.0f) {
            float bias = (_ssffAccumError / (float)_ssffAccumCount) * ssffGain * 0.00001f;
            if (bias > 2.0f) bias = 2.0f;
            if (bias < -2.0f) bias = -2.0f;
            if (strokeSign > 0.0f) _ssffFerocityDownBias = bias;
            else                  _ssffFerocityUpBias = bias;
        }
        _ssffAccumError = 0.0f;
        _ssffAccumCount = 0;
    }
    _ssffAccumError += gyroPitchErrorRate;
    _ssffAccumCount++;
    _prevFlappingSin = strokeSign;

    // Resonance — phase-locked lock-in amplifier: accumulate
    // errorRate × sin(phase), leaky τ = 0.15 s, clamped to ±2.0.
    // Uses the oscillator fundamental (not the shaped reversal boundary).
    if (resonanceGain > 0.0f) {
        const float waveSin = sinf(rawWave);
        _resonanceAccum += gyroPitchErrorRate * waveSin * resonanceGain * 0.01f * dt;
        _resonanceAccum *= expf(-dt / 0.15f);
        if (_resonanceAccum > 2.0f) _resonanceAccum = 2.0f;
        if (_resonanceAccum < -2.0f) _resonanceAccum = -2.0f;
    }
#else
    (void)dt; (void)rawWave; (void)limiarShared;
#endif
}

void Ornithopter::_ondasReset()
{
#ifdef ZEPHYRUS_ENABLED
    // Fresh biases for each flight / after failsafe.
    _prevFlappingSin = 0.0f;
    _ssffAccumError = 0.0f;
    _ssffAccumCount = 0;
    _ssffFerocityUpBias = 0.0f;
    _ssffFerocityDownBias = 0.0f;
    _ferHold = 0.0f;
    _ferHoldVel = 0.0f;
#endif
}

void Ornithopter::_ondasGlidePurge()
{
#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
    // Glide pause: purge half-stroke accumulators so a fresh flap
    // burst starts clean (no stale SSFF biases / resonance charge).
    _ssffAccumError = 0.0f;
    _ssffAccumCount = 0;
    _ssffFerocityUpBias   = 0.0f;
    _ssffFerocityDownBias = 0.0f;
    _resonanceAccum = 0.0f;
    _prevFlappingSin = 0.0f;
    _ferHold = 0.0f;
    _ferHoldVel = 0.0f;
#endif
}
