#include "Ornithopter.h"
#ifdef UNIT_TEST
  #include <stdint.h>
  static inline uint32_t micros() { return 0; }
  static inline uint32_t millis() { return 0; }
  static inline void delay(unsigned long) {}
#else
  #include <Arduino.h>
#endif

// ─────────────────────────────────────────────────────────────────────
//  GEARBOX MIXER — CRSF channel → PWM (motor + head rudder + tail surfaces)
// ─────────────────────────────────────────────────────────────────────
// Non-flapping drive profiles. There is no oscillator here — the GEARBOX
// glide controller (a dantiel-style attitude + rate closed loop) steers the
// tail while the pilot's sticks command rate/attitude. Every profile is a
// COMPLETE mixer: wing and tail surfaces are never split into separate
// mixes, because that would fragment pin allocation across the two.
//
// Tail physics (full derivation in docs/documentation/mixers):
//   • Rotation tails (3-servo and 1-pitch+1-rotate): at neutral pitch,
//     rotation is pure roll and produces ZERO yaw. Yaw appears only when the
//     tail is pitched, and its sign INVERTS with pitch sign — yaw rides the
//     rotation differential scaled by the tail's live pitch deflection.
//   • Split/elevon tail: dihedral/anhedral (tailDihedralMix) sets how much
//     yaw rides the roll differential (0 = flat, + = dihedral, − = anhedral).
//   • Merged tail: both servos split pitch (common mode) and rotation
//     (differential), distributing the tail load. Same yaw inversion.

