#include "Ornithopter.h"

// ─────────────────────────────────────────────────────────────────────
//  GEARBOX MIXER — CRSF channel → PWM (motor + rudder + tail surfaces)
// ─────────────────────────────────────────────────────────────────────
// Non-flapping drive profiles (gearbox + motor + V-tail/elevator). The
// waveform kernels live in OrnithopterMixer.cpp; this path has no oscillator
// — it maps channels and gyro corrections straight to servo pulses.

void Ornithopter::_computeGearboxMixer()
{
    const float aileronNorm  = _crsfToNorm(voiceAileron);
    const float elevatorNorm = _crsfToNorm(voiceElevator);
    const float throttleNorm = _crsfToNorm(voiceThrottle);
    const float rudderNorm   = _crsfToNorm(voiceRudder);
    const bool  armed        = _isArmed();

#ifdef ZEPHYRUS_ENABLED
    // Aeroelastic PID gain modulation for gearbox.
    const bool motorRunning = armed && (throttleNorm > 0.1f);
    aeroGainScale = motorRunning ? (aeroFlapCoeff * 0.01f) : (aeroGlideCoeff * 0.01f);
#endif

    // ── Motor ──
    const float motorF = armed ? ((throttleNorm + 1.0f) * 0.5f) : 0.0f;  // 0..1
    _f[SF_MOTOR] = _clampServo((int32_t)(1000.0f + motorF * 1000.0f));

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
    float rollPidUs  = 0.0f;
    float pitchPidUs = 0.0f;
#ifdef ZEPHYRUS_ENABLED
    rollPidUs  = gyroAileronCorrection;
    pitchPidUs = gyroElevatorCorrection;
    if (rollPidUs  >  ZEPHYR_GEARBOX_CLAMP_US) rollPidUs  =  ZEPHYR_GEARBOX_CLAMP_US;
    if (rollPidUs  < -ZEPHYR_GEARBOX_CLAMP_US) rollPidUs  = -ZEPHYR_GEARBOX_CLAMP_US;
    if (pitchPidUs >  ZEPHYR_GEARBOX_CLAMP_US) pitchPidUs =  ZEPHYR_GEARBOX_CLAMP_US;
    if (pitchPidUs < -ZEPHYR_GEARBOX_CLAMP_US) pitchPidUs = -ZEPHYR_GEARBOX_CLAMP_US;
#endif

    if (ACTIVE_PROFILE == GEARBOX_1ELE_1RUD || ACTIVE_PROFILE == GEARBOX_1MOT_1ELE_1RUD) {
        // Traditional tail: separate elevator + rudder (no elevon mix).
        float elev = elevatorNorm;
        if (elev > 1.0f) elev = 1.0f; else if (elev < -1.0f) elev = -1.0f;
        int32_t elevUs = (int32_t)((float)ORNI_SERVO_CENTER_US + elev * 500.0f + pitchPidUs)
                       + servoTrimUs[SF_ELEVATOR];
        _f[SF_ELEVATOR] = _clampServo(elevUs);
    } else {
        // VTAIL profiles: elevon mix on V-tail surfaces.
        const float elevonScale = 500.0f;
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
