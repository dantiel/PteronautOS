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

extern "C" void pteroLog(const char *fmt, ...);

Ornithopter::Ornithopter()
  : enabled(true)
  , linkUp(false)
  , voiceAileron(992), voiceElevator(992)
  , voiceThrottle(172), voiceRudder(992)
  , voiceArm(172)
  , voiceRudderFerocity(172), voiceStrokeFerocity(172)
  , voiceReturnFerocity(172), voiceCadence(992)
  , voiceAltitudeHold(172)
#ifdef ZEPHYRUS_ENABLED
  , gyroRudderCorrection(0.0f)
  , gyroAileronCorrection(0.0f)
  , gyroElevatorCorrection(0.0f)
#endif
  , _lastUpdateUs(0)
{
    for (uint8_t i = 0; i < SF_COUNT; ++i) _f[i] = ORNI_SERVO_CENTER_US;
    if (PROFILE_IS_GEARBOX) {
        _f[SF_MOTOR] = ORNI_SERVO_MIN_US;  // motor stopped
    }
    pteroLog("Ornithopter: profile %d — %u servos, %s kernel",
             (int)ACTIVE_PROFILE, PROFILE.servoCount,
             PROFILE_IS_GEARBOX ? "gearbox" : "servo");
}

void Ornithopter::onLinkUp()   { linkUp = true; }
void Ornithopter::onLinkDown() { linkUp = false; enterFailsafe(); }

void Ornithopter::enterFailsafe() {
    for (uint8_t i = 0; i < SF_COUNT; ++i) _f[i] = ORNI_SERVO_CENTER_US;
    if (PROFILE_IS_GEARBOX) {
        _f[SF_MOTOR] = ORNI_SERVO_MIN_US;
    } else {
        _osc.reset();
    }
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
    voiceAileron        = ChannelData[ORNI_CH_AILERON];
    voiceElevator       = ChannelData[ORNI_CH_ELEVATOR];
    voiceThrottle       = ChannelData[ORNI_CH_THROTTLE];
    voiceRudder         = ChannelData[ORNI_CH_RUDDER];
    voiceArm            = ChannelData[ORNI_CH_ARM];
    voiceRudderFerocity = ChannelData[ORNI_CH_RUDDER_FEROCITY];
    voiceStrokeFerocity = ChannelData[ORNI_CH_STROKE_FEROCITY];
    voiceReturnFerocity = ChannelData[ORNI_CH_RETURN_FEROCITY];
    voiceCadence        = ChannelData[ORNI_CH_CADENCE];
    voiceAltitudeHold   = ChannelData[ORNI_CH_ALTITUDE_HOLD];
}