void Ornithopter::_computeGearboxMixer()
{
    const float aileronNorm  = _crsfToNorm(voiceAileron);
    const float elevatorNorm = _crsfToNorm(voiceElevator);
    // Altitude hold modulates the motor throttle (gearbox) exactly as it does
    // flap amplitude (servo) — the controller output replaces the stick value.
    const float throttleNorm = _crsfToNorm(_altitudeHoldThrottle((float)voiceThrottle));
    const float rudderNorm   = _crsfToNorm(voiceRudder);
    const bool  armed        = _isArmed();

    // ── Dantiel-style gearbox glide: pilot throttle below the glide
    //    threshold engages glide — a leveling burst at glideThrottleUs
    //    settles the bird, then the motor drops to glideIdleUs. Above the
    //    threshold the stick drives the motor normally. Operates on the RAW
    //    stick so a manual pull-to-glide overrides altitude hold.
    if (glideMode && armed)
    {
        const uint16_t stickUs = (uint16_t)_crsfToFloat((float)voiceThrottle,
                                                        (float)ORNI_SERVO_MIN_US,
                                                        (float)ORNI_SERVO_MAX_US);
        if (stickUs < glideThrottleUs)
        {
            if (!glideActive) { glideActive = true; _glideEnteredMs = millis(); }
        }
        else
        {
            glideActive = false;
        }
    }
    else
    {
        glideActive = false;
    }

#ifdef ZEPHYRUS_ENABLED
    const bool motorRunning = armed && (throttleNorm > 0.1f) && !glideActive;
    aeroGainScale = motorRunning ? (aeroFlapCoeff * 0.01f) : (aeroGlideCoeff * 0.01f);
#endif

    // ── Motor (0..1 throttle across the configured idle→full window) ──
    uint16_t motorUs;
    if (glideActive)
    {
        const uint32_t elapsed = millis() - _glideEnteredMs;
        motorUs = (elapsed < glideLevelMs) ? glideThrottleUs : glideIdleUs;
    }
    else
    {
        const float motorF = armed ? ((throttleNorm + 1.0f) * 0.5f) : 0.0f;
        motorUs = (uint16_t)((float)motorMinUs + motorF * (float)(motorMaxUs - motorMinUs));
    }
    _f[SF_MOTOR] = _clampServo((int32_t)motorUs);

    // ── Glide controller: attitude + rate closed loop (µs) ────────────
    // Rate dampers (gyroAileron/ElevatorCorrection) plus attitude level hold
    // (gyroRoll/PitchAttitudeCorrection × ATT_GAIN) on the same axis. Yaw has
    // no magnetometer reference, so it is rate-damped only (rudderCorrection).
    float rollPidUs  = 0.0f, pitchPidUs = 0.0f, yawPidUs = 0.0f;
#ifdef ZEPHYRUS_ENABLED
    rollPidUs  = gyroAileronCorrection + gyroRollAttitudeCorrection  * ZEPHYR_GEARBOX_ATT_GAIN;
    pitchPidUs = gyroElevatorCorrection + gyroPitchAttitudeCorrection * ZEPHYR_GEARBOX_ATT_GAIN;
    yawPidUs   = gyroRudderCorrection;
    if (rollPidUs  >  ZEPHYR_GEARBOX_CLAMP_US) rollPidUs  =  ZEPHYR_GEARBOX_CLAMP_US;
    if (rollPidUs  < -ZEPHYR_GEARBOX_CLAMP_US) rollPidUs  = -ZEPHYR_GEARBOX_CLAMP_US;
    if (pitchPidUs >  ZEPHYR_GEARBOX_CLAMP_US) pitchPidUs =  ZEPHYR_GEARBOX_CLAMP_US;
    if (pitchPidUs < -ZEPHYR_GEARBOX_CLAMP_US) pitchPidUs = -ZEPHYR_GEARBOX_CLAMP_US;
    if (yawPidUs   >  ZEPHYR_GEARBOX_CLAMP_US) yawPidUs   =  ZEPHYR_GEARBOX_CLAMP_US;
    if (yawPidUs   < -ZEPHYR_GEARBOX_CLAMP_US) yawPidUs   = -ZEPHYR_GEARBOX_CLAMP_US;
#endif

    // ── Head rudder (pilot yaw + roll coupling + gyro yaw) ────────────
    float rudderMix = (rudderNorm * rudderYawWeight * 0.01f) + (aileronNorm * rudderRollWeight * 0.01f);
    if (rudderMix > 1.0f) rudderMix = 1.0f;
    if (rudderMix < -1.0f) rudderMix = -1.0f;
    _f[SF_RUDDER] = _clampServo((int32_t)((float)ORNI_RUDDER_CENTER_US + rudderMix * 500.0f + yawPidUs)
                              + servoTrimUs[SF_RUDDER]);

    // ── Tail command (µs around centre): pilot ±500 µs + glide correction ──
    // Authority scales (0–100) gate BOTH the pilot stick and the gyro PID on
    // the same axis. The gyro PID (Zephyrus) already applied its own gains in
    // µs; these scales are the per-axis authority gate on top — the same role
    // aileronScale/elevatorScale play in the servo-waveform kernel. rudderScale
    // is the yaw equivalent for the tail surfaces (flattail + rotation/elevon/
    // merged tails, which drive BOTH roll and yaw off the same differential).
    const float rollScale  = aileronScale  * 0.01f;
    const float pitchScale = elevatorScale * 0.01f;
    const float yawScale   = rudderScale   * 0.01f;

    const float rollUs  = (aileronNorm  * 500.0f + rollPidUs)  * rollScale;
    const float pitchUs = (elevatorNorm * 500.0f + pitchPidUs) * pitchScale;
    const float yawUs   = (rudderNorm   * 500.0f + yawPidUs)   * yawScale;

    switch (ACTIVE_PROFILE)
    {
    case GEARBOX_2VTAIL_1RUD:
    case GEARBOX_1MOT_2VTAIL:
    case GEARBOX_1MOT_2VTAIL_1RUD:
        {
            // V-tail: elevon mix on the two tail surfaces.
            float leftMix  = aileronNorm + elevatorNorm;
            float rightMix = aileronNorm - elevatorNorm;
            if (leftMix  > 1.0f) leftMix  = 1.0f; else if (leftMix  < -1.0f) leftMix  = -1.0f;
            if (rightMix > 1.0f) rightMix = 1.0f; else if (rightMix < -1.0f) rightMix = -1.0f;
            _f[SF_VTAIL_LEFT]  = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + leftMix  * 500.0f + rollPidUs + pitchPidUs) + servoTrimUs[SF_VTAIL_LEFT]);
            _f[SF_VTAIL_RIGHT] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + rightMix * 500.0f - rollPidUs + pitchPidUs) + servoTrimUs[SF_VTAIL_RIGHT]);
        }
        break;
    case GEARBOX_1ELE_1RUD:
    case GEARBOX_1MOT_1ELE_1RUD:
        {
            // Traditional tail: separate elevator + rudder (no elevon mix).
            float elev = elevatorNorm;
            if (elev > 1.0f) elev = 1.0f; else if (elev < -1.0f) elev = -1.0f;
            _f[SF_ELEVATOR] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + elev * 500.0f + pitchPidUs) + servoTrimUs[SF_ELEVATOR]);
        }
        break;
    case GEARBOX_1MOT_1RUD_TAIL3:
        _gearboxTailRotation3(rollUs, pitchUs, yawUs);
        break;
    case GEARBOX_1MOT_1RUD_TAIL11:
        _gearboxTailRotation1(rollUs, pitchUs, yawUs);
        break;
    case GEARBOX_1MOT_1RUD_TAILMERGED:
        _gearboxTailMerged(rollUs, pitchUs, yawUs);
        break;
    case GEARBOX_1MOT_1RUD_ELEVON:
        _gearboxTailElevon(rollUs, pitchUs, yawUs);
        break;
    case GEARBOX_1MOT_FLATTAIL:
        _gearboxTailFlat(rollUs, pitchUs, yawUs);
        break;
    case ZWING_9PWM:
        // Airbrake rides CH6 (the flap-frequency channel), which is otherwise
        // unused in the non-flapping gearbox kernel. 0..1 (retracted → full).
        _gearboxZwing(rollUs, pitchUs, yawUs, (_crsfToNorm(voiceFreq) + 1.0f) * 0.5f);
        break;
    default:
        break;
    }
}

