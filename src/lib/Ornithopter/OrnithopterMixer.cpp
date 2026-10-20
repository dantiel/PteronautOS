#include "Ornithopter.h"
#ifdef UNIT_TEST
  #include <stdint.h>
  static inline uint32_t micros() { return 0; }
  static inline void delay(unsigned long) {}
  #define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#else
  #include <Arduino.h>
#endif
#include <cmath>

// ─────────────────────────────────────────────────────────────────────
//  SERVO MIXER — 2-wing waveform kernel (pilot-driven pipeline)
// ─────────────────────────────────────────────────────────────────────
// _computeServoMixer() is a thin orchestrator. The 15 small stage helpers
// each fill one slice of the stack working set (OrniServoMix). Gyro terms
// come from the _stab* bridge (OrnithopterStabilizer.cpp); the Nigredo
// modulation terms come from the _ondas* bridge (OrnithopterOndas.cpp).
// Order matters: each stage reads the fields written by the one before it.

bool Ornithopter::_mixerIsFlapping(float throttleUsF, bool armed)
{
    // Flap/glide hysteresis: once flapping, throttle must fall below the
    // lower bound before glide engages — no chatter at the boundary.
    bool isFlapping;
    if (_wasFlapping) {
        isFlapping = armed && (throttleUsF > (float)(ORNI_FLAP_THRESHOLD_US - ORNI_FLAP_HYSTERESIS_US));
    } else {
        isFlapping = armed && (throttleUsF > (float)ORNI_FLAP_THRESHOLD_US);
    }
    _wasFlapping = isFlapping;
    return isFlapping;
}

void Ornithopter::_mixerSteering(OrniServoMix &m)
{
    // Steering magnitudes scaled by runtime WebUI params (0–100). Sign
    // application happens later — the mirror mount swaps the code axes.
    m.aileronCmd    = m.aileronNorm  * aileronScale  * 0.01f * ORNI_STEER_MAX_DEG;
    m.elevatorCmd   = m.elevatorNorm * elevatorScale * 0.01f * ORNI_STEER_MAX_DEG;
    m.glideCmd      = (float)glideAngleDeg;    // static wing angle (glide only)
    m.flapCenterCmd = (float)flappingAngleDeg; // flap stroke centre offset

    // Mesozoic centre terms (attitude + rate, same axis) — work in glide AND
    // flap. Zero when Zephyrus is compiled out or a gain is 0.
    m.gyroPitchCenter = _stabPitchCenter();
    m.gyroRollCenter  = _stabRollCenter();
}

void Ornithopter::_flapTiming(OrniServoMix &m)
{
    // Frequency command: at 0% coupling CH6 independently commands frequency;
    // at 100% the normalized frequency follows normalized throttle, so
    // frequency and amplitude scale together. Intermediate values blend.
    float independentFreq01 = _crsfToNorm(voiceFreq) * 0.5f + 0.5f;
    m.throttlePct = constrain((m.throttleUsF - (float)ORNI_FLAP_THRESHOLD_US) /
                              (1811.0f - (float)ORNI_FLAP_THRESHOLD_US), 0.0f, 1.0f);
    float freq01 = orniThrottleFrequencyCommand(independentFreq01, m.throttlePct,
                                                throttleFrequencyMix);
    float freqMax = flapBaseFreq * 0.1f;                   // deci-Hz → Hz
    m.freqHz = ORNI_FREQ_MIN + freq01 * (freqMax - ORNI_FREQ_MIN);
    _osc.cadenceTarget = m.freqHz * 6.283185307f;          // 2π rad/s

    uint32_t nowUs = micros();
    if (_lastUpdateUs == 0) _lastUpdateUs = nowUs;
    m.dt = (float)(nowUs - _lastUpdateUs) * 1e-6f;
    if (m.dt > 0.1f) m.dt = 0.1f;
    _lastUpdateUs = nowUs;

    // Cadence P → phase advance + beat-locking stiffness (no-op off-axis).
    _ondasCadenceApply(m.dt);

#if !defined(MUSHIN_ENABLED)
    m.rawWave = _osc.advance(m.dt);
#endif

#if defined(MESOZOIC_HORIZON)
    // Flap phase for the Zephyrus gravity recovery (bridged each tick).
    flapSin = sinf(m.rawWave);
    flapCos = cosf(m.rawWave);
#endif

    _ondasZeroStaleBiases();
}

