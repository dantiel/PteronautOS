#include "Ornithopter.h"

// ─────────────────────────────────────────────────────────────────────
//  STABILIZER BRIDGE — Mesozoic 2-wing corrections → mixer terms
// ─────────────────────────────────────────────────────────────────────
// The mixer asks each axis for ONE clamped scalar; the axis owns its own
// build-variant (MESOZOIC_ONLY rate bridge vs legacy angle bridge) and its
// own scaling/clamping. Zero when Zephyrus is compiled out or a gain is 0.
//
// Centre terms (pitch/roll) are ATTITUDE (slow level) + RATE (fast damping)
// on the same axis — they shift the wing CENTRE, so they work in glide AND
// flap. The flap-only amplitude/ferocity/skew axes have their own bridges.

namespace {
    inline float clampSymmetric(float v, float limit) {
        if (v >  limit) return  limit;
        if (v < -limit) return -limit;
        return v;
    }
}

float Ornithopter::_stabPitchCenter()
{
#ifdef ZEPHYRUS_ENABLED
    // Centre channel = ATTITUDE (accel, slow level hold) + RATE (gyro, fast
    // damping) summed on the SAME axis. Under MESOZOIC_ONLY the attitude error
    // comes from the HORIZON layer (0 in the rate-only brain) and the rate
    // error from the inner mesoPid; the Mahony path keeps its legacy deg
    // correction alone.
#ifdef MESOZOIC_ONLY
    return clampSymmetric(
        gyroPitchAttitudeCorrection * (wingPitchGain * 0.01f) * ZEPHYR_WING_PITCH_CENTER_SCALE
      + gyroPitchCorrection         * (wingPitchGain * 0.01f) * ZEPHYR_WING_PITCH_RATE_CENTER_SCALE,
        ZEPHYR_WING_PITCH_CENTER_CLAMP);
#else
    return clampSymmetric(
        gyroPitchCorrection * (wingPitchGain * 0.01f) * ZEPHYR_WING_PITCH_CENTER_SCALE,
        ZEPHYR_WING_PITCH_CENTER_CLAMP);
#endif
#else
    return 0.0f;
#endif
}

float Ornithopter::_stabRollCenter()
{
#ifdef ZEPHYRUS_ENABLED
    // Roll centre (deg): symmetric aileron-centre offset. ATTITUDE term levels
    // the wings (slow), RATE term damps the tumble (fast). Both work in flap
    // AND glide — the flap-only roll amplitude axis is a third, separate
    // bridge. This is the same two-actuator roll the pilot gets from aileronCmd
    // + aileron amplitude differential.
#ifdef MESOZOIC_ONLY
    return clampSymmetric(
        gyroRollAttitudeCorrection * (wingRollGain * 0.01f) * ZEPHYR_WING_ROLL_CENTER_SCALE
      + gyroRollCorrection         * (wingRollGain * 0.01f) * ZEPHYR_WING_ROLL_RATE_CENTER_SCALE,
        ZEPHYR_WING_ROLL_CENTER_CLAMP);
#else
    return clampSymmetric(
        gyroRollCorrection * (wingRollGain * 0.01f) * ZEPHYR_WING_ROLL_CENTER_SCALE,
        ZEPHYR_WING_ROLL_CENTER_CLAMP);
#endif
#else
    return 0.0f;
#endif
}

float Ornithopter::_stabPitchFerocity()
{
#ifdef ZEPHYRUS_ENABLED
#ifdef MESOZOIC_ONLY
    // Mesozoic pitch stabilizer → ferocity (primary pitch channel):
    // symmetric downstroke/upstroke dwell split. Both wings share the SAME
    // half-stroke boost (common mode), producing pitch torque — the
    // flapping analogue of the elevator, no discrete surface. Sign mirrors
    // the elevator stick: positive correction (nose-up command) → downstroke
    // dwell (climb), negative → upstroke dwell (dive). The mixer splits this
    // into downstroke (+half) and upstroke (−half) boosts.
    return clampSymmetric(
        gyroPitchCorrection * (wingPitchFerGain * 0.01f) * ZEPHYR_WING_PITCH_RATE_FER_SCALE,
        ZEPHYR_WING_PITCH_RATE_FER_CLAMP);
#endif
#endif
    return 0.0f;
}

