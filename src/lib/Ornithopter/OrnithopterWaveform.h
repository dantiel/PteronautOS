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
    static float shapeWave(float theta, float strokeFerocity, float returnFerocity,
                           float limiarShared = -1.0f, float shapeMixPercent = 0.0f);
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
    float theta, float strokeFerocity, float returnFerocity,
    float limiarShared, float shapeMixPercent
) {
    // The original GralhaAzul mode is a dwell/plateau plus a compressed cosine
    // ramp. shapeMixPercent continuously transmutes it into a rounded pyramidal
    // stroke: no dwell, nearly constant velocity through the middle, and a
    // finite-acceleration reversal. Each half uses its own ferocity, while the
    // shared reversal threshold below preserves anticipation from asymmetry.
    constexpr float kPi = 3.14159265358979f;
    constexpr float kTwoPi = 6.283185307f;
    constexpr float kMaxDwell = 0.98f;  // never emit an impossible position jump
    constexpr float kMaxPoint = 0.98f;  // rounded rather than infinite-acceleration triangle

    // Normalize phase into [0, 2π)
    theta = fmodf(theta, kTwoPi);
    if (theta < 0.0f) theta += kTwoPi;

    float fD = strokeFerocity; if (fD < 0.0f) fD = 0.0f; else if (fD > 8.0f) fD = 8.0f;
    float fS = returnFerocity; if (fS < 0.0f) fS = 0.0f; else if (fS > 8.0f) fS = 8.0f;
    float shapeMix = shapeMixPercent * 0.01f;
    if (shapeMix < 0.0f) shapeMix = 0.0f;
    else if (shapeMix > 1.0f) shapeMix = 1.0f;

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

    float ferocity01 = f * 0.125f;
    float d = ferocity01 * kMaxDwell;
    float dh = d * 0.5f;    // half-dwell per extreme

    float plateau;
    if (t < dh) plateau = 1.0f;
    else if (t > 1.0f - dh) plateau = -1.0f;
    else plateau = cosf(kPi * (t - dh) / (1.0f - d));

    // Coupling pointedness to this half's ferocity makes a strong half-stroke
    // direct and pyramidal while a weaker, elongated half remains sinusoidal.
    // The eased mapping reaches the direct-stroke family decisively at high
    // ferocity without ever reaching the sharp k=1 triangle singularity.
    float pointK = kMaxPoint * (2.0f * ferocity01 - ferocity01 * ferocity01);
    float pointed = pointK < 0.0001f
        ? cosf(kPi * t)
        : asinf(pointK * cosf(kPi * t)) / asinf(pointK);

    float halfWave = plateau + (pointed - plateau) * shapeMix;
    return descida ? halfWave : -halfWave;
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