void Ornithopter::_flapAmplitude(OrniServoMix &m)
{
    // Amplitude = throttle % of the servo-speed-limited max at this freq.
    float degPerSec = 60.0f / (servoSpeed * 0.001f);       // ms/60° → °/s
    float ampMax = degPerSec / (2.0f * m.freqHz);
    if (ampMax > ORNI_AMP_MAX) ampMax = ORNI_AMP_MAX;      // hard safety ceiling
    m.amplitude = m.throttlePct * ampMax;
}

void Ornithopter::_flapElevatorFerocity(OrniServoMix &m)
{
    // Elevator → ferocity: ASYMMETRIC stroke modulation. Elevator-up (norm +1)
    // strengthens the DOWNSTROKE (power stroke); elevator-down (norm -1)
    // strengthens the UPSTROKE (recovery). Each direction boosts one half.
    float elevFerScale = elevatorFerocityMix * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN);
    m.elevUpBoost   = fmaxf( m.elevatorNorm, 0.0f) * elevFerScale;  // climb → downstroke
    m.elevDownBoost = fmaxf(-m.elevatorNorm, 0.0f) * elevFerScale;  // dive  → upstroke

    // Elevator-RATE → ferocity transient (slew) + antigravity. The transient
    // DECAYS toward the current static coupling (a held climb settles on the
    // steady climb ferocity, not neutral). Antigravity is a rate-gated,
    // stick-proportional feed-forward that fades when the stick rests.
    float elevRateKickUp   = 0.0f;
    float elevRateKickDown = 0.0f;
    float antiGravUp       = 0.0f;
    float antiGravDown     = 0.0f;

    if (_prevElevatorNorm < -1.5f) {
        // First flap tick after glide/link: seed the transient at the static
        // coupling so the pre-existing elevator→ferocity mix applies instantly.
        _prevElevatorNorm = m.elevatorNorm;
        _elevFerStroke = m.elevUpBoost;
        _elevFerReturn = m.elevDownBoost;
    } else if (m.dt > 0.0f) {
        float elevRate = (m.elevatorNorm - _prevElevatorNorm) / m.dt;
        _prevElevatorNorm = m.elevatorNorm;
        float alphaRate = m.dt / (ORNI_ELEV_RATE_LPF_TAU + m.dt);
        _elevatorRateLPF += (elevRate - _elevatorRateLPF) * alphaRate;

        elevRateKickUp   = orniElevatorRateFerKick( _elevatorRateLPF, elevatorFerocityRateMix);
        elevRateKickDown = orniElevatorRateFerKick(-_elevatorRateLPF, elevatorFerocityRateMix);

        float gate = fabsf(_elevatorRateLPF) * ORNI_ANTIGRAV_GATE_GAIN;
        if (gate > 1.0f) gate = 1.0f;
        float alphaGate = m.dt / (ORNI_ANTIGRAV_GATE_TAU + m.dt);
        _antiGravGate += (gate - _antiGravGate) * alphaGate;

        antiGravUp   = orniElevatorAntigravity(_antiGravGate,  m.elevatorNorm, elevatorAntigravityMix);
        antiGravDown = orniElevatorAntigravity(_antiGravGate, -m.elevatorNorm, elevatorAntigravityMix);

        float alphaFer = m.dt / (ORNI_ELEV_RATE_TAU + m.dt);
        _elevFerStroke += ((m.elevUpBoost   + elevRateKickUp   + antiGravUp)   - _elevFerStroke) * alphaFer;
        _elevFerReturn += ((m.elevDownBoost + elevRateKickDown + antiGravDown) - _elevFerReturn) * alphaFer;
    }

    // Transient EXTRA ferocity riding on the static coupling (≈0 at rest).
    m.elevFerRateUp   = _elevFerStroke - m.elevUpBoost;    // downstroke dwell kick
    m.elevFerRateDown = _elevFerReturn - m.elevDownBoost;  // upstroke dwell kick

    // Throttle → thrust-shape coupling (one blended knob): thrust aggression
    // drives dwell (square) AND centre (front-load) together.
    m.thrust = orniThrottleThrustShape(m.throttlePct, throttleThrustShapeMix,
                                       throttleThrustExpo);
}

