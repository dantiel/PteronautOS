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

    FlappingOscillator() : phase(0), cadence(0), cadenceTarget(0) {}

    float advance(float dt);
    static float shapeWave(float rawSin, float strokeFerocity, float returnFerocity);
    void decay(float dt);
    void reset();
};

inline float FlappingOscillator::advance(float dt) {
    constexpr float kGain = 1.0f;
    constexpr float kDamp = 10.0f;
    float error = kGain * cadenceTarget - kDamp * cadence;
    cadence += error * dt;
    phase += cadence * dt;
    return sinf(phase);
}

inline float FlappingOscillator::shapeWave(
    float rawSin, float strokeFerocity, float returnFerocity
) {
    float ferocity = (rawSin >= 0.0f) ? strokeFerocity : returnFerocity;
    float denom = tanhf(ferocity);
    if (denom < 0.000001f) return rawSin;
    float result = tanhf(ferocity * rawSin) / denom;
    if (result > 1.0f) result = 1.0f;
    if (result < -1.0f) result = -1.0f;
    return result;
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
}
