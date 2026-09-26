#include "Ornithopter.h"
#ifdef UNIT_TEST
  #include <stdint.h>
  static inline uint32_t micros() { return 0; }
  static inline void delay(unsigned long) {}
#else
  #include <Arduino.h>
#endif
#include <algorithm>

Ornithopter ornithopter;

// Runtime-active mixer profile (defaults to MIXER_PROFILE build flag).
// Changed at runtime via the WebUI; see OrnithopterConfig.h.
MixerProfile activeProfile = (MixerProfile)MIXER_PROFILE;

void setOrnithopterProfile(uint8_t p)
{
    if (p < PROFILE_COUNT && p != (uint8_t)activeProfile) {
        activeProfile = (MixerProfile)p;
        ornithopter.enterFailsafe();  // recenter servos for the new profile
    }
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

Ornithopter::Ornithopter()
  : enabled(true)
  , linkUp(false)
  , stickOverride(false)
  , voiceAileron(992), voiceElevator(992)
  , voiceThrottle(172), voiceRudder(992)
  , voiceArm(172)
  , voiceFreq(992), voiceProfile(992)
  , activeFlightProfile(1)
  , lastThrottlePct(0.0f)
  , lastFlapHz(0.0f)
  , lastStrokeFer(0.0f)
  , lastReturnFer(0.0f)
  , lastStrokeSkew(0.0f)
  , lastReturnSkew(0.0f)
  , lastFlapping(false)
  , strokeFerocity(50.0f)
  , returnFerocity(50.0f)
  , glideAngleDeg(ORNI_GLIDE_ANGLE_DEG_DEFAULT)
  , flappingAngleDeg(ORNI_FLAP_ANGLE_DEG_DEFAULT)
  , aileronScale(67.0f)
  , elevatorScale(100.0f)
  , servoSpeed(ORNI_SERVO_SPEED_MS_DEFAULT)
  , flapBaseFreq(ORNI_FLAP_BASE_FREQ_DHZ_DEFAULT)
  , servoMinUs(ORNI_SERVO_MIN_US)
  , servoMaxUs(ORNI_SERVO_MAX_US)
  , rudderYawWeight(65.0f)
  , rudderRollWeight(35.0f)
  , rudderFerocityRange(50.0f)
  , rudderAmplitudeDifferential(0.0f)
  , elevatorFerocityMix(0.0f)
  , throttleFrequencyMix(0.0f)
  , ferocityShapeMix(0.0f)
  , strokeSkew(ORNI_SKEW_DEFAULT)
  , returnSkew(ORNI_SKEW_DEFAULT)
  , throttleThrustShapeMix(0.0f)
  , throttleThrustExpo(0.0f)
  , aileronSkewMix(0.0f)
  , throttleSkewRateMix(0.0f)
  , aileronSkewRateMix(0.0f)
  , elevatorFerocityRateMix(0.0f)
  , elevatorAntigravityMix(0.0f)
  , elevonScale(50.0f)
  , motorMinUs(ORNI_SERVO_MIN_US)
  , motorMaxUs(ORNI_SERVO_MAX_US)
    , glideMode(false)
    , benchMode(false)
    , hallSensorPin(12)
  , ratchetThrottlePct(15)
  , ratchetTimeoutMs(500)
#ifdef ZEPHYRUS_ENABLED
  , gyroRudderCorrection(0.0f)
  , gyroAileronCorrection(0.0f)
  , gyroElevatorCorrection(0.0f)
  , gyroRollCorrection(0.0f), gyroYawCorrection(0.0f), gyroPitchCorrection(0.0f)
  , wingRollGain(ORNI_WING_ROLL_GAIN), wingPitchGain(ORNI_WING_PITCH_GAIN)
  , wingYawGain(ORNI_WING_YAW_GAIN)
  , gyroPitchPTerm(0.0f), gyroPitchITerm(0.0f)
  , gyroPitchDTerm(0.0f), gyroPitchErrorRate(0.0f)
  , cadenceGain(ORNI_CADENCE_GAIN), ferocityDGain(ORNI_FEROCITY_D_GAIN)
  , balanceGain(ORNI_BALANCE_GAIN)
  , ferocityPGain(ORNI_FEROCITY_P_GAIN), ferocityRollGain(ORNI_FEROCITY_ROLL_GAIN)
  , ferocityYawGain(ORNI_FEROCITY_YAW_GAIN)
  , warpGain(ORNI_WARP_GAIN), warpYawGain(ORNI_WARP_YAW_GAIN)
  , anchorGain(ORNI_ANCHOR_GAIN), resonanceGain(ORNI_RESONANCE_GAIN)
  , ssffGain(ORNI_SSFF_GAIN)
  , aeroGlideCoeff(ORNI_AERO_GLIDE_COEFF), aeroFlapCoeff(ORNI_AERO_FLAP_COEFF)
  , aeroGainScale(1.0f)
  , _resonanceAccum(0.0f)
  , _prevFlappingSin(0.0f), _ssffAccumError(0.0f)
  , _ssffAccumCount(0)
  , _ssffFerocityUpBias(0.0f), _ssffFerocityDownBias(0.0f)
  , _ferHold(0.0f), _ferHoldVel(0.0f)
 #endif
  , _lastUpdateUs(0)
  , _armedState(false)
  , _prevThrottlePct(-1.0f)
  , _throttleRateLPF(0.0f)
  , _prevAileronNorm(-2.0f)
  , _aileronRateLPF(0.0f)
  , _prevElevatorNorm(-2.0f)
  , _elevatorRateLPF(0.0f)
  , _elevFerStroke(0.0f)
  , _elevFerReturn(0.0f)
  , _antiGravGate(0.0f)
{
    modelName[0] = '\0';
    for (uint8_t i = 0; i < STK_COUNT; ++i) stickChannels[i] = 992; // center (CRSF neutral)
        stickChannels[STK_THROTTLE] = 172;  // glide (below flap threshold) so channel test starts at rest
    stickChannels[STK_ARM]      = 1811; // armed (CRSF full)
    stickChannels[STK_FREQ]     = 1500; // mid flap frequency
    stickChannels[STK_PROFILE]  = 992;  // profile selector mid
    for (uint8_t i = 0; i < SF_COUNT; ++i) { _f[i] = ORNI_SERVO_CENTER_US; servoTrimUs[i] = ORNI_SERVO_TRIM_US; }
    if (PROFILE_IS_GEARBOX) {
        _f[SF_MOTOR] = ORNI_SERVO_MIN_US;  // motor stopped
    }
    // Flight profile defaults — three distinct tuning sets
    flightProfiles[0] = { 30.0f, 50.0f, -4, 0, 40.0f, 60.0f, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    flightProfiles[1] = { 50.0f, 50.0f, -4, 0, 40.0f, 60.0f, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    flightProfiles[2] = { 70.0f, 50.0f,  2, 0, 40.0f, 60.0f, 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
}

void Ornithopter::onLinkUp() {
    linkUp = true;
    _prevThrottlePct = -1.0f;   // seed on next flap tick — no stale slew kick
    _throttleRateLPF = 0.0f;
    _prevAileronNorm = -2.0f;   // seed on next flap tick — no stale roll slew kick
    _aileronRateLPF = 0.0f;
    _prevElevatorNorm = -2.0f;  // seed on next flap tick — no stale fer dwell kick
    _elevatorRateLPF = 0.0f;
    _elevFerStroke = 0.0f;
    _elevFerReturn = 0.0f;
    _antiGravGate = 0.0f;
#ifdef ZEPHYRUS_ENABLED
    // Reset SSFF state on arm — fresh biases for each flight
    _prevFlappingSin = 0.0f;
    _ssffAccumError = 0.0f;
    _ssffAccumCount = 0;
    _ssffFerocityUpBias = 0.0f;
    _ssffFerocityDownBias = 0.0f;
    _ferHold = 0.0f;
    _ferHoldVel = 0.0f;
#endif
}

void Ornithopter::onLinkDown() { linkUp = false; enterFailsafe(); }

void Ornithopter::enterFailsafe() {
    _armedState = false;  // disarm — require fresh throttle-zero + arm cycle
    for (uint8_t i = 0; i < SF_COUNT; ++i) _f[i] = ORNI_SERVO_CENTER_US;
    if (PROFILE_IS_GEARBOX) {
        _f[SF_MOTOR] = ORNI_SERVO_MIN_US;
    } else {
        _osc.reset();
        _prevElevatorNorm = -2.0f;  // fresh seed on next flap tick
        _elevatorRateLPF = 0.0f;
        _elevFerStroke = 0.0f;
        _elevFerReturn = 0.0f;
        _antiGravGate = 0.0f;
    }
#ifdef ZEPHYRUS_ENABLED
    _prevFlappingSin = 0.0f;
    _ssffAccumError = 0.0f;
    _ssffAccumCount = 0;
    _ssffFerocityUpBias = 0.0f;
    _ssffFerocityDownBias = 0.0f;
    _ferHold = 0.0f;
    _ferHoldVel = 0.0f;
#endif
}

// ─── Helpers ───────────────────────────────────────────────────────
float Ornithopter::_crsfToFloat(uint16_t raw, float outMin, float outMax) {
    float t = (float)(raw - 172) / (float)(1811 - 172);
    return outMin + t * (outMax - outMin);
}

float Ornithopter::_crsfToNorm(uint16_t raw) {
    return _crsfToFloat(raw, -1.0f, 1.0f);
}

void Ornithopter::_readChannels() {
    if (stickOverride) {
        voiceAileron  = stickChannels[STK_AILERON];
        voiceElevator = stickChannels[STK_ELEVATOR];
        voiceThrottle = stickChannels[STK_THROTTLE];
        voiceRudder   = stickChannels[STK_RUDDER];
        voiceArm      = stickChannels[STK_ARM];
        voiceFreq     = stickChannels[STK_FREQ];
        voiceProfile  = stickChannels[STK_PROFILE];
    } else {
        voiceAileron  = ChannelData[ORNI_CH_AILERON];
        voiceElevator = ChannelData[ORNI_CH_ELEVATOR];
        voiceThrottle = ChannelData[ORNI_CH_THROTTLE];
        voiceRudder   = ChannelData[ORNI_CH_RUDDER];
        voiceArm      = ChannelData[ORNI_CH_ARM];
        voiceFreq     = ChannelData[ORNI_CH_FREQ];
        voiceProfile  = ChannelData[ORNI_CH_PROFILE];
    }

    // Flight profile selector (multi-position): map CRSF raw → 0..2.
    uint8_t prof = 1;
    if (voiceProfile < 660)       prof = 0;
    else if (voiceProfile < 1500) prof = 1;
    else                          prof = 2;
    if (prof != activeFlightProfile) applyFlightProfile(prof);
}

// ─── Arming latch ──────────────────────────────────────────────────
// Requires the arm switch high AND throttle at zero (below the pre-arm
// threshold). Once latched, throttle may rise to flap; dropping the arm
// switch (or link loss / failsafe) clears the latch.
bool Ornithopter::_isArmed() {
    if (voiceArm <= 992) {
        _armedState = false;                 // disarm on switch low
    } else if (!_armedState) {
        _armedState = (voiceThrottle < ORNI_ARM_THROTTLE_ZERO_US);  // arm only at zero throttle
    }
    return _armedState;
}

uint16_t Ornithopter::_clampServo(int32_t us) {
    if (us < ORNI_SERVO_ABS_MIN_US) return ORNI_SERVO_ABS_MIN_US;
    if (us > ORNI_SERVO_ABS_MAX_US) return ORNI_SERVO_ABS_MAX_US;
    return (uint16_t)us;
}

// ═══════════════════════════════════════════════════════════════════
//  SERVO KERNEL — waveform-driven flapping wings
// ═══════════════════════════════════════════════════════════════════
void Ornithopter::_computeServoMixer() {
    float aileronNorm  = _crsfToNorm(voiceAileron);
    float elevatorNorm = _crsfToNorm(voiceElevator);
    float throttleUsF  = (float)voiceThrottle;
    bool armed = _isArmed();

    static bool wasFlapping = false;
    bool isFlapping;
    if (wasFlapping) {
        isFlapping = armed && (throttleUsF > (float)(ORNI_FLAP_THRESHOLD_US - ORNI_FLAP_HYSTERESIS_US));
    } else {
        isFlapping = armed && (throttleUsF > (float)ORNI_FLAP_THRESHOLD_US);
    }
    wasFlapping = isFlapping;

#ifdef ZEPHYRUS_ENABLED
    // Aeroelastic PID gain modulation: scale Zephyrus pitch response
    // by glide/flap coefficient based on flapping state
    aeroGainScale = isFlapping ? (aeroFlapCoeff * 0.01f) : (aeroGlideCoeff * 0.01f);
#endif

    // Steering magnitudes scaled by runtime WebUI params (aileron_scale /
    // elevator_scale, 0–100). Sign application happens below — the mirror
    // mount swaps the code axes (see angle formula).
    float aileronCmd  = aileronNorm * aileronScale * 0.01f * ORNI_STEER_MAX_DEG;
    float elevatorCmd = elevatorNorm * elevatorScale * 0.01f * ORNI_STEER_MAX_DEG;
        float glideCmd    = (float)glideAngleDeg;   // static wing angle (glide only)
        float flapCenterCmd = (float)flappingAngleDeg; // flap stroke centre offset

    // Mesozoic 2-wing stabilizer centre terms — declared unconditionally so
    // the angle formulas below need no #ifdef. Zero when Zephyrus is compiled
    // out (ZEPHYRUS_ENABLED undefined) or the gains are 0.
    float gyroPitchCenter = 0.0f;   // symmetric flap centre shift (deg)
    float gyroRollCenter  = 0.0f;   // glide-only roll self-level centre (deg)
#ifdef ZEPHYRUS_ENABLED
    // Pitch stabilizer: gyro pitch correction → symmetric flap CENTRE shift
    // (both wings bias the same physical direction). Rides the elevator axis
    // (elevatorCmd above), so a nose-up error commands a nose-down bias.
#ifdef MESOZOIC_ONLY
    gyroPitchCenter = gyroPitchCorrection * (wingPitchGain * 0.01f) * ZEPHYR_WING_PITCH_RATE_CENTER_SCALE;
    if (gyroPitchCenter >  ZEPHYR_WING_PITCH_RATE_CENTER_CLAMP) gyroPitchCenter =  ZEPHYR_WING_PITCH_RATE_CENTER_CLAMP;
    if (gyroPitchCenter < -ZEPHYR_WING_PITCH_RATE_CENTER_CLAMP) gyroPitchCenter = -ZEPHYR_WING_PITCH_RATE_CENTER_CLAMP;
#else
    gyroPitchCenter = gyroPitchCorrection * (wingPitchGain * 0.01f) * ZEPHYR_WING_PITCH_CENTER_SCALE;
    if (gyroPitchCenter >  ZEPHYR_WING_PITCH_CENTER_CLAMP) gyroPitchCenter =  ZEPHYR_WING_PITCH_CENTER_CLAMP;
    if (gyroPitchCenter < -ZEPHYR_WING_PITCH_CENTER_CLAMP) gyroPitchCenter = -ZEPHYR_WING_PITCH_CENTER_CLAMP;
#endif

    // Roll self-level (glide only — in flap roll rides the amplitude axis).
    // Maps roll correction to a symmetric aileron-centre offset so the wings
    // stay level while gliding.
#ifdef MESOZOIC_ONLY
    gyroRollCenter = gyroRollCorrection * (wingRollGain * 0.01f) * ZEPHYR_WING_ROLL_RATE_CENTER_SCALE;
    if (gyroRollCenter >  ZEPHYR_WING_ROLL_RATE_CENTER_CLAMP) gyroRollCenter =  ZEPHYR_WING_ROLL_RATE_CENTER_CLAMP;
    if (gyroRollCenter < -ZEPHYR_WING_ROLL_RATE_CENTER_CLAMP) gyroRollCenter = -ZEPHYR_WING_ROLL_RATE_CENTER_CLAMP;
#else
    gyroRollCenter = gyroRollCorrection * (wingRollGain * 0.01f) * ZEPHYR_WING_ROLL_CENTER_SCALE;
    if (gyroRollCenter >  ZEPHYR_WING_ROLL_CENTER_CLAMP) gyroRollCenter =  ZEPHYR_WING_ROLL_CENTER_CLAMP;
    if (gyroRollCenter < -ZEPHYR_WING_ROLL_CENTER_CLAMP) gyroRollCenter = -ZEPHYR_WING_ROLL_CENTER_CLAMP;
#endif
#endif

    int angleLeft, angleRight;

    if (isFlapping) {
        // At 0% coupling CH6 independently commands frequency. At 100%, the
        // normalized frequency command follows normalized throttle, so
        // frequency and amplitude scale together. Intermediate profile values
        // are a continuous blend. Both sources are CRSF raw (172–1811).
        float independentFreq01 = _crsfToNorm(voiceFreq) * 0.5f + 0.5f;
        float throttlePct = constrain((throttleUsF - (float)ORNI_FLAP_THRESHOLD_US) /
                                      (1811.0f - (float)ORNI_FLAP_THRESHOLD_US), 0.0f, 1.0f);
        float freq01 = orniThrottleFrequencyCommand(independentFreq01,
                                                    throttlePct,
                                                    throttleFrequencyMix);
        float freqMax = flapBaseFreq * 0.1f;                   // deci-Hz → Hz
        float freqHz = ORNI_FREQ_MIN + freq01 * (freqMax - ORNI_FREQ_MIN);
        _osc.cadenceTarget = freqHz * 6.283185307f;            // 2π rad/s

        uint32_t nowUs = micros();
        if (_lastUpdateUs == 0) _lastUpdateUs = nowUs;
        float dt = (float)(nowUs - _lastUpdateUs) * 1e-6f;
        if (dt > 0.1f) dt = 0.1f;
        _lastUpdateUs = nowUs;

#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
        // ── Real Ondas wiring (Nigredo) ──────────────────────────────
        // Zephyrus bridges the raw pitch PID terms at 250 Hz
        // (ZephyrusFilter.h) and NaN-guards them upstream (Validatio),
        // so the values consumed here are live and bounded.
        // Cadence P → Phase Advance: nose-up asks for a brief faster flap,
        // nose-down a brief slower one (clamped to [0.5, 2.0]). The demand
        // feeds the phase-quantized Josephson washboard in advance(): a weak
        // demand bends the phase and rings back to the same beat, a strong one
        // slips a WHOLE stroke — the flap always lands on a beat, never between.
        _osc.kGainMod = 1.0f + gyroPitchPTerm * aeroGainScale * cadenceGain * 0.00005f;
        if (_osc.kGainMod < 0.5f) _osc.kGainMod = 0.5f;
        if (_osc.kGainMod > 2.0f) _osc.kGainMod = 2.0f;
#endif

#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
        _osc.anchorGain = anchorGain;
#else
        _osc.anchorGain = 0.0f;
#endif

#if !defined(MUSHIN_ENABLED)
        float rawWave = _osc.advance(dt);
#endif

#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
        // Resonance uses the oscillator fundamental. SSFF below must instead
        // use the shaped wave's asymmetric reversal boundary.
        float waveSin = sinf(rawWave);
        if (ssffGain <= 0.0f) {
            // Purificatio: zero stale biases when SSFF is disabled
            _ssffFerocityUpBias   = 0.0f;
            _ssffFerocityDownBias = 0.0f;
        }
#endif

        // Amplitude = throttle % of the servo-speed-limited max at this freq.
        float degPerSec = 60.0f / (servoSpeed * 0.001f);       // ms/60° → °/s
        float ampMax = degPerSec / (2.0f * freqHz);
                if (ampMax > ORNI_AMP_MAX) ampMax = ORNI_AMP_MAX;            // hard safety ceiling
        float amplitude = throttlePct * ampMax;

        // Elevator → ferocity: ASYMMETRIC stroke modulation.
        // Elevator-up (norm +1) strengthens the DOWNSTROKE (power stroke)
        // ferocity; elevator-down (norm -1) strengthens the UPSTROKE
        // (recovery) ferocity. Each direction boosts only one half-stroke.
        // CRSF: elevator up = 1811 (norm +1), down = 172 (norm -1).
        float elevFerScale = elevatorFerocityMix * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN);
        float elevUpBoost   = fmaxf( elevatorNorm, 0.0f) * elevFerScale;   // climb → downstroke
        float elevDownBoost = fmaxf(-elevatorNorm, 0.0f) * elevFerScale;   // dive  → upstroke

        // ── Elevator-RATE → ferocity transient (slew) + antigravity ──
        // The faster the elevator stick moves, the more ferocity dwells in
        // that stroke direction (climb → downstroke, dive → upstroke). The
        // transient DECAYS toward the CURRENT static stick coupling (so a held
        // climb settles on the steady climb ferocity, not neutral). Antigravity
        // is a rate-gated, stick-proportional feed-forward that fights gravity
        // in the stick direction while moving, then fades when the stick rests.
        float elevRateKickUp   = 0.0f;
        float elevRateKickDown = 0.0f;
        float antiGravUp       = 0.0f;
        float antiGravDown     = 0.0f;

        if (_prevElevatorNorm < -1.5f) {
            // First flap tick after glide/link: seed the transient at the static
            // coupling so the pre-existing elevator→ferocity mix applies
            // instantly (no soft-start dip) — only the kicks ride on top.
            _prevElevatorNorm = elevatorNorm;
            _elevFerStroke = elevUpBoost;
            _elevFerReturn = elevDownBoost;
        } else if (dt > 0.0f) {
            float elevRate = (elevatorNorm - _prevElevatorNorm) / dt;
            _prevElevatorNorm = elevatorNorm;
            float alphaRate = dt / (ORNI_ELEV_RATE_LPF_TAU + dt);
            _elevatorRateLPF += (elevRate - _elevatorRateLPF) * alphaRate;

            elevRateKickUp   = orniElevatorRateFerKick( _elevatorRateLPF, elevatorFerocityRateMix);
            elevRateKickDown = orniElevatorRateFerKick(-_elevatorRateLPF, elevatorFerocityRateMix);

            float gate = fabsf(_elevatorRateLPF) * ORNI_ANTIGRAV_GATE_GAIN;
            if (gate > 1.0f) gate = 1.0f;
            float alphaGate = dt / (ORNI_ANTIGRAV_GATE_TAU + dt);
            _antiGravGate += (gate - _antiGravGate) * alphaGate;

            antiGravUp   = orniElevatorAntigravity(_antiGravGate,  elevatorNorm, elevatorAntigravityMix);
            antiGravDown = orniElevatorAntigravity(_antiGravGate, -elevatorNorm, elevatorAntigravityMix);

            // Decaying accumulator toward the current static coupling + kick +
            // antigravity — so the transient relaxes "in the direction of the
            // current stick input". The DELTA over the static is what we add.
            float alphaFer = dt / (ORNI_ELEV_RATE_TAU + dt);
            _elevFerStroke += ((elevUpBoost + elevRateKickUp   + antiGravUp)   - _elevFerStroke) * alphaFer;
            _elevFerReturn += ((elevDownBoost + elevRateKickDown + antiGravDown) - _elevFerReturn) * alphaFer;
        }

        // Transient EXTRA ferocity riding on the static coupling (≈0 at rest).
        float elevFerRateUp   = _elevFerStroke - elevUpBoost;    // downstroke dwell kick
        float elevFerRateDown = _elevFerReturn - elevDownBoost;  // upstroke dwell kick

        // Throttle → thrust-shape coupling (per-profile, 0–100). One blended
        // knob: thrust aggression α = expo(throttle)·mix drives dwell (square)
        // AND centre (front-load) together — the defined overlap of ferocity
        // and skew along the thrust axis. 0 = throttle drives amplitude only;
        // 100 = full dwell + full front-load at full gas. The per-profile expo
        // curves where along the stick that aggression arrives.
        OrniThrustShape thrust = orniThrottleThrustShape(throttlePct, throttleThrustShapeMix,
                                                         throttleThrustExpo);

        // Ferocity (dwell/shape) = per-profile stroke/return sliders
        // + elevator mix + throttle mix (+ gyro).

#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
        // Ferocity PD-blend: P-term + D-term → dwell ratio (clamped ±0.5).
        float ferocitySignal = (gyroPitchPTerm * ferocityPGain * 0.00015f
                              + gyroPitchDTerm * ferocityDGain * 0.0003f) * aeroGainScale;
        if (ferocitySignal > 0.5f) ferocitySignal = 0.5f;
        if (ferocitySignal < -0.5f) ferocitySignal = -0.5f;
        // Inertial harmonizer: the held dwell bias is a damped pendulum
        // (ω₀=10, ζ=0.7) tracking the live PD blend. It catches the demanded
        // ferocity with pendulum momentum and decays back inertially, so the
        // dwell change lands in step with the phase-quantized cadence above
        // rather than snapping the reversal boundary mid-stroke.
        {
            constexpr float fOmega = 10.0f;
            constexpr float fZeta  = 0.7f;
            _ferHoldVel += (-fOmega * fOmega * (_ferHold - ferocitySignal)
                            - 2.0f * fZeta * fOmega * _ferHoldVel) * dt;
            _ferHold += _ferHoldVel * dt;
        }
        ferocitySignal = _ferHold;
        // Balance I → asymmetry: accumulated pitch error shifts the
        // stroke centre (clamped to ±3.0 ferocity units).
        float iBias = gyroPitchITerm * aeroGainScale * balanceGain * 0.0001f;
        if (iBias > 3.0f) iBias = 3.0f;
        if (iBias < -3.0f) iBias = -3.0f;
        // Resonance lock-in pump feeds both half-strokes symmetrically.
        float resonanceBias = _resonanceAccum;

        float strokeFer = ORNI_FEROCITY_MIN + strokeFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN)
                          + ferocitySignal + iBias + _ssffFerocityUpBias + resonanceBias + elevUpBoost + elevFerRateUp + thrust.dwellBoost;
        float returnFer = ORNI_FEROCITY_MIN + returnFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN)
                          + ferocitySignal - iBias + _ssffFerocityDownBias + resonanceBias + elevDownBoost + elevFerRateDown + thrust.dwellBoost;
#else
        float strokeFer = ORNI_FEROCITY_MIN + strokeFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN) + elevUpBoost + elevFerRateUp + thrust.dwellBoost;
        float returnFer = ORNI_FEROCITY_MIN + returnFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN) + elevDownBoost + elevFerRateDown + thrust.dwellBoost;
#endif
        // Yaw stick → L/R wing differential, scaled by rudder_ferocity_range (0–100)
        float rudderFer = _crsfToNorm(voiceRudder) * rudderFerocityRange * 0.01f * ORNI_DIFFERENTIAL_MAX;

        // Yaw stick → L/R differential flap amplitude, scaled by
        // rudder_amplitude_differential (0–100). The aileron roll axis (below)
        // joins this same differential before the stroke scales are computed.
        float rudderAmpDiff = _crsfToNorm(voiceRudder) * rudderAmplitudeDifferential * 0.01f;

        float strokeFerL = strokeFer + rudderFer;
        float strokeFerR = strokeFer - rudderFer;
        float returnFerL = returnFer + rudderFer;
        float returnFerR = returnFer - rudderFer;

#ifdef ZEPHYRUS_ENABLED
        // Mesozoic yaw stabilizer: gyro yaw-rate correction → differential
        // ferocity (asymmetric drag). Rides the same axis as the rudder stick
        // coupling above, so a yaw disturbance drags one wing harder than the
        // other to arrest rotation. 0 gain = axis off.
#ifdef MESOZOIC_ONLY
        float gyroYawFer = gyroYawCorrection * (wingYawGain * 0.01f) * ZEPHYR_WING_YAW_RATE_FER_SCALE;
        if (gyroYawFer >  ZEPHYR_WING_YAW_RATE_FER_CLAMP) gyroYawFer =  ZEPHYR_WING_YAW_RATE_FER_CLAMP;
        if (gyroYawFer < -ZEPHYR_WING_YAW_RATE_FER_CLAMP) gyroYawFer = -ZEPHYR_WING_YAW_RATE_FER_CLAMP;
#else
        float gyroYawFer = gyroYawCorrection * (wingYawGain * 0.01f) * ZEPHYR_WING_YAW_FER_SCALE;
        if (gyroYawFer >  ZEPHYR_WING_YAW_FER_CLAMP) gyroYawFer =  ZEPHYR_WING_YAW_FER_CLAMP;
        if (gyroYawFer < -ZEPHYR_WING_YAW_FER_CLAMP) gyroYawFer = -ZEPHYR_WING_YAW_FER_CLAMP;
#endif
        strokeFerL += gyroYawFer; strokeFerR -= gyroYawFer;
        returnFerL += gyroYawFer; returnFerR -= gyroYawFer;
#endif

        // Shared reversal threshold computed from the BASE ferocities (before
        // rudder differential), so both wings reverse at the SAME phase even
        // when their per-wing ferocities differ. Faithful to GralhaAzul's
        // limiarBase (shared downstroke/upstroke boundary).
        float fDbase = strokeFer; if (fDbase < 0.0f) fDbase = 0.0f; else if (fDbase > 8.0f) fDbase = 8.0f;
        float fSbase = returnFer; if (fSbase < 0.0f) fSbase = 0.0f; else if (fSbase > 8.0f) fSbase = 8.0f;
        float wDbase = 8.0f - fDbase; if (wDbase < 0.01f) wDbase = 0.01f;
        float wSbase = 8.0f - fSbase; if (wSbase < 0.01f) wSbase = 0.01f;
        float limiarShared = 6.283185307f * wDbase / (wDbase + wSbase);

#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
        // Use exactly the boundary consumed by both shapeWave calls, not π.
        // Updated biases take effect on the NEXT mixer tick: do not recompute
        // this tick's boundary from its own feedback event.
        const float strokeSign = rawWave < limiarShared ? 1.0f : -1.0f;
        if (_prevFlappingSin != strokeSign) {
            if (_ssffAccumCount > 0 && ssffGain > 0.0f) {
                float bias = (_ssffAccumError / (float)_ssffAccumCount) * ssffGain * 0.00001f;
                if (bias > 2.0f) bias = 2.0f;
                if (bias < -2.0f) bias = -2.0f;
                if (strokeSign > 0.0f) _ssffFerocityDownBias = bias;
                else                  _ssffFerocityUpBias = bias;
            }
            _ssffAccumError = 0.0f;
            _ssffAccumCount = 0;
        }
        _ssffAccumError += gyroPitchErrorRate;
        _ssffAccumCount++;
        _prevFlappingSin = strokeSign;
#endif

        // Thrust-shape centre (dwell + centre blended above) shifts both
        // half-stroke centres symmetrically; the throttle-rate slew adds
        // its transient on top.

        // Throttle-RATE → transient boost/brake (slew): the low-passed
        // throttle slew briefly shifts BOTH wave centres the same way as the
        // static coupling — giving gas front-loads both half-strokes (boost),
        // cutting gas late-loads both (brake). τ = ORNI_SKEW_RATE_LPF_TAU
        // decays the kick once the stick rests; the sentinel seeds without kick.
        float throttleRateBoost = 0.0f;
        if (_prevThrottlePct < 0.0f) {
            _prevThrottlePct = throttlePct;
        } else if (dt > 0.0f) {
            float throttleRate = (throttlePct - _prevThrottlePct) / dt;
            _prevThrottlePct = throttlePct;
            float alpha = dt / (ORNI_SKEW_RATE_LPF_TAU + dt);
            _throttleRateLPF += (throttleRate - _throttleRateLPF) * alpha;
            throttleRateBoost = orniThrottleSkewRateShift(_throttleRateLPF, throttleSkewRateMix);
        }

        float strokeSkewEff = strokeSkew + thrust.centreShift + throttleRateBoost;
        float returnSkewEff = returnSkew + thrust.centreShift + throttleRateBoost;

        // Aileron → differential flap AMPLITUDE coupling (roll steering).
        // Centre-skew is aerodynamically roll-neutral: +skew and −skew produce
        // the SAME lift impulse (time-reversal symmetry), so their L/R
        // difference is exactly zero — that is why the old differential-skew
        // "roll" produced no torque. Roll needs an impulse differential, so it
        // lives on the amplitude axis (the same one that makes rudder_amplitude_
        // differential work): aileron enlarges one stroke and shrinks the other.
        float aileronRollShift = orniAileronRollShift(aileronNorm, aileronSkewMix);

        // Aileron-RATE → transient differential flap amplitude kick (slew):
        // quick aileron movement briefly enlarges one wing / shrinks the other,
        // a roll-torque kick that decays once the stick rests.
        float aileronRateBoost = 0.0f;
        if (_prevAileronNorm < -1.5f) {
            _prevAileronNorm = aileronNorm;
        } else if (dt > 0.0f) {
            float aileronRate = (aileronNorm - _prevAileronNorm) / dt;
            _prevAileronNorm = aileronNorm;
            float alpha = dt / (ORNI_SKEW_RATE_LPF_TAU + dt);
            _aileronRateLPF += (aileronRate - _aileronRateLPF) * alpha;
            aileronRateBoost = orniAileronRollRateShift(_aileronRateLPF, aileronSkewRateMix);
        }

        // Roll torque = amplitude differential from rudder (yaw) + aileron
        // (static + slew). Clamped so neither wing's stroke collapses to zero.
        float rollAmpDiff = rudderAmpDiff + aileronRollShift + aileronRateBoost;

#ifdef ZEPHYRUS_ENABLED
        // Mesozoic roll stabilizer: gyro roll correction → differential flap
        // amplitude. This is the aerodynamically effective roll axis (a roll
        // perturbation enlarges one stroke and shrinks the other, producing a
        // correcting torque). 0 gain = axis off.
#ifdef MESOZOIC_ONLY
        float gyroRollAmp = gyroRollCorrection * (wingRollGain * 0.01f) * ZEPHYR_WING_ROLL_RATE_AMP_SCALE;
        if (gyroRollAmp >  ZEPHYR_WING_ROLL_RATE_AMP_CLAMP) gyroRollAmp =  ZEPHYR_WING_ROLL_RATE_AMP_CLAMP;
        if (gyroRollAmp < -ZEPHYR_WING_ROLL_RATE_AMP_CLAMP) gyroRollAmp = -ZEPHYR_WING_ROLL_RATE_AMP_CLAMP;
#else
        float gyroRollAmp = gyroRollCorrection * (wingRollGain * 0.01f) * ZEPHYR_WING_ROLL_AMP_SCALE;
        if (gyroRollAmp >  ZEPHYR_WING_ROLL_AMP_CLAMP) gyroRollAmp =  ZEPHYR_WING_ROLL_AMP_CLAMP;
        if (gyroRollAmp < -ZEPHYR_WING_ROLL_AMP_CLAMP) gyroRollAmp = -ZEPHYR_WING_ROLL_AMP_CLAMP;
#endif
        rollAmpDiff += gyroRollAmp;
#endif
        if (rollAmpDiff > 0.9f) rollAmpDiff = 0.9f;
        if (rollAmpDiff < -0.9f) rollAmpDiff = -0.9f;
        float amplitudeL = amplitude * (1.0f + rollAmpDiff);
        float amplitudeR = amplitude * (1.0f - rollAmpDiff);

        // Centre-skew stays symmetric (throttle thrust vector); the roll axis
        // moved to amplitude above, so both wings share the same skew.
#if defined(MUSHIN_ENABLED)
        // Compile the entire pilot-driven motion on EP2, but never evaluate
        // phase or wave samples here. RP2040 owns the only running oscillator.
        Motion::prepare(motionIntent, limiarShared, strokeFerL, returnFerL,
                        strokeFerR, returnFerR, ferocityShapeMix, strokeSkewEff, returnSkewEff);
        motionIntent.flapping = 1;
        motionIntent.hz = freqHz;
        motionIntent.centre[0] = ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd + elevatorCmd + flapCenterCmd + gyroPitchCenter) * ORNI_ANGULAR_MULTIPLIER;
        motionIntent.centre[1] = ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd - elevatorCmd - flapCenterCmd - gyroPitchCenter) * ORNI_ANGULAR_MULTIPLIER;
        motionIntent.amplitude[0] = -amplitudeL * ORNI_ANGULAR_MULTIPLIER;
        motionIntent.amplitude[1] = amplitudeR * ORNI_ANGULAR_MULTIPLIER;
        const float pulseL = 0, pulseR = 0; // local preview centres, not actuator samples