void Ornithopter::_flapFerocities(OrniServoMix &m)
{
    // Ferocity (dwell/shape) = per-profile stroke/return sliders + elevator mix
    // + throttle mix (+ gyro). The Nigredo terms (PD-blend, balance, resonance)
    // come from the _ondas* bridge and are compiled out under MESOZOIC_ONLY.
#if defined(ZEPHYRUS_ENABLED) && !defined(MESOZOIC_ONLY)
    const float ferocitySignal = _ondasFerocityBlend(m.dt);  // inertial harmonizer
    const float iBias          = _ondasBalanceBias();        // I → asymmetry
    const float resonanceBias  = _ondasResonanceBias();      // lock-in pump
    m.strokeFer = ORNI_FEROCITY_MIN + strokeFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN)
                + ferocitySignal + iBias + _ssffFerocityUpBias + resonanceBias
                + m.elevUpBoost + m.elevFerRateUp + m.thrust.dwellBoost;
    m.returnFer = ORNI_FEROCITY_MIN + returnFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN)
                + ferocitySignal - iBias + _ssffFerocityDownBias + resonanceBias
                + m.elevDownBoost + m.elevFerRateDown + m.thrust.dwellBoost;
#else
    m.strokeFer = ORNI_FEROCITY_MIN + strokeFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN)
                + m.elevUpBoost + m.elevFerRateUp + m.thrust.dwellBoost;
    m.returnFer = ORNI_FEROCITY_MIN + returnFerocity * 0.01f * (ORNI_FEROCITY_MAX - ORNI_FEROCITY_MIN)
                + m.elevDownBoost + m.elevFerRateDown + m.thrust.dwellBoost;
#endif

    // Mesozoic pitch stabilizer → ferocity (primary pitch channel, MESOZOIC_ONLY):
    // symmetric downstroke/upstroke dwell split (common mode). Positive
    // correction (nose-up) → downstroke dwell (climb), negative → upstroke.
#ifdef MESOZOIC_ONLY
    const float gyroPitchFer = _stabPitchFerocity();
    m.strokeFer += fmaxf( gyroPitchFer, 0.0f);  // climb → downstroke dwell
    m.returnFer += fmaxf(-gyroPitchFer, 0.0f);  // dive  → upstroke dwell
#endif
}

void Ornithopter::_flapRudderDifferential(OrniServoMix &m)
{
    // Yaw stick → L/R wing differential ferocity, scaled by rudder_ferocity_range.
    m.rudderFer = _crsfToNorm(voiceRudder) * rudderFerocityRange * 0.01f * ORNI_DIFFERENTIAL_MAX;

    // Yaw stick → L/R differential flap amplitude, scaled by
    // rudder_amplitude_differential. The aileron roll axis joins this same
    // differential before the stroke scales are computed.
    m.rudderAmpDiff = _crsfToNorm(voiceRudder) * rudderAmplitudeDifferential * 0.01f;

    m.strokeFerL = m.strokeFer + m.rudderFer;
    m.strokeFerR = m.strokeFer - m.rudderFer;
    m.returnFerL = m.returnFer + m.rudderFer;
    m.returnFerR = m.returnFer - m.rudderFer;

    // Mesozoic yaw stabilizer: gyro yaw-rate → differential ferocity
    // (asymmetric drag). Rides the same axis as the rudder stick coupling.
    const float gyroYawFer = _stabYawFerocity();
    m.strokeFerL += gyroYawFer; m.strokeFerR -= gyroYawFer;
    m.returnFerL += gyroYawFer; m.returnFerR -= gyroYawFer;
}

void Ornithopter::_flapLimiar(OrniServoMix &m)
{
    // Shared reversal threshold computed from the BASE ferocities (before
    // rudder differential), so both wings reverse at the SAME phase even when
    // their per-wing ferocities differ. Faithful to GralhaAzul's limiarBase.
    float fDbase = m.strokeFer; if (fDbase < 0.0f) fDbase = 0.0f; else if (fDbase > 8.0f) fDbase = 8.0f;
    float fSbase = m.returnFer; if (fSbase < 0.0f) fSbase = 0.0f; else if (fSbase > 8.0f) fSbase = 8.0f;
    float wDbase = 8.0f - fDbase; if (wDbase < 0.01f) wDbase = 0.01f;
    float wSbase = 8.0f - fSbase; if (wSbase < 0.01f) wSbase = 0.01f;
    m.limiarShared = 6.283185307f * wDbase / (wDbase + wSbase);

    // SSFF stroke-sync + resonance accumulation (Nigredo path only).
    _ondasStrokeSyncUpdate(m.dt, m.rawWave, m.limiarShared);
}

