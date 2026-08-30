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
    throttleFerocityMix = p.throttleFerocityMix;
}

void Ornithopter::setFlightProfileParams(uint8_t idx, float sf, float rf, int8_t glide, int8_t flapAng, float ail, float elev, float rudRng, float rudAmpDiff, float elevFerMix, float thrFerMix)
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
    p.throttleFerocityMix = thrFerMix;
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
  , throttleFerocityMix(0.0f)
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
#endif
  , _lastUpdateUs(0)
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
        flightProfiles[0] = { 30.0f, 50.0f, -4, 0, 40.0f, 60.0f, 50.0f, 0.0f };
        flightProfiles[1] = { 50.0f, 50.0f, -4, 0, 40.0f, 60.0f, 50.0f, 0.0f };
        flightProfiles[2] = { 70.0f, 50.0f,  2, 0, 40.0f, 60.0f, 50.0f, 0.0f };
}

void Ornithopter::onLinkUp() {
    linkUp = true;
#ifdef ZEPHYRUS_ENABLED
    // Reset SSFF state on arm — fresh biases for each flight
    _prevFlappingSin = 0.0f;
    _ssffAccumError = 0.0f;
    _ssffAccumCount = 0;
    _ssffFerocityUpBias = 0.0f;
    _ssffFerocityDownBias = 0.0f;
#endif
}

void Ornithopter::onLinkDown() { linkUp = false; enterFailsafe(); }

void Ornithopter::enterFailsafe() {
    for (uint8_t i = 0; i < SF_COUNT; ++i) _f[i] = ORNI_SERVO_CENTER_US;
    if (PROFILE_IS_GEARBOX) {
        _f[SF_MOTOR] = ORNI_SERVO_MIN_US;
    } else {
        _osc.reset();
    }
#ifdef ZEPHYRUS_ENABLED
    _prevFlappingSin = 0.0f;
    _ssffAccumError = 0.0f;
    _ssffAccumCount = 0;
    _ssffFerocityUpBias = 0.0f;
    _ssffFerocityDownBias = 0.0f;
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
    bool armed = (voiceArm > 992);

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

    int angleLeft, angleRight;

    if (isFlapping) {
        // Frequency modulator channel (FREQ) sets flap rate directly within
        // the window [ORNI_FREQ_MIN … flapBaseFreq]; throttle sets stroke
        // amplitude (%). Both CRSF raw (172–1811).
        float freq01 = _crsfToNorm(voiceFreq) * 0.5f + 0.5f;   // 0..1
        float freqMax = flapBaseFreq * 0.1f;                   // deci-Hz → Hz
        float freqHz = ORNI_FREQ_MIN + freq01 * (freqMax - ORNI_FREQ_MIN);
        _osc.cadenceTarget = freqHz * 6.283185307f;            // 2π rad/s

        uint32_t nowUs = micros();
        if (_lastUpdateUs == 0) _lastUpdateUs = nowUs;
        float dt = (float)(nowUs - _lastUpdateUs) * 1e-6f;
        if (dt > 0.1f) dt = 0.1f;
        _lastUpdateUs = nowUs;

#ifdef ZEPHYRUS_ENABLED
        // STEP 7: ALL gyro values hard-zeroed (NaN guard test)
        _osc.kGainMod = 1.0f;
        gyroRudderCorrection = 0.0f;
        gyroAileronCorrection = 0.0f;
        gyroElevatorCorrection = 0.0f;
#endif

        _osc.anchorGain = 0.0f;

        float rawWave = _osc.advance(dt);

#ifdef ZEPHYRUS_ENABLED
        _ssffAccumError = 0.0f;
        _ssffAccumCount = 0;
        _prevFlappingSin = rawWave;
#endif

        // Amplitude = throttle % of the servo-speed-limited max at this freq.
        float degPerSec = 60.0f / (servoSpeed * 0.001f);       // ms/60° → °/s
        float ampMax = degPerSec / (2.0f * freqHz);
                if (ampMax > ORNI_AMP_MAX) ampMax = ORNI_AMP_MAX;            // hard safety ceiling
        float throttlePct = constrain((throttleUsF - (float)ORNI_FLAP_THRESHOLD_US) /
                                      (1811.0f - (float)ORNI_FLAP_THRESHOLD_US), 0.0f, 1.0f);
        float amplitude = throttlePct * ampMax;

        // Elevator → ferocity: ASYMMETRIC stroke modulation.
        // Elevator-up (norm +1) strengthens the DOWNSTROKE (power stroke)
        // ferocity; elevator-down (norm -1) strengthens the UPSTROKE
        // (recovery) ferocity. Each direction boosts only one half-stroke.
        // CRSF: elevator up = 1811 (norm +1), down = 172 (norm -1).
        float elevFerScale = elevatorFerocityMix * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN);
        float elevUpBoost   = fmaxf( elevatorNorm, 0.0f) * elevFerScale;   // climb → downstroke
        float elevDownBoost = fmaxf(-elevatorNorm, 0.0f) * elevFerScale;   // dive  → upstroke

        // Throttle → ferocity mix (per-profile, 0–100). Adds dwell/aggression
        // proportional to throttle %. 0 = throttle drives amplitude only (pure
        // sine at 0/0 ferocity); 100 = full coupling up to square at full gas.
        float throttleFerBoost = throttlePct * throttleFerocityMix * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN);

        // Ferocity (dwell/shape) = per-profile stroke/return sliders
        // + elevator mix + throttle mix (+ gyro).

