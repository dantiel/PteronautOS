#include "Ornithopter.h"

// ─────────────────────────────────────────────────────────────────────
//  FLIGHT PROFILES — three tuning sets selectable via the profile channel
// ─────────────────────────────────────────────────────────────────────

void Ornithopter::_loadDefaultFlightProfiles()
{
    // Three distinct tuning sets (index 0..2). Aggregate init; the trailing
    // fields of FlightProfileParams zero-initialize.
    flightProfiles[0] = { 30.0f, 50.0f, -4, 0, 40.0f, 60.0f, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    flightProfiles[1] = { 50.0f, 50.0f, -4, 0, 40.0f, 60.0f, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    flightProfiles[2] = { 70.0f, 50.0f,  2, 0, 40.0f, 60.0f, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
}

void Ornithopter::applyFlightProfile(uint8_t idx)
{
    if (idx >= FLIGHT_PROFILE_COUNT) idx = 1;
    activeFlightProfile = idx;
    const FlightProfileParams &p = flightProfiles[idx];
    strokeFerocity      = p.strokeFerocity;
    returnFerocity      = p.returnFerocity;
    glideAngleDeg       = p.glideAngleDeg;
    flappingAngleDeg    = p.flappingAngleDeg;
    aileronScale        = p.aileronScale;
    elevatorScale       = p.elevatorScale;
    rudderFerocityRange = p.rudderFerocityRange;
    rudderAmplitudeDifferential = p.rudderAmplitudeDifferential;
    elevatorFerocityMix = p.elevatorFerocityMix;
    throttleFrequencyMix = p.throttleFrequencyMix;
    ferocityShapeMix = p.ferocityShapeMix;
    strokeSkew        = p.strokeSkew;
    returnSkew        = p.returnSkew;
    throttleThrustShapeMix = p.throttleThrustShapeMix;
    throttleThrustExpo    = p.throttleThrustExpo;
    aileronSkewMix      = p.aileronSkewMix;
    throttleSkewRateMix = p.throttleSkewRateMix;
    aileronSkewRateMix = p.aileronSkewRateMix;
    elevatorFerocityRateMix = p.elevatorFerocityRateMix;
    elevatorAntigravityMix  = p.elevatorAntigravityMix;
}

void Ornithopter::setFlightProfileParams(uint8_t idx, float sf, float rf,
                                         int8_t glide, int8_t flapAng,
                                         float ail, float elev, float rudRng,
                                         float rudAmpDiff, float elevFerMix,
                                         float thrThrustExpo, float thrThrustShape,
                                         float thrFreqMix,
                                         float ferShapeMix,
                                         float strokeSkew, float returnSkew,
                                         float ailSkewMix,
                                         float thrSkewRateMix, float ailSkewRateMix,
                                         float elevFerRateMix, float elevAntiGravMix)
{
    if (idx >= FLIGHT_PROFILE_COUNT) idx = 1;
    FlightProfileParams &p = flightProfiles[idx];
    p.strokeFerocity      = sf;
    p.returnFerocity      = rf;
    p.glideAngleDeg       = glide;
    p.flappingAngleDeg    = flapAng;
    p.aileronScale        = ail;
    p.elevatorScale       = elev;
    p.rudderFerocityRange = rudRng;
    p.rudderAmplitudeDifferential = rudAmpDiff;
    p.elevatorFerocityMix = elevFerMix;
    p.throttleThrustShapeMix = thrThrustShape;
    p.throttleThrustExpo  = thrThrustExpo;
    p.throttleFrequencyMix = thrFreqMix;
    p.ferocityShapeMix = ferShapeMix;
    p.strokeSkew       = strokeSkew;
    p.returnSkew       = returnSkew;
    p.aileronSkewMix      = ailSkewMix;
    p.throttleSkewRateMix = thrSkewRateMix;
    p.aileronSkewRateMix = ailSkewRateMix;
    p.elevatorFerocityRateMix = elevFerRateMix;
    p.elevatorAntigravityMix  = elevAntiGravMix;
    if (idx == activeFlightProfile) applyFlightProfile(idx);
}