void Ornithopter::_flapSkewAndRoll(OrniServoMix &m)
{
    // Throttle-RATE → transient boost/brake (slew): the low-passed throttle
    // slew briefly shifts BOTH wave centres. τ decays the kick once the stick
    // rests; the sentinel seeds without kick.
    float throttleRateBoost = 0.0f;
    if (_prevThrottlePct < 0.0f) {
        _prevThrottlePct = m.throttlePct;
    } else if (m.dt > 0.0f) {
        float throttleRate = (m.throttlePct - _prevThrottlePct) / m.dt;
        _prevThrottlePct = m.throttlePct;
        float alpha = m.dt / (ORNI_SKEW_RATE_LPF_TAU + m.dt);
        _throttleRateLPF += (throttleRate - _throttleRateLPF) * alpha;
        throttleRateBoost = orniThrottleSkewRateShift(_throttleRateLPF, throttleSkewRateMix);
    }

    // Skew bridge — phase-local stroke/return timing shift. PITCH skew is
    // symmetric (both wings front-load downstroke, back-load upstroke).
    const float gyroPitchSkew = _stabPitchSkew();
    m.strokeSkewEff = strokeSkew + m.thrust.centreShift + throttleRateBoost + gyroPitchSkew;
    m.returnSkewEff = returnSkew + m.thrust.centreShift + throttleRateBoost - gyroPitchSkew;

    // Aileron → differential flap AMPLITUDE (roll steering). Steady pilot roll
    // lives on the amplitude axis; skew's transient moment is the gyro bridge.
    float aileronRollShift = orniAileronRollShift(m.aileronNorm, aileronSkewMix);

    // Aileron-RATE → transient differential flap amplitude kick (slew).
    float aileronRateBoost = 0.0f;
    if (_prevAileronNorm < -1.5f) {
        _prevAileronNorm = m.aileronNorm;
    } else if (m.dt > 0.0f) {
        float aileronRate = (m.aileronNorm - _prevAileronNorm) / m.dt;
        _prevAileronNorm = m.aileronNorm;
        float alpha = m.dt / (ORNI_SKEW_RATE_LPF_TAU + m.dt);
        _aileronRateLPF += (aileronRate - _aileronRateLPF) * alpha;
        aileronRateBoost = orniAileronRollRateShift(_aileronRateLPF, aileronSkewRateMix);
    }

    // Roll torque = rudder (yaw) + aileron (static + slew) + gyro roll.
    float rollAmpDiff = m.rudderAmpDiff + aileronRollShift + aileronRateBoost;
    rollAmpDiff += _stabRollAmplitude();
    if (rollAmpDiff > 0.9f) rollAmpDiff = 0.9f;
    if (rollAmpDiff < -0.9f) rollAmpDiff = -0.9f;
    m.amplitudeL = m.amplitude * (1.0f + rollAmpDiff);
    m.amplitudeR = m.amplitude * (1.0f - rollAmpDiff);
}

void Ornithopter::_flapPulses(OrniServoMix &m)
{
#if defined(MUSHIN_ENABLED)
    // Compile the entire pilot-driven motion on EP2, but never evaluate phase
    // or wave samples here — the RP2040 owns the only running oscillator.
    Motion::prepare(motionIntent, m.limiarShared, m.strokeFerL, m.returnFerL,
                    m.strokeFerR, m.returnFerR, ferocityShapeMix,
                    m.strokeSkewEff, m.returnSkewEff);
    motionIntent.flapping = 1;
    motionIntent.hz = m.freqHz;
    motionIntent.centre[0] = ORNI_NEUTRAL_ANGLE_DEG + (m.aileronCmd + m.elevatorCmd + m.flapCenterCmd + m.gyroPitchCenter + m.gyroRollCenter) * ORNI_ANGULAR_MULTIPLIER;
    motionIntent.centre[1] = ORNI_NEUTRAL_ANGLE_DEG + (m.aileronCmd - m.elevatorCmd - m.flapCenterCmd - m.gyroPitchCenter + m.gyroRollCenter) * ORNI_ANGULAR_MULTIPLIER;
    motionIntent.amplitude[0] = -m.amplitudeL * ORNI_ANGULAR_MULTIPLIER;
    motionIntent.amplitude[1] =  m.amplitudeR * ORNI_ANGULAR_MULTIPLIER;
    m.degL = 0.0f;
    m.degR = 0.0f;
#else
    // Differential skew bridge (roll + yaw): L/R skew asymmetry kicks a
    // transient moment each half-stroke via the inertial reaction (F = -m*a).
    const float diffSkew = _stabRollYawSkew();
    const float pulseL = FlappingOscillator::shapeWave(m.rawWave, m.strokeFerL, m.returnFerL,
                                                       m.limiarShared, ferocityShapeMix,
                                                       m.strokeSkewEff + diffSkew, m.returnSkewEff - diffSkew);
    const float pulseR = FlappingOscillator::shapeWave(m.rawWave, m.strokeFerR, m.returnFerR,
                                                       m.limiarShared, ferocityShapeMix,
                                                       m.strokeSkewEff - diffSkew, m.returnSkewEff + diffSkew);
    m.degL = m.amplitudeL * pulseL;
    m.degR = m.amplitudeR * pulseR;
#endif
}

