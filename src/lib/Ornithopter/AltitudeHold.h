#pragma once
/*
  AltitudeHold — the no-mind port of Gralha Azul's sustentarAltura().

  A P-controller with a first-order lag on the error and a slew limit on the
  output (the rate limiter acts as the integrator). The sustain channel is a
  0..1 gain knob; above the engage threshold the controller latches the current
  altitude as the setpoint and drives the throttle toward zero error, starting
  from the throttle value it captured at engage.

  Faithful to the reference law (FORCA_BASE=30 µs/m, AMORTECIMENTO=0.1,
  SILENCIO=0.5 m, TAXA=50 µs/cycle, GANHO_LIMIAR=0.1) but re-expressed in the
  PteronautOS CRSF-raw throttle domain (172..1811, centre 992 ≈ 1500 µs) so it
  drops straight into _computeServoMixer/_computeGearboxMixer's throttleUsF.

  No Arduino, no heap, no dynamic allocation — unit-testable in isolation.
*/

#include <cstdint>

// ─── Cross-module altitude bridge ──────────────────────────────────
// Written by whichever baro source is alive (direct-I2C devBaro, or the MUSHIN
// companion TELEMETRY frame), read by the Ornithopter mixer each tick. Inline
// variables (C++17) keep this a header-only, dependency-free seam.
namespace PteroAltitude {
    inline float currentM = 0.0f;   // relative altitude, metres (0 = launch home)
    inline bool  valid    = false;  // false until a baro has produced a reading

    inline void set(float meters, bool ok)
    {
        currentM = meters;
        valid    = ok;
    }
}

inline float altHoldClamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

struct AltitudeHoldParams
{
    bool    enabled        = false;  // master enable (auto-gated on baro validity)
    float   gain           = 40.0f;  // CRSF-raw throttle per metre of error
    float   errorFilter    = 0.10f;  // low-pass α on the altitude error (AMORTECIMENTO)
    float   deadbandM      = 0.5f;   // ±m deadband where error is forced to 0 (SILENCIO)
    float   maxRate        = 20.0f;  // CRSF-raw slew limit per mixer tick (TAXA)
    float   minRaw         = 172.0f; // output floor (SOPRO_MIN)
    float   maxRaw         = 1811.0f;// output ceiling (SOPRO_MAX)
    float   engageThreshold = 0.10f; // sustain-gain below which it releases (GANHO_LIMIAR)
    float   maxAltitudeM   = 20.0f;  // |setpoint| clamp (ALTURA_MAX)
    uint8_t channel        = 8;      // CRSF channel index (0-based) for the sustain knob
};

struct AltitudeHoldState
{
    bool  active        = false;
    float desiredM      = 0.0f;
    float filteredError = 0.0f;
    float outputRaw     = 992.0f;    // CRSF-raw throttle centre
};

// One control-law step. Returns the effective throttle (CRSF raw). When
// disengaged it passes the raw throttle through untouched, so the stick keeps
// full authority the moment the sustain knob drops below the threshold.
inline float altitudeHoldStep(const AltitudeHoldParams &p, AltitudeHoldState &s,
                              float currentAltM, bool altValid,
                              float engageChannelRaw, float rawThrottle)
{
    const float gain01 = altHoldClamp01((engageChannelRaw - 172.0f) / (1811.0f - 172.0f));

    if (!p.enabled || !altValid || gain01 <= p.engageThreshold)
    {
        s.active = false;
        s.outputRaw = rawThrottle;
        return rawThrottle;
    }

    if (!s.active)
    {
        s.active = true;
        s.desiredM = currentAltM;
        s.filteredError = 0.0f;
        s.outputRaw = rawThrottle;   // engage from the pilot's current throttle
    }

    float desired = s.desiredM;
    if (desired >  p.maxAltitudeM) desired =  p.maxAltitudeM;
    if (desired < -p.maxAltitudeM) desired = -p.maxAltitudeM;

    float err = desired - currentAltM;
    if (err > -p.deadbandM && err < p.deadbandM) err = 0.0f;   // deadband

    s.filteredError += p.errorFilter * (err - s.filteredError);

    float target = 992.0f + s.filteredError * p.gain * gain01;
    if (target < p.minRaw) target = p.minRaw;
    if (target > p.maxRaw) target = p.maxRaw;

    float delta = target - s.outputRaw;
    if (delta >  p.maxRate) delta =  p.maxRate;
    if (delta < -p.maxRate) delta = -p.maxRate;
    s.outputRaw += delta;
    if (s.outputRaw < p.minRaw) s.outputRaw = p.minRaw;
    if (s.outputRaw > p.maxRaw) s.outputRaw = p.maxRaw;

    return s.outputRaw;
}
