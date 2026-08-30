#pragma once
/*
  Flapping Oscillator — phase accumulator + asymmetric tanh waveform.
  Ported from GralhaAzul::animarPulsarDoCoracaoAlado() + formaDoBaterDasAsas().
*/

#include <cstdint>
#include <cmath>

class FlappingOscillator {
public:
    float phase;
    float cadence;
    float cadenceTarget;
    float kGainMod;   // Phase advance multiplier (1.0=nominal, 0.5-2.0 from Cadence P→Phase)
    float anchorGain; // k₂ damping delta (0=k₂=10 tight, 100=k₂=20)

    FlappingOscillator() : phase(0), cadence(0), cadenceTarget(0), kGainMod(1.0f), anchorGain(0.0f) {}

    float advance(float dt);
    // limiarShared (optional): shared reversal threshold [rad] between the two
    // wings, so left/right reverse at the SAME phase even when their ferocities
    // differ (rudder differential). < 0 means "compute from this wing's own
    // ferocities" (legacy behaviour).
    static float shapeWave(float theta, float strokeFerocity, float returnFerocity, float limiarShared = -1.0f);
    void decay(float dt);
    void reset();
};

inline float FlappingOscillator::advance(float dt) {
    constexpr float kBaseDamp = 10.0f;
    constexpr float kGain = kBaseDamp;  // unity steady-state: cadence == cadenceTarget (bird-like FREQ mode)
    constexpr float kTwoPi = 6.283185307f;
    float kDamp = kBaseDamp + anchorGain;  // anchorGain=0→k₂=10 (tight), anchorGain=100→k₂=110
    float error = kGain * kGainMod * cadenceTarget - kDamp * cadence;
    cadence += error * dt;
    phase += cadence * dt;
    // Return the raw phase angle (radians) for wave shaping, kept in [0, 2π).
    phase = fmodf(phase, kTwoPi);
    if (phase < 0.0f) phase += kTwoPi;
    return phase;
}

inline float FlappingOscillator::shapeWave(
    float theta, float strokeFerocity, float returnFerocity, float limiarShared
) {
    // Faithful port of GralhaAzul::formaDoBaterDasAsas().
    // Trapezoidal half-wave: dwell + cos ramp, continuous across all boundaries.
    constexpr float kPi = 3.14159265358979f;
    constexpr float kTwoPi = 6.283185307f;

    // Normalize phase into [0, 2π)
    theta = fmodf(theta, kTwoPi);
    if (theta < 0.0f) theta += kTwoPi;

    // Fast-path: max ferocity → pure square wave (limiarShared shifts the 50/50 boundary)
    if (strokeFerocity >= 7.999f && returnFerocity >= 7.999f) {
        float limiar = (limiarShared >= 0.0f) ? limiarShared : kPi;
        return (theta < limiar) ? 1.0f : -1.0f;
    }

    float fD = strokeFerocity; if (fD < 0.0f) fD = 0.0f; else if (fD > 8.0f) fD = 8.0f;
    float fS = returnFerocity; if (fS < 0.0f) fS = 0.0f; else if (fS > 8.0f) fS = 8.0f;

    // Shared reversal threshold between downstroke (descida) and upstroke (subida)
    float limiar;
    if (limiarShared >= 0.0f) {
        limiar = limiarShared;
    } else {
        float wD = 8.0f - fD; if (wD < 0.01f) wD = 0.01f;
        float wS = 8.0f - fS; if (wS < 0.01f) wS = 0.01f;
        limiar = kTwoPi * wD / (wD + wS);
    }

    bool descida = (theta < limiar);
    float t, f;
    if (descida) {
        t = theta / limiar;
        f = fD;
    } else {
        t = (theta - limiar) / (kTwoPi - limiar);
        f = fS;
    }

    float d = f * 0.125f;   // f/8, dwell ratio ∈ [0,1]
    float dh = d * 0.5f;    // half-dwell per extreme

    if (d >= 1.0f) return descida ? 1.0f : -1.0f;
    if (t < dh) return descida ? 1.0f : -1.0f;             // dwell at start
    if (t > 1.0f - dh) return descida ? -1.0f : 1.0f;      // dwell at end
    float ramp = cosf(kPi * (t - dh) / (1.0f - d));
    return descida ? ramp : -ramp;
}

inline void FlappingOscillator::decay(float /*dt*/) {
    phase = 0;
    cadence *= 0.90f;
    if (fabsf(cadence) < 0.001f) cadence = 0;
    cadenceTarget = 0;
}

inline void FlappingOscillator::reset() {
    phase = 0;
    cadence = 0;
    cadenceTarget = 0;
    kGainMod = 1.0f;
    anchorGain = 0.0f;
}