// ─── 3-servo rotation tail: pitch + 2 rotation servos ─────────────────
void Ornithopter::_gearboxTailRotation3(float rollUs, float pitchUs, float yawUs)
{
    // Rotation (differential) = roll. Yaw rides the SAME differential but is
    // gated by the tail's live pitch: neutral pitch → rotation is pure roll;
    // pitched → rotation also yaws, and the yaw sign inverts with pitch sign.
    const float pitchNorm  = pitchUs * (1.0f / 500.0f);
    const float coupling   = pitchNorm * (tailYawCoupling * 0.01f);
    const float differential = rollUs + yawUs * coupling;
    _f[SF_TAIL_PITCH]  = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + pitchUs) + servoTrimUs[SF_TAIL_PITCH]);
    _f[SF_TAIL_ROLL_L] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + differential) + servoTrimUs[SF_TAIL_ROLL_L]);
    _f[SF_TAIL_ROLL_R] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US - differential) + servoTrimUs[SF_TAIL_ROLL_R]);
}

// ─── 1-pitch + 1-rotation tail (same physics, single rotation servo) ──
void Ornithopter::_gearboxTailRotation1(float rollUs, float pitchUs, float yawUs)
{
    const float pitchNorm  = pitchUs * (1.0f / 500.0f);
    const float coupling   = pitchNorm * (tailYawCoupling * 0.01f);
    const float differential = rollUs + yawUs * coupling;
    _f[SF_TAIL_PITCH]  = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + pitchUs) + servoTrimUs[SF_TAIL_PITCH]);
    _f[SF_TAIL_ROLL_L] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + differential) + servoTrimUs[SF_TAIL_ROLL_L]);
}

// ─── Merged mechanical tail: 2 servos, both pitch + rotation ──────────
void Ornithopter::_gearboxTailMerged(float rollUs, float pitchUs, float yawUs)
{
    // A linkage merges pitch and rotation so both servos contribute to both
    // (distributing the tail load). Pitch = common mode; rotation = differential.
    // Same negative-elevator yaw inversion as the rotation tails.
    const float pitchNorm  = pitchUs * (1.0f / 500.0f);
    const float coupling   = pitchNorm * (tailYawCoupling * 0.01f);
    const float differential = rollUs + yawUs * coupling;
    _f[SF_TAIL_PITCHROT_L] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + pitchUs + differential) + servoTrimUs[SF_TAIL_PITCHROT_L]);
    _f[SF_TAIL_PITCHROT_R] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + pitchUs - differential) + servoTrimUs[SF_TAIL_PITCHROT_R]);
}

// ─── 2-servo split/elevon tail (di/anhedral slider) ───────────────────
void Ornithopter::_gearboxTailElevon(float rollUs, float pitchUs, float yawUs)
{
    // Pitch = common mode; roll = differential. tailDihedralMix (−100…+100)
    // maps the surface dihedral/anhedral angle to a yaw coupling: 0 = flat
    // (pure elevon, no yaw), + = dihedral (yaw rides the roll differential),
    // − = anhedral (yaw rides it inverted). Magnitude scales with the angle
    // and the surface's size ratio, which is why it is a runtime slider.
    const float dihedral     = tailDihedralMix * 0.01f;   // −1..+1
    const float common       = pitchUs;
    const float differential = rollUs + yawUs * dihedral;
    _f[SF_ELEVON_L] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + common + differential) + servoTrimUs[SF_ELEVON_L]);
    _f[SF_ELEVON_R] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + common - differential) + servoTrimUs[SF_ELEVON_R]);
}

