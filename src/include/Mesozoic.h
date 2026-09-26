#pragma once
/*
  Mesozoic — the bare rate-damper brain (PteronautOS)
  One primitive fixed-point PID per axis, integer-only. No sinf, no cosf,
  no atan2, no quaternion, no float division. Runs every loop tick and folds
  its output into the wing waveform — the haltere reflex.

  Canonical shared header (ESP8285 spirit ⇄ RP2040 muscle): the same math the
  codebase already shares for waveform mathematics (OrnithopterWaveform.h →
  MotionWaveform.h). One source of truth, no duplicated constants.

  Units contract
  ──────────────
    err    : gyro rate error in raw LSB (target = 0). MPU6050 ±250 dps
             default is 131 LSB/(°/s).
    kp/kd/ki: Q12 fixed-point gains (real gain = value / 4096).
    imax   : integrator accumulator clamp, in the same Q12·LSB units as p.i.
    dtMs   : loop period in ms (uint8_t; 255 ms ceiling).
    return : correction demand in the SAME LSB units as err. The Q12 lives in
             the gains (kp/kd/ki = value/4096) and is shifted back out, so the
             output is a plain rate-demand LSB, ready to map to °/s.

  The integrator deliberately has no dt factor: the brain runs at a fixed
  gate rate (250 Hz → 4 ms), so per-tick accumulation IS per-time. The gains
  absorb the loop-rate; retune them if the gate changes.
*/

#include <cstdint>

struct MesoPid {
    int32_t i;       // integrator accumulator (Q12·LSB)
    int16_t lastErr; // previous rate error (LSB)
    int16_t lastD;   // low-passed derivative (LSB/ms)
};

inline void mesoInit(MesoPid &p) {
    p.i = 0;
    p.lastErr = 0;
    p.lastD = 0;
}

// err: rate error LSB (target = 0). kp/kd/ki: Q12. imax: integrator clamp.
// dtMs: loop period. Returns Q12 correction (rate-demand LSB).
inline int16_t mesoPid(MesoPid &p, int16_t err,
                       int16_t kp, int16_t kd, int16_t ki,
                       int32_t imax, uint8_t dtMs)
{
    if (dtMs == 0) dtMs = 1;

    // Derivative-on-measurement with a one-pole LPF (α ≈ 1/8 per tick → τ ≈ 8·dt).
    // d is in LSB/ms; the low-pass keeps the D term bounded against gyro noise.
    int32_t d  = (int32_t)(err - p.lastErr) / dtMs;
    int32_t lp = (d * 8 + (int32_t)p.lastD * 5);
    p.lastD    = (int16_t)(lp >> 3);

    // Integrator with anti-windup clamp.
    p.i += (int32_t)err * ki;
    if (p.i >  imax) p.i =  imax;
    if (p.i < -imax) p.i = -imax;
    p.lastErr = err;

    return (int16_t)(((int32_t)err * kp + p.i + (int32_t)p.lastD * kd) >> 12);
}