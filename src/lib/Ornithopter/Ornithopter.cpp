#include "Ornithopter.h"
#ifdef UNIT_TEST
  #include <stdint.h>
  static inline uint32_t micros() { return 0; }
  static inline void delay(unsigned long) {}
#else
  #include <Arduino.h>
#endif

// ─────────────────────────────────────────────────────────────────────
//  KERNEL — lifecycle, link state, channel input, arming, update loop
// ─────────────────────────────────────────────────────────────────────

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
  , tailDihedralMix(0.0f)
  , tailYawCoupling(50.0f)
  , motorMinUs(ORNI_SERVO_MIN_US)
  , motorMaxUs(ORNI_SERVO_MAX_US)
    , glideMode(false)
    , benchMode(false)
    , hallSensorPin(12)
  , ratchetThrottlePct(15)
  , ratchetTimeoutMs(500)
  , glitchActive(true)
  , glitchDeltaMax(160)
  , glitchDeltaArm(800)
  , glitchDeltaPersist(320)
  , glitchSimultThreshold(4)
#ifdef ZEPHYRUS_ENABLED
  , gyroRudderCorrection(0.0f)
  , gyroAileronCorrection(0.0f)
  , gyroElevatorCorrection(0.0f)
  , gyroRollCorrection(0.0f), gyroYawCorrection(0.0f), gyroPitchCorrection(0.0f)
  , gyroRollAttitudeCorrection(0.0f), gyroPitchAttitudeCorrection(0.0f)
#ifdef MESOZOIC_HORIZON
  , flapSin(0.0f), flapCos(1.0f)
#endif
  , wingRollGain(ORNI_WING_ROLL_GAIN), wingPitchGain(ORNI_WING_PITCH_GAIN)
  , wingPitchFerGain(ORNI_WING_PITCH_FER_GAIN)
  , wingPitchSkewGain(ORNI_WING_PITCH_SKEW_GAIN)
  , wingRollSkewGain(ORNI_WING_ROLL_SKEW_GAIN)
  , wingYawSkewGain(ORNI_WING_YAW_SKEW_GAIN)
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
  , _wasArmedAtLoss(false)
  , _wasFlapping(false)
  , _prevThrottlePct(-1.0f)
  , _throttleRateLPF(0.0f)
  , _prevAileronNorm(-2.0f)
  , _aileronRateLPF(0.0f)
  , _prevElevatorNorm(-2.0f)
  , _elevatorRateLPF(0.0f)
  , _elevFerStroke(0.0f)
  , _elevFerReturn(0.0f)
  , _antiGravGate(0.0f)
  , _glitchInitialized(false)
  , _glitchGhostStreak(0)
{
    modelName[0] = '\0';
    _glitchFrameTick = 0;
    _armLowFrame = 0xFFFF;   // arm-high sentinel — no debounce state at boot
    for (uint8_t i = 0; i < STK_COUNT; ++i) {
        _glitchRef[i]         = 992;
        _glitchCandidate[i]   = 992;
        _glitchPersist[i] = 0;
        _glitchLastRaw[i]     = 992;
    }
    for (uint8_t i = 0; i < STK_COUNT; ++i) stickChannels[i] = 992; // center (CRSF neutral)
        stickChannels[STK_THROTTLE] = 172;  // glide (below flap threshold) so channel test starts at rest
    stickChannels[STK_ARM]      = 1811; // armed (CRSF full)
    stickChannels[STK_FREQ]     = 1500; // mid flap frequency
    stickChannels[STK_PROFILE]  = 992;  // profile selector mid
    for (uint8_t i = 0; i < SF_COUNT; ++i) { _f[i] = ORNI_SERVO_CENTER_US; servoTrimUs[i] = ORNI_SERVO_TRIM_US; }
    if (PROFILE_IS_GEARBOX) {
        _f[SF_MOTOR] = ORNI_SERVO_MIN_US;  // motor stopped
    }
    _loadDefaultFlightProfiles();
}

// ─── Link state ─────────────────────────────────────────────────────

void Ornithopter::onLinkUp() {
    linkUp = true;
    _armLowFrame = 0xFFFF;      // defined state on fresh link — no stale debounce
    // Fully define the filter state: sentinel the freshness tracker so the
    // first post-link frame ALWAYS baselines — even when it equals the
    // held/failsafe values left in the tracker (neutral sticks). Without
    // this the filter stayed un-baselined (passthrough) until the first
    // stick movement — an undefined protection state at the worst moment.
    _resetGlitchFilter();
    // Arm continuity: if the bird was armed when the link dropped and the
    // arm switch is still high, resume armed immediately — the failsafe
    // already proved the outputs safe. Power-on still requires the
    // throttle-zero arming ceremony (_wasArmedAtLoss starts false).
    if (_wasArmedAtLoss && ChannelData[ORNI_CH_ARM] > 992) {
        _armedState = true;
    }
    _resetSlewTransients();     // seed on next flap tick — no stale slew kick
    _ondasReset();
}