#ifdef ZEPHYRUS_ENABLED
        float ferocitySignal = 0.0f;
        float iBias = 0.0f;
        float resonanceBias = 0.0f;

        float strokeFer = ORNI_FEROCITY_MIN + strokeFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN)
                          + ferocitySignal + iBias + _ssffFerocityUpBias + resonanceBias + elevUpBoost + throttleFerBoost;
        float returnFer = ORNI_FEROCITY_MIN + returnFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN)
                          + ferocitySignal - iBias + _ssffFerocityDownBias + resonanceBias + elevDownBoost + throttleFerBoost;
#else
        float strokeFer = ORNI_FEROCITY_MIN + strokeFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN) + elevUpBoost + throttleFerBoost;
        float returnFer = ORNI_FEROCITY_MIN + returnFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN) + elevDownBoost + throttleFerBoost;
#endif
        // Yaw stick → L/R wing differential, scaled by rudder_ferocity_range (0–100)
        float rudderFer = _crsfToNorm(voiceRudder) * rudderFerocityRange * 0.01f * ORNI_DIFFERENTIAL_MAX;

        // Yaw stick → L/R differential flap amplitude, scaled by
        // rudder_amplitude_differential (0–100). Multiplicative so the two
        // wings diverge symmetrically around the throttle-set amplitude.
        float rudderAmpDiff = _crsfToNorm(voiceRudder) * rudderAmplitudeDifferential * 0.01f;
        float amplitudeL = amplitude * (1.0f + rudderAmpDiff);
        float amplitudeR = amplitude * (1.0f - rudderAmpDiff);

        float strokeFerL = strokeFer + rudderFer;
        float strokeFerR = strokeFer - rudderFer;
        float returnFerL = returnFer + rudderFer;
        float returnFerR = returnFer - rudderFer;

        // Shared reversal threshold computed from the BASE ferocities (before
        // rudder differential), so both wings reverse at the SAME phase even
        // when their per-wing ferocities differ. Faithful to GralhaAzul's
        // limiarBase (shared downstroke/upstroke boundary).
        float fDbase = strokeFer; if (fDbase < 0.0f) fDbase = 0.0f; else if (fDbase > 8.0f) fDbase = 8.0f;
        float fSbase = returnFer; if (fSbase < 0.0f) fSbase = 0.0f; else if (fSbase > 8.0f) fSbase = 8.0f;
        float wDbase = 8.0f - fDbase; if (wDbase < 0.01f) wDbase = 0.01f;
        float wSbase = 8.0f - fSbase; if (wSbase < 0.01f) wSbase = 0.01f;
        float limiarShared = 6.283185307f * wDbase / (wDbase + wSbase);

        float pulseL = FlappingOscillator::shapeWave(rawWave, strokeFerL, returnFerL, limiarShared);
        float pulseR = FlappingOscillator::shapeWave(rawWave, strokeFerR, returnFerR, limiarShared);

#ifdef ZEPHYRUS_ENABLED
        _resonanceAccum = 0.0f;
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
                angleLeft  = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd + elevatorCmd + flapCenterCmd - degL) * ORNI_ANGULAR_MULTIPLIER);
                angleRight = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd - elevatorCmd - flapCenterCmd + degR) * ORNI_ANGULAR_MULTIPLIER);
    } else {
        _osc.decay(0.0f);
        _lastUpdateUs = 0;
        angleLeft  = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd + elevatorCmd + glideCmd) * ORNI_ANGULAR_MULTIPLIER);
        angleRight = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (aileronCmd - elevatorCmd - glideCmd) * ORNI_ANGULAR_MULTIPLIER);
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
    bool armed = (voiceArm > 992);

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
    return true;
}