#else
        float pulseL = FlappingOscillator::shapeWave(rawWave, strokeFerL, returnFerL,
                                                     limiarShared, ferocityShapeMix,
                                                     strokeSkewEff, returnSkewEff);
        float pulseR = FlappingOscillator::shapeWave(rawWave, strokeFerR, returnFerR,
                                                     limiarShared, ferocityShapeMix,
                                                     strokeSkewEff, returnSkewEff);

        // ── MUSHIN v1 parameter cache ────────────────────────────────
#endif
        // The spirit streams wave parameters, not servo µs: the muscle
        // reconstructs phase + shapeWave locally per tick. Values are
        // post-mix (L wing) and symmetric (skew before aileron differential).
        lastThrottlePct = throttlePct;
        lastFlapHz = freqHz;
        lastStrokeFer = strokeFerL;
        lastReturnFer = returnFerL;
        lastStrokeSkew = strokeSkewEff;
        lastReturnSkew = returnSkewEff;
        lastFlapping = true;

#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
        // Resonance — phase-locked lock-in amplifier: accumulate
        // errorRate × sin(phase), leaky τ = 0.15 s, clamped to ±2.0.
        if (resonanceGain > 0.0f) {
            _resonanceAccum += gyroPitchErrorRate * waveSin * resonanceGain * 0.01f * dt;
            _resonanceAccum *= expf(-dt / 0.15f);
            if (_resonanceAccum > 2.0f) _resonanceAccum = 2.0f;
            if (_resonanceAccum < -2.0f) _resonanceAccum = -2.0f;
        }