// ─── Merged flat tail: elevon ↔ birdlike-yaw morph ───────────────────
void Ornithopter::_gearboxTailFlat(float rollUs, float pitchUs, float yawUs)
{
    // A flat tail with 2 servos (merged mechanical linkage). Above the pitch
    // threshold it is a pure flat elevon split: pitch = common mode, roll =
    // differential, ZERO yaw (dihedral 0). Below the threshold — negative
    // elevator, the nose-down / tucked-tail regime — the tail morphs into a
    // birdlike vertical surface: yaw rides the roll differential scaled by the
    // live pitch deflection, inverting with pitch sign exactly like the
    // rotation tails. The morph ramps smoothly so crossing the threshold never
    // jumps a servo.
    const float pitchNorm  = pitchUs * (1.0f / 500.0f);
    const float threshold  = -(tailFlatYawThreshold * 0.01f);   // −1..0
    float coupling = 0.0f;
    if (pitchNorm < threshold)
    {
        // t = 0 at the threshold → 1 at full nose-down (pitchNorm = −1).
        const float span = threshold + 1.0f;
        float t = (span > 1e-6f) ? ((threshold - pitchNorm) / span) : 0.0f;
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
        coupling = t * pitchNorm * (tailYawCoupling * 0.01f);
    }
    const float common       = pitchUs;
    const float differential = rollUs + yawUs * coupling;
    _f[SF_TAIL_PITCHROT_L] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + common + differential) + servoTrimUs[SF_TAIL_PITCHROT_L]);
    _f[SF_TAIL_PITCHROT_R] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + common - differential) + servoTrimUs[SF_TAIL_PITCHROT_R]);
}

// ─── Z-Wing 9-PWM complete mixer ──────────────────────────────────────
void Ornithopter::_gearboxZwing(float rollUs, float pitchUs, float yawUs, float airbrake01)
{
    // Authority matrix — fractions of the ±500 µs full deflection. These are
    // the "fine tuned" constants for the Z-Wing airframe (tune per bird):
    //   ailerons      ← roll (primary, 75%)
    //   tail rotation ← roll (secondary, 35%) + pitch-gated yaw
    //   drag rudders  ← yaw (differential, 60%) + airbrake (common, 300 µs max)
    //   head rudder   ← yaw (already mixed via rudderYawWeight in the caller)
    //   tail pitch    ← pitch (full)
    //   motor         ← throttle (already mixed in the caller)

    // Wing ailerons (roll, differential).
    _f[SF_WING_AIL_L] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + rollUs * 0.75f) + servoTrimUs[SF_WING_AIL_L]);
    _f[SF_WING_AIL_R] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US - rollUs * 0.75f) + servoTrimUs[SF_WING_AIL_R]);

    // Split drag rudders: airbrake (common spread) + yaw (differential spread).
    const float dragCommon = airbrake01 * 300.0f;    // symmetric brake, max 300 µs
    const float dragDiff   = yawUs * 0.60f;          // yaw authority on the drag rudders
    _f[SF_WING_DRAG_L] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + dragCommon + dragDiff) + servoTrimUs[SF_WING_DRAG_L]);
    _f[SF_WING_DRAG_R] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + dragCommon - dragDiff) + servoTrimUs[SF_WING_DRAG_R]);

    // Tail (3-servo rotation): roll (secondary) + pitch-gated yaw.
    const float pitchNorm    = pitchUs * (1.0f / 500.0f);
    const float coupling     = pitchNorm * (tailYawCoupling * 0.01f);
    const float differential = rollUs * 0.35f + yawUs * coupling;
    _f[SF_TAIL_PITCH]  = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + pitchUs) + servoTrimUs[SF_TAIL_PITCH]);
    _f[SF_TAIL_ROLL_L] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US + differential) + servoTrimUs[SF_TAIL_ROLL_L]);
    _f[SF_TAIL_ROLL_R] = _clampServo((int32_t)((float)ORNI_SERVO_CENTER_US - differential) + servoTrimUs[SF_TAIL_ROLL_R]);
}