#include "Ornithopter.h"
#ifdef UNIT_TEST
  #include <stdint.h>
  static inline uint32_t micros() { return 0; }
  static inline void delay(unsigned long) {}
#else
  #include <Arduino.h>
#endif
#include <algorithm>  // for std::min/std::max

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
  , servoLeftUs(ORNI_SERVO_CENTER_US)
  , servoRightUs(ORNI_SERVO_CENTER_US)
  , servoRudderUs(ORNI_RUDDER_CENTER_US)
#ifdef ZEPHYRUS_ENABLED
  , gyroRudderCorrection(0.0f)
#endif
  , _lastUpdateUs(0)
{
    pteroLog("Ornithopter: initialized — 3-servo waveform mixer ready");
}

void Ornithopter::onLinkUp()   { linkUp = true; }
void Ornithopter::onLinkDown() { linkUp = false; enterFailsafe(); }

void Ornithopter::enterFailsafe() {
    servoLeftUs   = ORNI_SERVO_CENTER_US;
    servoRightUs  = ORNI_SERVO_CENTER_US;
    servoRudderUs = ORNI_RUDDER_CENTER_US;
    _osc.reset();
}

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

void Ornithopter::_computeMixer() {
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

    // Clamp angles to [0, 180]
    if (angleLeft  < 0) angleLeft  = 0; else if (angleLeft  > 180) angleLeft  = 180;
    if (angleRight < 0) angleRight = 0; else if (angleRight > 180) angleRight = 180;

    uint16_t usL = (uint16_t)(ORNI_SERVO_MIN_US + (uint32_t)angleLeft  * (ORNI_SERVO_MAX_US - ORNI_SERVO_MIN_US) / 180);
    uint16_t usR = (uint16_t)(ORNI_SERVO_MIN_US + (uint32_t)angleRight * (ORNI_SERVO_MAX_US - ORNI_SERVO_MIN_US) / 180);

    servoLeftUs  = _clampServo(usL);
    servoRightUs = _clampServo(usR);

    // --- Front Rudder ---
    float rudderNorm   = _crsfToNorm(voiceRudder);
    float rudderMix    = rudderNorm + (aileronNorm * ORNI_RUDDER_MIX_SCALE);
    if (rudderMix > 1.0f) rudderMix = 1.0f;
    if (rudderMix < -1.0f) rudderMix = -1.0f;
    servoRudderUs = (uint16_t)((float)ORNI_RUDDER_CENTER_US + rudderMix * 500.0f
#ifdef ZEPHYRUS_ENABLED
                               + gyroRudderCorrection
#endif
                              );
    servoRudderUs = _clampServo(servoRudderUs);
}

bool Ornithopter::update() {
    if (!enabled) return false;
    _readChannels();
    if (!linkUp) {
        servoLeftUs   = ORNI_SERVO_CENTER_US;
        servoRightUs  = ORNI_SERVO_CENTER_US;
        servoRudderUs = ORNI_RUDDER_CENTER_US;
        return true;
    }
    _computeMixer();
    return true;
}