float Ornithopter::_stabYawFerocity()
{
#ifdef ZEPHYRUS_ENABLED
    // Mesozoic yaw stabilizer: gyro yaw-rate correction → differential
    // ferocity (asymmetric drag). Rides the same axis as the rudder stick
    // coupling, so a yaw disturbance drags one wing harder than the
    // other to arrest rotation. 0 gain = axis off.
#ifdef MESOZOIC_ONLY
    return clampSymmetric(
        gyroYawCorrection * (wingYawGain * 0.01f) * ZEPHYR_WING_YAW_RATE_FER_SCALE,
        ZEPHYR_WING_YAW_RATE_FER_CLAMP);
#else
    return clampSymmetric(
        gyroYawCorrection * (wingYawGain * 0.01f) * ZEPHYR_WING_YAW_FER_SCALE,
        ZEPHYR_WING_YAW_FER_CLAMP);
#endif
#else
    return 0.0f;
#endif
}

float Ornithopter::_stabPitchSkew()
{
#ifdef ZEPHYRUS_ENABLED
#ifdef MESOZOIC_ONLY
    // PITCH skew (symmetric): both wings front-load the downstroke and
    // back-load the upstroke, shifting thrust timing to level the nose.
    return clampSymmetric(
        gyroPitchAttitudeCorrection * (wingPitchSkewGain * 0.01f) * ZEPHYR_WING_PITCH_ATT_SKEW_SCALE,
        ZEPHYR_WING_PITCH_ATT_SKEW_CLAMP);
#endif
#endif
    return 0.0f;
}

float Ornithopter::_stabRollYawSkew()
{
#ifdef ZEPHYRUS_ENABLED
#ifdef MESOZOIC_ONLY
    // ROLL/YAW skew (differential): L/R skew asymmetry drives a transient
    // moment each half-stroke via the wing's inertial reaction (F = -m*a —
    // skew reshapes the acceleration profile, i.e. the momentary inertial
    // direction). Roll uses the accelerometer attitude tendency (level hold);
    // yaw uses the gyro rate (no magnetometer, so no yaw attitude reference).
    const float rollSkew = clampSymmetric(
        gyroRollAttitudeCorrection * (wingRollSkewGain * 0.01f) * ZEPHYR_WING_ROLL_ATT_SKEW_SCALE,
        ZEPHYR_WING_ROLL_ATT_SKEW_CLAMP);
    const float yawSkew = clampSymmetric(
        gyroYawCorrection * (wingYawSkewGain * 0.01f) * ZEPHYR_WING_YAW_SKEW_SCALE,
        ZEPHYR_WING_YAW_SKEW_CLAMP);
    return rollSkew + yawSkew;
#endif
#endif
    return 0.0f;
}

float Ornithopter::_stabRollAmplitude()
{
#ifdef ZEPHYRUS_ENABLED
    // Mesozoic roll stabilizer: gyro roll correction → differential flap
    // amplitude. This is the aerodynamically effective roll axis (a roll
    // perturbation enlarges one stroke and shrinks the other, producing a
    // correcting torque). 0 gain = axis off.
#ifdef MESOZOIC_ONLY
    return clampSymmetric(
        gyroRollCorrection * (wingRollGain * 0.01f) * ZEPHYR_WING_ROLL_RATE_AMP_SCALE,
        ZEPHYR_WING_ROLL_RATE_AMP_CLAMP);
#else
    return clampSymmetric(
        gyroRollCorrection * (wingRollGain * 0.01f) * ZEPHYR_WING_ROLL_AMP_SCALE,
        ZEPHYR_WING_ROLL_AMP_CLAMP);
#endif
#else
    return 0.0f;
#endif
}