void Ornithopter::_flapWaveCache(OrniServoMix &m)
{
    // MUSHIN v1 parameter cache — post-mix (L wing) and symmetric (skew before
    // aileron differential). The spirit streams wave parameters, not servo µs.
    lastThrottlePct = m.throttlePct;
    lastFlapHz = m.freqHz;
    lastStrokeFer = m.strokeFerL;
    lastReturnFer = m.returnFerL;
    lastStrokeSkew = m.strokeSkewEff;
    lastReturnSkew = m.returnSkewEff;
    lastFlapping = true;
}

void Ornithopter::_mixerFlapAngles(OrniServoMix &m, int &angleLeft, int &angleRight)
{
    // Neutral must NOT be scaled by ORNI_ANGULAR_MULTIPLIER — otherwise
    // 100° × 2 = 200° pins the wings at max deflection. Multiplier applies to
    // deviation terms only. Mirror-mounted wing servos: code "common" ==
    // physical differential (roll/aileron), code "differential" == physical
    // common (pitch/flap). Aileron (roll) → common; elevator (pitch) → diff.
    angleLeft  = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (m.aileronCmd + m.elevatorCmd + m.flapCenterCmd + m.gyroPitchCenter + m.gyroRollCenter - m.degL) * ORNI_ANGULAR_MULTIPLIER);
    angleRight = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (m.aileronCmd - m.elevatorCmd - m.flapCenterCmd - m.gyroPitchCenter + m.gyroRollCenter + m.degR) * ORNI_ANGULAR_MULTIPLIER);
}

void Ornithopter::_mixerGlide(OrniServoMix &m, int &angleLeft, int &angleRight)
{
#if defined(MUSHIN_ENABLED)
    motionIntent.flapping = 0;
    motionIntent.hz = 0;
    motionIntent.amplitude[0] = motionIntent.amplitude[1] = 0;
    motionIntent.centre[0] = ORNI_NEUTRAL_ANGLE_DEG + (m.aileronCmd + m.elevatorCmd + m.glideCmd + m.gyroPitchCenter + m.gyroRollCenter) * ORNI_ANGULAR_MULTIPLIER;
    motionIntent.centre[1] = ORNI_NEUTRAL_ANGLE_DEG + (m.aileronCmd - m.elevatorCmd - m.glideCmd - m.gyroPitchCenter + m.gyroRollCenter) * ORNI_ANGULAR_MULTIPLIER;
#else
    _osc.decay(0.0f);
#endif
    _lastUpdateUs = 0;
    _prevThrottlePct = -1.0f;   // sentinel seeds the next flap tick without kick
    lastFlapping = false;
    lastThrottlePct = 0.0f;
    lastFlapHz = 0.0f;
    _throttleRateLPF = 0.0f;
    _prevAileronNorm = -2.0f;   // sentinel seeds without roll slew kick
    _aileronRateLPF = 0.0f;
    _prevElevatorNorm = -2.0f;  // sentinel seeds without fer dwell kick
    _elevatorRateLPF = 0.0f;
    _elevFerStroke = 0.0f;
    _elevFerReturn = 0.0f;
    _antiGravGate = 0.0f;
    _ondasGlidePurge();

    // Glide angle IS applied here as a glide-only static offset (the flap
    // centre offset is applied in the flapping branch, so glide and flap
    // centres are tunable independently per flight profile).
    angleLeft  = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (m.aileronCmd + m.elevatorCmd + m.glideCmd + m.gyroPitchCenter + m.gyroRollCenter) * ORNI_ANGULAR_MULTIPLIER);
    angleRight = (int)((float)ORNI_NEUTRAL_ANGLE_DEG + (m.aileronCmd - m.elevatorCmd - m.glideCmd - m.gyroPitchCenter + m.gyroRollCenter) * ORNI_ANGULAR_MULTIPLIER);
}

