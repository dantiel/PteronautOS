#pragma once
/*
  Flapping Oscillator — phase accumulator + asymmetric tanh waveform.
  Ported from GralhaAzul::animarPulsarDoCoracaoAlado() + formaDoBaterDasAsas().
*/

#include <cstdint>
#include <cmath>

class FlappingOscillator {
public:
    float phase;         // actual flap phase [rad], kept in [0, 2π)
    float cadence;       // instantaneous flap rate [rad/s] (base + debt momentum)
    float cadenceTarget; // commanded base flap rate [rad/s] — the beat grid
    float kGainMod;      // ONDAS phase-advance demand: 1.0=nominal, >1 = brief faster flap
    float anchorGain;    // beat-locking stiffness delta (0=ω₀=10 soft, 100=ω₀=110 stiff)
    float basePhase;     // virtual beat grid [rad] — always advances at cadenceTarget
    float phaseOffset;   // debt to the grid [rad] — settles on whole strokes (2π·k)
    float debtVel;       // debt momentum [rad/s] — the pendulum's extra flap rate

    FlappingOscillator()
        : phase(0), cadence(0), cadenceTarget(0), kGainMod(1.0f), anchorGain(0.0f),
          basePhase(0), phaseOffset(0), debtVel(0) {}

    float advance(float dt);
    // limiarShared (optional): shared reversal threshold [rad] between the two
    // wings, so left/right reverse at the SAME phase even when their ferocities
    // differ (rudder differential). < 0 means "compute from this wing's own
    // ferocities" (legacy behaviour).
    //
    // strokeSkewPercent / returnSkewPercent (±100) shift the CENTRE of each
    // half-stroke along its own [start…end] axis — the same per-half mirroring
    // as stroke/return ferocity ("as above so below"). + shifts the centre
    // toward the START of the half (augmented thrust: the wing reaches peak
    // velocity sooner) AND lengthens the leading plateau of the square family;
    // − shifts both toward the END (diminished/late thrust, longer trailing
    // plateau). 0 keeps the wave symmetric. The warp is monotonic and
    // end-point-preserving — exactly an asymmetric mix of the square (dwell)
    // and triangular families, never a position jump.
    static float shapeWave(float theta, float strokeFerocity, float returnFerocity,
                           float limiarShared = -1.0f, float shapeMixPercent = 0.0f,
                           float strokeSkewPercent = 0.0f, float returnSkewPercent = 0.0f);
    void decay(float dt);
    void reset();
};

inline float FlappingOscillator::advance(float dt) {
    constexpr float kBaseDamp = 10.0f;  // beat-locking natural frequency [rad/s] at anchorGain=0
    constexpr float kZeta     = 0.7f;   // underdamped → the debt RINGS and decays (inertia)
    constexpr float kTwoPi    = 6.283185307f;
    float omega0 = kBaseDamp + anchorGain;  // anchorGain=0→10 (soft catch), 100→110 (stiff catch)

    // The base beat grid always advances at the commanded flap frequency, so
    // "on beat" is a whole number of strokes (2π) ahead or behind it.
    basePhase += cadenceTarget * dt;
    basePhase = fmodf(basePhase, kTwoPi);
    if (basePhase < 0.0f) basePhase += kTwoPi;

    // ONDAS phase-advance demand: kGainMod > 1 asks to flap faster briefly.
    // This is a *rate* target for the debt (extra frequency), not a bare force.
    float extraTarget = (kGainMod - 1.0f) * cadenceTarget;

    // Josephson washboard — phase-quantized harmonization. The debt φ_offset
    // is attracted to whole strokes (multiples of 2π) by −ω₀²·sin(φ_offset).
    // Weak demand only nudges the phase and rings back to the SAME beat; a
    // strong demand (extraTarget > ω₀/(2ζ)) whips the debt over the π barrier —
    // a *quantized* phase slip of exactly one whole stroke — so the flap always
    // lands on a beat, never between beats. ζ<1 keeps the return inertial
    // (pendulum momentum):
    //   φ_offset'' = −ω₀²·sin(φ_offset) − 2ζω₀·(φ_offset' − extraTarget)
    debtVel += (-omega0 * omega0 * sinf(phaseOffset)
                - 2.0f * kZeta * omega0 * (debtVel - extraTarget)) * dt;
    phaseOffset += debtVel * dt;

    // Actual flap phase = base grid + debt; instantaneous rate = base + debt velocity.
    cadence = cadenceTarget + debtVel;
    phase = basePhase + phaseOffset;

    // Keep the returned phase in [0, 2π) for wave shaping.
    phase = fmodf(phase, kTwoPi);
    if (phase < 0.0f) phase += kTwoPi;
    return phase;
}

inline float FlappingOscillator::shapeWave(
    float theta, float strokeFerocity, float returnFerocity,
    float limiarShared, float shapeMixPercent,
    float strokeSkewPercent, float returnSkewPercent
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

    // Centre-skew: warp the normalized half-stroke phase t∈[0,1] monotonically
    // so the wave's centre (peak velocity) moves toward the start (+) or the
    // end (−) of the half. Endpoints stay pinned (0→0, 1→1), so reversal is
    // continuous; the interior shifts by a quadratic-bias remap. This is the
    // asymmetric square↔triangle mix expressed as a single signed parameter.
    float skew01 = (descida ? strokeSkewPercent : returnSkewPercent) * 0.01f;
    if (skew01 < -1.0f) skew01 = -1.0f;
    else if (skew01 > 1.0f) skew01 = 1.0f;
    if (skew01 != 0.0f) {
        // quadratic bias: t' = t + s·t·(1−t). s>0 pushes the centre toward the
        // start (front-load thrust), s<0 toward the end (late thrust).
        t = t + skew01 * t * (1.0f - t);
    }

    float ferocity01 = f * 0.125f;
    float d = ferocity01 * kMaxDwell;

    // Skew also redistributes the square-wave plateau: the dwell is no longer
    // split 50/50. +s holds the START of the half longer (front-load — the
    // wing dwells at full extension before the ramp), −s holds the END longer
    // (late thrust). front + back still sum to d, so the ramp keeps its width
    // and only its position within the half moves — monotonic, no jump.
    float dh = d * 0.5f;
    float frontDwell = dh * (1.0f + skew01);
    float backDwell  = dh * (1.0f - skew01);

    float plateau;
    if (t < frontDwell) plateau = 1.0f;
    else if (t > 1.0f - backDwell) plateau = -1.0f;
    else plateau = cosf(kPi * (t - frontDwell) / (1.0f - d));

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
    basePhase = 0;
    phaseOffset *= 0.90f;   // the debt pendulum rings down with the flap
    if (fabsf(phaseOffset) < 0.001f) phaseOffset = 0;
    debtVel *= 0.90f;
    if (fabsf(debtVel) < 0.001f) debtVel = 0;
}

inline void FlappingOscillator::reset() {
    phase = 0;
    cadence = 0;
    cadenceTarget = 0;
    kGainMod = 1.0f;
    anchorGain = 0.0f;
    basePhase = 0;
    phaseOffset = 0;
    debtVel = 0;
}