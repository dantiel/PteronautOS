#pragma once
/*
  MesozoicAttitude — slow complementary attitude (PteronautOS MESOZOIC_HORIZON T2)

  Consumes the flap-decoupled gravity vector (MesozoicGravity, T1) and the raw
  gyro rate to produce a roll/pitch attitude estimate that does NOT oscillate
  at beat frequency.

  · predict — integrate gyro rate (fast, drift-free over the short term)
  · measure — gravity → Euler roll/pitch (slow, drift-free over the long term)
  · blend  — complementary correction with τ = MESO_ATT_TAU (~0.3 Hz)

  The small-angle approximation rollDot ≈ gx, pitchDot ≈ gy holds because
  HORIZON keeps the body near level (that is the mode's entire purpose); the
  full Euler-rate Jacobian would only add tan/sin terms that vanish at level.

  Header-only, natively testable (same contract as Mesozoic.h / MesozoicGravity.h).
*/

#include <cmath>

struct MesoAttitude {
    float roll;   // deg
    float pitch;  // deg
};

inline void mesoAttitudeInit(MesoAttitude &a) {
    a.roll = 0.0f;
    a.pitch = 0.0f;
}

// gx/gy = gyro rates (°/s, aircraft frame). gravX/Y/Z = clean gravity (g).
// tau = complementary correction time constant (s).
inline void mesoAttitudeUpdate(MesoAttitude &a,
                               float gx, float gy,
                               float gravX, float gravY, float gravZ,
                               float dt, float tau)
{
    // predict — integrate gyro (small-angle: rollDot≈gx, pitchDot≈gy)
    a.roll  += gx * dt;
    a.pitch += gy * dt;

    // measure — gravity → Euler, guarded against a null/absent gravity vector
    const float norm = sqrtf(gravX * gravX + gravY * gravY + gravZ * gravZ);
    if (norm < 0.05f) return;   // no valid gravity — trust the gyro

    const float r = atan2f(gravY, gravZ) * 57.29578f;
    const float p = atan2f(-gravX, sqrtf(gravY * gravY + gravZ * gravZ)) * 57.29578f;

    // blend — complementary correction (slow; rejects residual flap content)
    const float alpha = dt / (tau + dt);
    a.roll  += (r - a.roll)  * alpha;
    a.pitch += (p - a.pitch) * alpha;
}

// Attitude outer loop (T3): attitude error (deg) → rate setpoint (°/s).
// Pure P — the inner mesoPid already carries the rate loop's I/D terms, so
// adding an outer integrator would fight it. Slew-clamped so a hard stick
// throw commands a bounded rate. Header-only so the cascade is natively
// testable against a synthetic attitude error.
inline float mesoAttitudeRateSetpoint(float attErrDeg, float kp, float maxRateDps) {
    float sp = attErrDeg * kp;
    if (sp >  maxRateDps) sp =  maxRateDps;
    if (sp < -maxRateDps) sp = -maxRateDps;
    return sp;
}

// HORIZON blend (T3): self-level strength fades with |stick|, the direct rate
// command fades in. Stick centre → pure level hold (attErr → rate setpoint);
// full deflection → pure rate command (stick·maxRate). Classic Betaflight
// HORIZON — self-leveling when you let go, rate authority when you push.
inline float mesoHorizonRateSetpoint(float attErrDeg, float stickNorm,
                                     float kp, float maxRateDps) {
    const float level = mesoAttitudeRateSetpoint(attErrDeg, kp, maxRateDps);
    const float hz = 1.0f - fabsf(stickNorm);
    return level * hz + stickNorm * maxRateDps;
}