void Ornithopter::_mixerWriteWings(int angleLeft, int angleRight)
{
    if (angleLeft  < 0) angleLeft  = 0; else if (angleLeft  > 180) angleLeft  = 180;
    if (angleRight < 0) angleRight = 0; else if (angleRight > 180) angleRight = 180;

    uint16_t usL = (uint16_t)(servoMinUs + (uint32_t)angleLeft  * (servoMaxUs - servoMinUs) / 180);
    uint16_t usR = (uint16_t)(servoMinUs + (uint32_t)angleRight * (servoMaxUs - servoMinUs) / 180);

    _f[SF_LEFT_WING]  = _clampServo((int32_t)usL + servoTrimUs[SF_LEFT_WING]);
    _f[SF_RIGHT_WING] = _clampServo((int32_t)usR + servoTrimUs[SF_RIGHT_WING]);
}

void Ornithopter::_mixerTailOutput(float aileronNorm)
{
    // ── Tail / back wings (profile-dependent) ──
    float rudderMix = (_crsfToNorm(voiceRudder) * rudderYawWeight * 0.01f)
                    + (aileronNorm * rudderRollWeight * 0.01f);
    if (rudderMix > 1.0f) rudderMix = 1.0f;
    if (rudderMix < -1.0f) rudderMix = -1.0f;

    if (ACTIVE_PROFILE == SERVO_4WING) {
        // Back wings mirror front wings (same waveform), with their own trim.
        _f[SF_BACK_LEFT_WING]  = _clampServo((int32_t)_f[SF_LEFT_WING]  + servoTrimUs[SF_BACK_LEFT_WING]);
        _f[SF_BACK_RIGHT_WING] = _clampServo((int32_t)_f[SF_RIGHT_WING] + servoTrimUs[SF_BACK_RIGHT_WING]);
    } else if (PROFILE.servoCount >= 3) {
        // SERVO_2WING_1RUD: crest/head rudder.
        int32_t rudderUs = (int32_t)((float)ORNI_RUDDER_CENTER_US + rudderMix * 500.0f
#ifdef ZEPHYRUS_ENABLED
                                   + gyroRudderCorrection
#endif
                                  ) + servoTrimUs[SF_RUDDER];
        _f[SF_RUDDER] = _clampServo(rudderUs);
    }
}

void Ornithopter::_computeServoMixer()
{
    OrniServoMix m{};  // stack working set, zero-init (rawWave unused under MUSHIN)
    m.aileronNorm  = _crsfToNorm(voiceAileron);
    m.elevatorNorm = _crsfToNorm(voiceElevator);
    m.rudderNorm   = _crsfToNorm(voiceRudder);
    m.throttleUsF  = (float)voiceThrottle;

    bool armed = _isArmed();
    bool isFlapping = _mixerIsFlapping(m.throttleUsF, armed);

#ifdef ZEPHYRUS_ENABLED
    // Aeroelastic PID gain modulation: scale Zephyrus pitch response by the
    // glide/flap coefficient based on flapping state.
    aeroGainScale = isFlapping ? (aeroFlapCoeff * 0.01f) : (aeroGlideCoeff * 0.01f);
#endif

    _mixerSteering(m);

    int angleLeft, angleRight;
    if (isFlapping) {
        _flapTiming(m);
        _flapAmplitude(m);
        _flapElevatorFerocity(m);
        _flapFerocities(m);
        _flapRudderDifferential(m);
        _flapLimiar(m);
        _flapSkewAndRoll(m);
        _flapPulses(m);
        _flapWaveCache(m);
        _mixerFlapAngles(m, angleLeft, angleRight);
    } else {
        _mixerGlide(m, angleLeft, angleRight);
    }

    _mixerWriteWings(angleLeft, angleRight);
    _mixerTailOutput(m.aileronNorm);
}