void Ornithopter::onLinkDown() {
    if (linkUp) _wasArmedAtLoss = _armedState;  // snapshot on the loss transition only
    linkUp = false;
    enterFailsafe();
}

void Ornithopter::enterFailsafe() {
    _armedState = false;  // disarm — recovery re-arms only if _wasArmedAtLoss
    _armLowFrame = 0xFFFF;   // defined state during the gap — no stale debounce
    _resetGlitchFilter();
    for (uint8_t i = 0; i < SF_COUNT; ++i) _f[i] = ORNI_SERVO_CENTER_US;
    if (PROFILE_IS_GEARBOX) {
        _f[SF_MOTOR] = ORNI_SERVO_MIN_US;
    } else {
        _osc.reset();
        _resetElevatorTransients();  // fresh seed on next flap tick
    }
    _ondasReset();
}

// ─── Helpers ────────────────────────────────────────────────────────

float Ornithopter::_crsfToFloat(uint16_t raw, float outMin, float outMax) {
    float t = (float)(raw - 172) / (float)(1811 - 172);
    return outMin + t * (outMax - outMin);
}

float Ornithopter::_crsfToNorm(uint16_t raw) {
    return _crsfToFloat(raw, -1.0f, 1.0f);
}

void Ornithopter::_resetSlewTransients() {
    _prevThrottlePct = -1.0f;   // seed on next flap tick — no stale slew kick
    _throttleRateLPF = 0.0f;
    _prevAileronNorm = -2.0f;   // seed on next flap tick — no stale roll slew kick
    _aileronRateLPF = 0.0f;
    _resetElevatorTransients();
}

void Ornithopter::_resetElevatorTransients() {
    _prevElevatorNorm = -2.0f;  // seed on next flap tick — no stale fer dwell kick
    _elevatorRateLPF = 0.0f;
    _elevFerStroke = 0.0f;
    _elevFerReturn = 0.0f;
    _antiGravGate = 0.0f;
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
        _applyGlitchFilter();
    }

    // Flight profile selector (multi-position): map CRSF raw → 0..2.
    uint8_t prof = 1;
    if (voiceProfile < 660)       prof = 0;
    else if (voiceProfile < 1500) prof = 1;
    else                          prof = 2;
    if (prof != activeFlightProfile) applyFlightProfile(prof);
}

// ─── Arming latch ───────────────────────────────────────────────────
// Requires the arm switch high AND throttle at zero (below the pre-arm
// threshold). Once latched, throttle may rise to flap; dropping the arm
// switch (or link loss / failsafe) clears the latch.
bool Ornithopter::_isArmed() {
    if (voiceArm <= 992) {
        if (_armLowFrame == 0xFFFF) _armLowFrame = _glitchFrameTick;  // start of a low run
        // Disarm only after the low state persists across 3 DISTINCT CRSF
        // frames. A single corrupted frame — e.g. one admitted by the ghost-
        // streak re-baseline, or arriving while the filter is in a passthrough
        // window — must NOT kill the latch permanently. A genuine switch flip
        // stays low on every frame, so it disarms within ~13 ms at 150 Hz.
        else if ((uint16_t)(_glitchFrameTick - _armLowFrame) >= 2) _armedState = false;
    } else {
        _armLowFrame = 0xFFFF;               // arm high — reset the low-run tracker
        if (!_armedState) {
            _armedState = (voiceThrottle < ORNI_ARM_THROTTLE_ZERO_US);  // arm only at zero throttle
        }
    }
    return _armedState;
}

uint16_t Ornithopter::_clampServo(int32_t us) {
    if (us < ORNI_SERVO_ABS_MIN_US) return ORNI_SERVO_ABS_MIN_US;
    if (us > ORNI_SERVO_ABS_MAX_US) return ORNI_SERVO_ABS_MAX_US;
    return (uint16_t)us;
}

// ─── Update ─────────────────────────────────────────────────────────

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
    for (uint8_t ch = 0; ch < ORNI_MAX_OUTPUTS; ++ch) {
        const uint8_t f = COMPANION_FUNCTIONS[activeProfile][ch];
        if (f == SF_NONE) continue;
        motionIntent.output[n] = _f[f];
        motionIntent.kind[n] = f == SF_LEFT_WING ? 1 : f == SF_RIGHT_WING ? 2 :
                              f == SF_BACK_LEFT_WING ? 3 : f == SF_BACK_RIGHT_WING ? 4 :
                              f == SF_RUDDER ? 5 : f == SF_MOTOR ? 6 : 0;
        ++n;
    }
    while (n < ORNI_MAX_OUTPUTS) { motionIntent.kind[n] = 7; motionIntent.output[n++] = 1500; }
#endif
    return true;
}