#endif

                float degL = amplitudeL * pulseL;
                float degR = amplitudeR * pulseR;

        // Neutral must NOT be scaled by ORNI_ANGULAR_MULTIPLIER — otherwise
        // 100° × 2 = 200° pins the wings at max deflection and kills all
        // visible flapping. Multiplier applies to deviation terms only.
        // Mirror-mounted wing servos: code "common" == physical differential
        // (roll/aileron), code "differential" == physical common (pitch/flap).
        // Aileron (roll) → common; elevator (pitch) → differential.
                // Glide angle is NOT applied here — it is a glide-only static offset
                // (see non-flapping branch below). The flap centre offset
                // (flappingAngleDeg) IS applied so glide and flap centres are tunable
                // independently per flight profile.
                angleLeft  = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd + elevatorCmd + flapCenterCmd + gyroPitchCenter - degL) * ORNI_ANGULAR_MULTIPLIER);
                angleRight = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd - elevatorCmd - flapCenterCmd - gyroPitchCenter + degR) * ORNI_ANGULAR_MULTIPLIER);
    } else {
#if defined(MUSHIN_ENABLED)
        motionIntent.flapping = 0;
        motionIntent.hz = 0;
        motionIntent.amplitude[0] = motionIntent.amplitude[1] = 0;
        motionIntent.centre[0] = ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd + elevatorCmd + glideCmd + gyroPitchCenter + gyroRollCenter) * ORNI_ANGULAR_MULTIPLIER;
        motionIntent.centre[1] = ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd - elevatorCmd - glideCmd - gyroPitchCenter + gyroRollCenter) * ORNI_ANGULAR_MULTIPLIER;
