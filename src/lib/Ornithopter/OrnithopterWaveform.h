#pragma once
/*
  Flapping Oscillator — phase accumulator + asymmetric tanh waveform.
  Ported from GralhaAzul::animarPulsarDoCoracaoAlado() + formaDoBaterDasAsas().
*/

#include <cstdint>
#include <cmath>

// ── Wave-shape lookup tables ───────────────────────────────────────────
// Defined in OrnithopterWaveformTables.cpp; declared here so shapeWave()
// (inline) can use them. PROGMEM places the tables in flash (.irom.text,
// memory-mapped at 0x40200000) so they cost ZERO heap/DRAM — read
// transparently via direct dereference (the ESP8266 flash cache serves the
// read; no pgm_read needed). The mixer runs in the main loop, never an ISR,
// so cache reads are always safe.
constexpr float kWaveMaxDwell    = 0.98f;  // never emit an impossible position jump
constexpr float kWaveMaxPoint    = 0.98f;  // rounded, never infinite-accel triangle
constexpr int   kWaveCosHalfN    = 256;    // phase intervals → 257 samples
constexpr int   kWavePointedRows = 16;     // pointK bins (bilinear at runtime)

extern const float kWaveCosHalf[kWaveCosHalfN + 1] PROGMEM;
extern const float kWavePointed[kWavePointedRows][kWaveCosHalfN + 1] PROGMEM;

// cos(π·u), u∈[0,1] → [1,−1], linear interpolation over kWaveCosHalf.
static inline float waveCosHalfLerp(float u)
{
    float pos = u * (float)kWaveCosHalfN;
    int i = (int)pos;
    if (i < 0) i = 0;
    else if (i > kWaveCosHalfN - 1) i = kWaveCosHalfN - 1;
    float frac = pos - (float)i;
    float a = kWaveCosHalf[i];
    return a + (kWaveCosHalf[i + 1] - a) * frac;
}

// asin(pointK·cos(π·t)) / asin(pointK) — the ferocity-coupled pointedness
// map, 2-D bilinear over kWavePointed (pointK∈[0,kWaveMaxPoint], t∈[0,1]).
static inline float wavePointedLerp(float pointK, float t)
{
    float kPos = pointK * (1.0f / kWaveMaxPoint) * (float)(kWavePointedRows - 1);
    int ki = (int)kPos;
    if (ki < 0) ki = 0;
    else if (ki > kWavePointedRows - 2) ki = kWavePointedRows - 2;
    float kFrac = kPos - (float)ki;

    float tPos = t * (float)kWaveCosHalfN;
    int ti = (int)tPos;
    if (ti < 0) ti = 0;
    else if (ti > kWaveCosHalfN - 1) ti = kWaveCosHalfN - 1;
    float tFrac = tPos - (float)ti;

    const float* r0 = kWavePointed[ki];
    const float* r1 = kWavePointed[ki + 1];
    float row0 = r0[ti] + (r0[ti + 1] - r0[ti]) * tFrac;
    float row1 = r1[ti] + (r1[ti + 1] - r1[ti]) * tFrac;
    return row0 + (row1 - row0) * kFrac;
}

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
    constexpr float kTwoPi = 6.283185307f;

    // theta is guaranteed in [0, 2π) by FlappingOscillator::advance() — the
    // only caller feeds rawWave straight out of advance(). The fmodf
    // normalisation here was redundant and is removed: a real per-tick win on
    // the no-FPU ESP8285. A future caller with an out-of-range phase must
    // normalise before calling.

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
    float d = ferocity01 * kWaveMaxDwell;

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
    else plateau = waveCosHalfLerp((t - frontDwell) / (1.0f - d));

    // Coupling pointedness to this half's ferocity makes a strong half-stroke
    // direct and pyramidal while a weaker, elongated half remains sinusoidal.
    // The eased mapping reaches the direct-stroke family decisively at high
    // ferocity without ever reaching the sharp k=1 triangle singularity.
    float pointK = kWaveMaxPoint * (2.0f * ferocity01 - ferocity01 * ferocity01);
    float pointed = pointK < 0.0001f
        ? waveCosHalfLerp(t)
        : wavePointedLerp(pointK, t);

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