uint16_t Ornithopter::_clampServo(uint16_t us) {
    if (us < ORNI_SERVO_MIN_US) return ORNI_SERVO_MIN_US;
    if (us > ORNI_SERVO_MAX_US) return ORNI_SERVO_MAX_US;
    return us;
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

    float aileronCmd  = aileronNorm * ORNI_AILERON_SCALE;
    float elevatorCmd = elevatorNorm * ORNI_ELEVATOR_SCALE;

    int angleLeft, angleRight;

    if (isFlapping) {
        float cadenceNorm  = _crsfToNorm(voiceCadence);
        float intent = (throttleUsF - 480.0f) *
            ((1.0f / (120.0f * ORNI_CYCLE_RATING)) + (cadenceNorm * 0.0000725f));
        _osc.cadenceTarget = intent;

        uint32_t nowUs = micros();
        if (_lastUpdateUs == 0) _lastUpdateUs = nowUs;
        float dt = (float)(nowUs - _lastUpdateUs) * 1e-6f;
        if (dt > 0.1f) dt = 0.1f;
        _lastUpdateUs = nowUs;

        float rawWave = _osc.advance(dt);
        float amplitude = (throttleUsF - (float)ORNI_FLAP_THRESHOLD_US) * ORNI_MAGNITUDE_SCALE;

        float strokeFer = _crsfToFloat(voiceStrokeFerocity, ORNI_FEROCITY_MIN, ORNI_FEROCITY_MAX);
        float returnFer = _crsfToFloat(voiceReturnFerocity, ORNI_FEROCITY_MIN, ORNI_FEROCITY_MAX);
        float rudderFer = _crsfToFloat(voiceRudderFerocity, ORNI_DIFFERENTIAL_MIN, ORNI_DIFFERENTIAL_MAX);

        float strokeFerL = strokeFer + rudderFer;
        float strokeFerR = strokeFer - rudderFer;
        float returnFerL = returnFer + rudderFer;
        float returnFerR = returnFer - rudderFer;

        float pulseL = FlappingOscillator::shapeWave(rawWave, strokeFerL, returnFerL);
        float pulseR = FlappingOscillator::shapeWave(rawWave, strokeFerR, returnFerR);

        float rudderFactor = ((1500.0f / (float)voiceRudder) - 1.0f) * 2.0f + 1.0f;

        float degL = amplitude * pulseL * rudderFactor;
        float degR = amplitude * pulseR / rudderFactor;

        angleLeft  = (int)((aileronCmd - degL + (float)ORNI_NEUTRAL_ANGLE_DEG - elevatorCmd) * ORNI_ANGULAR_MULTIPLIER);
        angleRight = (int)((aileronCmd + degR + (float)ORNI_NEUTRAL_ANGLE_DEG + elevatorCmd) * ORNI_ANGULAR_MULTIPLIER);
    } else {
        _osc.decay(0.0f);
        _lastUpdateUs = 0;
        angleLeft  = (int)((aileronCmd - (float)ORNI_GLIDE_ANGLE_DEG + (float)ORNI_NEUTRAL_ANGLE_DEG - elevatorCmd) * ORNI_ANGULAR_MULTIPLIER);
        angleRight = (int)((aileronCmd + (float)ORNI_GLIDE_ANGLE_DEG + (float)ORNI_NEUTRAL_ANGLE_DEG + elevatorCmd) * ORNI_ANGULAR_MULTIPLIER);
    }

    if (angleLeft  < 0) angleLeft  = 0; else if (angleLeft  > 180) angleLeft  = 180;
    if (angleRight < 0) angleRight = 0; else if (angleRight > 180) angleRight = 180;

    uint16_t usL = (uint16_t)(ORNI_SERVO_MIN_US + (uint32_t)angleLeft  * (ORNI_SERVO_MAX_US - ORNI_SERVO_MIN_US) / 180);
    uint16_t usR = (uint16_t)(ORNI_SERVO_MIN_US + (uint32_t)angleRight * (ORNI_SERVO_MAX_US - ORNI_SERVO_MIN_US) / 180);

    _f[SF_LEFT_WING]  = _clampServo(usL);
    _f[SF_RIGHT_WING] = _clampServo(usR);

    // ── Tail / back wings (profile-dependent) ──
    float rudderNorm  = _crsfToNorm(voiceRudder);
    float rudderMix = (rudderNorm * ORNI_RUDDER_YAW_WEIGHT) + (aileronNorm * ORNI_RUDDER_ROLL_WEIGHT);
    if (rudderMix > 1.0f) rudderMix = 1.0f;
    if (rudderMix < -1.0f) rudderMix = -1.0f;

    if (ACTIVE_PROFILE == SERVO_4WING) {
        // Back wings mirror front wings (same waveform)
        _f[SF_BACK_LEFT_WING]  = _f[SF_LEFT_WING];
        _f[SF_BACK_RIGHT_WING] = _f[SF_RIGHT_WING];
    } else if (PROFILE.servoCount >= 3) {
        // SERVO_2WING_1RUD: crest/head rudder
        _f[SF_RUDDER] = (uint16_t)((float)ORNI_RUDDER_CENTER_US + rudderMix * 500.0f
    #ifdef ZEPHYRUS_ENABLED
                                   + gyroRudderCorrection
    #endif
                                  );
        _f[SF_RUDDER] = _clampServo(_f[SF_RUDDER]);
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

    // ── Motor ──
    float motorF = armed ? ((throttleNorm + 1.0f) * 0.5f) : 0.0f;  // 0..1
    _f[SF_MOTOR] = (uint16_t)(1000.0f + motorF * 1000.0f);
    _f[SF_MOTOR] = _clampServo(_f[SF_MOTOR]);

    // ── Rudder ──
    float rudderMix = (rudderNorm * ORNI_RUDDER_YAW_WEIGHT) + (aileronNorm * ORNI_RUDDER_ROLL_WEIGHT);
    if (rudderMix > 1.0f) rudderMix = 1.0f;
    if (rudderMix < -1.0f) rudderMix = -1.0f;
    _f[SF_RUDDER] = (uint16_t)((float)ORNI_RUDDER_CENTER_US + rudderMix * 500.0f
#ifdef ZEPHYRUS_ENABLED
                               + gyroRudderCorrection
#endif
                              );
    _f[SF_RUDDER] = _clampServo(_f[SF_RUDDER]);

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
            _f[SF_ELEVATOR] = (uint16_t)((float)ORNI_SERVO_CENTER_US + elev * 500.0f + pitchPidUs);
            _f[SF_ELEVATOR] = _clampServo(_f[SF_ELEVATOR]);
        } else {
            // VTAIL profiles: elevon mix on V-tail surfaces
            float elevonScale = 500.0f;
            float leftMix  = aileronNorm + elevatorNorm;
            float rightMix = aileronNorm - elevatorNorm;
            if (leftMix  > 1.0f) leftMix  = 1.0f; else if (leftMix  < -1.0f) leftMix  = -1.0f;
            if (rightMix > 1.0f) rightMix = 1.0f; else if (rightMix < -1.0f) rightMix = -1.0f;

            _f[SF_VTAIL_LEFT]  = (uint16_t)((float)ORNI_SERVO_CENTER_US + leftMix  * elevonScale
                                            + rollPidUs + pitchPidUs);
            _f[SF_VTAIL_RIGHT] = (uint16_t)((float)ORNI_SERVO_CENTER_US + rightMix * elevonScale
                                            - rollPidUs + pitchPidUs);
            _f[SF_VTAIL_LEFT]  = _clampServo(_f[SF_VTAIL_LEFT]);
            _f[SF_VTAIL_RIGHT] = _clampServo(_f[SF_VTAIL_RIGHT]);
        }
    }
}

// ─── Update ────────────────────────────────────────────────────────
bool Ornithopter::update() {
    if (!enabled) return false;
    _readChannels();
    if (!linkUp) {
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