#else
        _osc.decay(0.0f);
#endif
        _lastUpdateUs = 0;
        _prevThrottlePct = -1.0f;   // glide: sentinel seeds the next flap tick without kick
        lastFlapping = false;       // MUSHIN v1: glide → cadence decay on the muscle
        lastThrottlePct = 0.0f;
        lastFlapHz = 0.0f;
        _throttleRateLPF = 0.0f;
        _prevAileronNorm = -2.0f;   // glide: sentinel seeds without roll slew kick
        _aileronRateLPF = 0.0f;
        _prevElevatorNorm = -2.0f;  // glide: sentinel seeds without fer dwell kick
        _elevatorRateLPF = 0.0f;
        _elevFerStroke = 0.0f;
        _elevFerReturn = 0.0f;
        _antiGravGate = 0.0f;
#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
        // Glide pause: purge half-stroke accumulators so a fresh flap
        // burst starts clean (no stale SSFF biases / resonance charge).
        _ssffAccumError = 0.0f;
        _ssffAccumCount = 0;
        _ssffFerocityUpBias   = 0.0f;
        _ssffFerocityDownBias = 0.0f;
        _resonanceAccum = 0.0f;
        _prevFlappingSin = 0.0f;
        _ferHold = 0.0f;
        _ferHoldVel = 0.0f;
