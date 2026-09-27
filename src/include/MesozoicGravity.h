#pragma once
/*
  MesozoicGravity — flap-decoupled gravity recovery (PteronautOS MESOZOIC_HORIZON T1)

  The accelerometer on a flapping body is saturated with beat-frequency
  self-acceleration. Two physical sources, both flap-coherent, both removable
  by a 4-mixer orthogonal lock-in bank:

    · inertial reaction  ∝ wing angular accel  ∝ sin(θ)  →  FUNDAMENTAL
    · aerodynamic force  ∝ wing velocity²      ∝ cos²(θ) →  DC + 2nd HARMONIC

  So the bank {sinθ, cosθ, sin2θ, cos2θ} spans both. sin2θ/cos2θ are DERIVED
  (sin2θ = 2·sinθ·cosθ, cos2θ = cos²θ − sin²θ) — no extra trig. The ESPELHO
  integrators (τ = MESO_GRAV_LOCK_TAU) learn the flap-coherent self-image and
  subtract it, leaving gravity + true body motion. A slow low-pass
  (τ = MESO_GRAV_TAU, ~1 Hz) then rejects the residual (vortex-shedding
  impulses, higher harmonics, body translation) far below the flap band.

  This is the SAME 4-mixer lock-in math committed to OrniFlight ESPELHO
  (applyEspelho), ×2 for the A/2 mixer-DC, phase-true, skew-aware.

  Canonical shared header (ESP8285 spirit ⇄ RP2040 muscle), like Mesozoic.h.
  Header-only, integer-free float core, <cmath> only — natively testable.
*/

#include <cmath>

// ESPELHO lock-in bank — learns the flap-coherent image of ONE accel axis.
struct MesoEspelho {
    float s1;  // DC of sample·sinθ  → in-phase fundamental (A1/2)
    float c1;  // DC of sample·cosθ  → quadrature fundamental (B1/2)
    float s2;  // DC of sample·sin2θ → in-phase 2nd harmonic (A2/2) [skew]
    float c2;  // DC of sample·cos2θ → quadrature 2nd harmonic (B2/2) [skew]
};

inline void mesoEspelhoInit(MesoEspelho &e) {
    e.s1 = e.c1 = e.s2 = e.c2 = 0.0f;
}

// One lock-in fold. References are mutually orthogonal, so each integrator
// isolates a single coherent component. Returns the full flap-coherent
// self-image (amplitude + phase, fundamental + skew) to SUBTRACT.
inline float mesoEspelhoStep(MesoEspelho &e, float sample,
                             float sinT, float cosT,
                             float dt, float tau)
{
    const float sin2 = 2.0f * sinT * cosT;             // derived, no trig
    const float cos2 = cosT * cosT - sinT * sinT;      // derived, no trig
    const float alpha = dt / (tau + dt);

    e.s1 += (sample * sinT - e.s1) * alpha;
    e.c1 += (sample * cosT - e.c1) * alpha;
    e.s2 += (sample * sin2 - e.s2) * alpha;
    e.c2 += (sample * cos2 - e.c2) * alpha;

    // ×2 corrects the A/2 mixer DC; full amplitude, phase-true.
    return 2.0f * (e.s1 * sinT + e.c1 * cosT + e.s2 * sin2 + e.c2 * cos2);
}

// Skew ratio |2nd harmonic| / |fundamental| — stroke asymmetry diagnostic
// (mirrors OrniFlight espelhoSkewRatio). Guard against divide-by-zero.
inline float mesoEspelhoSkew(const MesoEspelho &e) {
    const float fund = sqrtf(e.s1 * e.s1 + e.c1 * e.c1);
    const float harm = sqrtf(e.s2 * e.s2 + e.c2 * e.c2);
    if (fund < 1e-6f) return 0.0f;
    return harm / fund;
}

// Flap-decoupled gravity recovery state (three banks + recovered vector).
struct MesoGravity {
    MesoEspelho bank[3];  // X, Y, Z
    float grav[3];        // recovered gravity (g), ~1 Hz bandwidth
    bool  seeded;
};

inline void mesoGravityInit(MesoGravity &g) {
    for (int i = 0; i < 3; ++i) { mesoEspelhoInit(g.bank[i]); g.grav[i] = 0.0f; }
    g.seeded = false;
}

// Recover the clean gravity vector. ax/ay/az are accel in g, already
// board-rotated + level-referenced. sinT/cosT = flap phase. tauLock drives
// the ESPELHO lock-in, tauGrav the gravity low-pass.
inline void mesoGravityUpdate(MesoGravity &g,
                              float ax, float ay, float az,
                              float sinT, float cosT,
                              float dt, float tauLock, float tauGrav)
{
    // 1. flap-coherent cancellation, continuous (fundamental + skew)
    const float clean[3] = {
        ax - mesoEspelhoStep(g.bank[0], ax, sinT, cosT, dt, tauLock),
        ay - mesoEspelhoStep(g.bank[1], ay, sinT, cosT, dt, tauLock),
        az - mesoEspelhoStep(g.bank[2], az, sinT, cosT, dt, tauLock),
    };

    // 2. slow low-pass → gravity. Seed directly, then converge.
    if (!g.seeded) {
        g.grav[0] = clean[0];
        g.grav[1] = clean[1];
        g.grav[2] = clean[2];
        g.seeded = true;
        return;
    }
    const float alpha = dt / (tauGrav + dt);
    for (int i = 0; i < 3; ++i) g.grav[i] += (clean[i] - g.grav[i]) * alpha;
}