#endif
        angleLeft  = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd + elevatorCmd + glideCmd + gyroPitchCenter + gyroRollCenter) * ORNI_ANGULAR_MULTIPLIER);
        angleRight = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd - elevatorCmd - glideCmd - gyroPitchCenter + gyroRollCenter) * ORNI_ANGULAR_MULTIPLIER);
    }

    if (angleLeft  < 0) angleLeft  = 0; else if (angleLeft  > 180) angleLeft  = 180;
    if (angleRight < 0) angleRight = 0; else if (angleRight > 180) angleRight = 180;

    uint16_t usL = (uint16_t)(servoMinUs + (uint32_t)angleLeft  * (servoMaxUs - servoMinUs) / 180);
    uint16_t usR = (uint16_t)(servoMinUs + (uint32_t)angleRight * (servoMaxUs - servoMinUs) / 180);

    _f[SF_LEFT_WING]  = _clampServo((int32_t)usL + servoTrimUs[SF_LEFT_WING]);
    _f[SF_RIGHT_WING] = _clampServo((int32_t)usR + servoTrimUs[SF_RIGHT_WING]);

    // ── Tail / back wings (profile-dependent) ──
    float rudderNorm  = _crsfToNorm(voiceRudder);
    float rudderMix = (rudderNorm * rudderYawWeight * 0.01f) + (aileronNorm * rudderRollWeight * 0.01f);
    if (rudderMix > 1.0f) rudderMix = 1.0f;
    if (rudderMix < -1.0f) rudderMix = -1.0f;

    if (ACTIVE_PROFILE == SERVO_4WING) {
        // Back wings mirror front wings (same waveform), with their own trim
        _f[SF_BACK_LEFT_WING]  = _clampServo((int32_t)_f[SF_LEFT_WING]  + servoTrimUs[SF_BACK_LEFT_WING]);
        _f[SF_BACK_RIGHT_WING] = _clampServo((int32_t)_f[SF_RIGHT_WING] + servoTrimUs[SF_BACK_RIGHT_WING]);
    } else if (PROFILE.servoCount >= 3) {
        // SERVO_2WING_1RUD: crest/head rudder
        int32_t rudderUs = (int32_t)((float)ORNI_RUDDER_CENTER_US + rudderMix * 500.0f
    #ifdef ZEPHYRUS_ENABLED
                                   + gyroRudderCorrection
    #endif
                                  ) + servoTrimUs[SF_RUDDER];
        _f[SF_RUDDER] = _clampServo(rudderUs);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  GEARBOX KERNEL — CRSF channel → PWM, elevon mixing
// ═══════════════════════════════════════════════════════════════════
void Ornithopter::_computeGearboxMixer() {
    float aileronNorm  = _crsfToNorm(voiceAileron);
    float elevatorNorm = _crsfToNorm(voiceElevator);
    float throttleNorm = _crsfToNorm(voiceThrottle);
    float rudderNorm   = _crsfToNorm(voiceRudder);
    bool armed = _isArmed();

#ifdef ZEPHYRUS_ENABLED
    // Aeroelastic PID gain modulation for gearbox
    bool motorRunning = armed && (throttleNorm > 0.1f);
    aeroGainScale = motorRunning ? (aeroFlapCoeff * 0.01f) : (aeroGlideCoeff * 0.01f);
#endif

    // ── Motor ──
    float motorF = armed ? ((throttleNorm + 1.0f) * 0.5f) : 0.0f;  // 0..1
    _f[SF_MOTOR] = (uint16_t)(1000.0f + motorF * 1000.0f);
    _f[SF_MOTOR] = _clampServo(_f[SF_MOTOR]);

    // ── Rudder ──
    float rudderMix = (rudderNorm * rudderYawWeight * 0.01f) + (aileronNorm * rudderRollWeight * 0.01f);
    if (rudderMix > 1.0f) rudderMix = 1.0f;
    if (rudderMix < -1.0f) rudderMix = -1.0f;
    int32_t rudderUs = (int32_t)((float)ORNI_RUDDER_CENTER_US + rudderMix * 500.0f
#ifdef ZEPHYRUS_ENABLED
                               + gyroRudderCorrection
#endif
                              ) + servoTrimUs[SF_RUDDER];
    _f[SF_RUDDER] = _clampServo(rudderUs);

    // ── Tail surfaces ──
    {
        float rollPidUs  = 0.0f;
        float pitchPidUs = 0.0f;
    #ifdef ZEPHYRUS_ENABLED
        rollPidUs  = gyroAileronCorrection;
        pitchPidUs = gyroElevatorCorrection;
        if (rollPidUs  > ZEPHYR_GEARBOX_CLAMP_US) rollPidUs  = ZEPHYR_GEARBOX_CLAMP_US;
        if (rollPidUs  < -ZEPHYR_GEARBOX_CLAMP_US) rollPidUs  = -ZEPHYR_GEARBOX_CLAMP_US;
        if (pitchPidUs > ZEPHYR_GEARBOX_CLAMP_US) pitchPidUs = ZEPHYR_GEARBOX_CLAMP_US;
        if (pitchPidUs < -ZEPHYR_GEARBOX_CLAMP_US) pitchPidUs = -ZEPHYR_GEARBOX_CLAMP_US;
    #endif

        if (ACTIVE_PROFILE == GEARBOX_1ELE_1RUD || ACTIVE_PROFILE == GEARBOX_1MOT_1ELE_1RUD) {
            // Traditional tail: separate elevator + rudder (no elevon mix)
            float elev = elevatorNorm;
            if (elev > 1.0f) elev = 1.0f; else if (elev < -1.0f) elev = -1.0f;
            int32_t elevUs = (int32_t)((float)ORNI_SERVO_CENTER_US + elev * 500.0f + pitchPidUs) + servoTrimUs[SF_ELEVATOR];
            _f[SF_ELEVATOR] = _clampServo(elevUs);
        } else {
            // VTAIL profiles: elevon mix on V-tail surfaces
            float elevonScale = 500.0f;
            float leftMix  = aileronNorm + elevatorNorm;
            float rightMix = aileronNorm - elevatorNorm;
            if (leftMix  > 1.0f) leftMix  = 1.0f; else if (leftMix  < -1.0f) leftMix  = -1.0f;
            if (rightMix > 1.0f) rightMix = 1.0f; else if (rightMix < -1.0f) rightMix = -1.0f;

            int32_t vtailL = (int32_t)((float)ORNI_SERVO_CENTER_US + leftMix  * elevonScale
                                            + rollPidUs + pitchPidUs) + servoTrimUs[SF_VTAIL_LEFT];
            int32_t vtailR = (int32_t)((float)ORNI_SERVO_CENTER_US + rightMix * elevonScale
                                            - rollPidUs + pitchPidUs) + servoTrimUs[SF_VTAIL_RIGHT];
            _f[SF_VTAIL_LEFT]  = _clampServo(vtailL);
            _f[SF_VTAIL_RIGHT] = _clampServo(vtailR);
        }
    }
}

// ─── Update ────────────────────────────────────────────────────────
bool Ornithopter::update() {
    if (!enabled) return false;
    _readChannels();
    if (benchMode && !stickOverride) {
        // Panel/bench mode (WiFi WebUI, no RC link): hold the glide position
        // with neutral sticks so trim / glide-angle edits are visible live.
        voiceAileron  = 992;
        voiceElevator = 992;
        voiceThrottle = 172;   // below flap threshold → glide
        voiceRudder   = 992;
        voiceArm      = 1811;  // armed — mixer runs, throttle keeps it gliding
        voiceFreq     = 992;
        voiceProfile  = 992;
    }
    if (!linkUp && !stickOverride && !benchMode) {
        for (uint8_t i = 0; i < SF_COUNT; ++i) _f[i] = ORNI_SERVO_CENTER_US;
        if (PROFILE_IS_GEARBOX) _f[SF_MOTOR] = ORNI_SERVO_MIN_US;
        return true;
    }
    if (PROFILE_IS_GEARBOX) {
        _computeGearboxMixer();
    } else {
        _computeServoMixer();
    }
#if defined(MUSHIN_ENABLED)
    motionIntent.minimum = servoMinUs;
    motionIntent.maximum = servoMaxUs;
    motionIntent.trim[0] = servoTrimUs[SF_LEFT_WING];
    motionIntent.trim[1] = servoTrimUs[SF_RIGHT_WING];
    motionIntent.backTrim[0] = servoTrimUs[SF_BACK_LEFT_WING];
    motionIntent.backTrim[1] = servoTrimUs[SF_BACK_RIGHT_WING];
    if (PROFILE_IS_GEARBOX) { motionIntent.flapping = 0; motionIntent.hz = 0; }
    uint8_t n = 0;
    for (uint8_t ch = 0; ch < 7; ++ch) {
        const uint8_t f = COMPANION_FUNCTIONS[activeProfile][ch];
        if (f == SF_NONE) continue;
        motionIntent.output[n] = _f[f];
        motionIntent.kind[n] = f == SF_LEFT_WING ? 1 : f == SF_RIGHT_WING ? 2 :
                              f == SF_BACK_LEFT_WING ? 3 : f == SF_BACK_RIGHT_WING ? 4 :
                              f == SF_RUDDER ? 5 : f == SF_MOTOR ? 6 : 0;
        ++n;
    }
    while (n < 7) { motionIntent.kind[n] = 7; motionIntent.output[n++] = 1500; }
#endif
    return true;
}