#pragma once
/*
  Ornithopter Configuration Parameters + Mixer Profile System

  BUILD FLAGS (in platformio.ini):
    -D MIXER_PROFILE=1   →  SERVO_2WING_1RUD (default, 3-servo waveform)
    -D MIXER_PROFILE=5   →  GEARBOX_1MOT_2VTAIL_1RUD (4-servo gearbox)
    -D MIXER_PROFILE=7   →  GEARBOX_1MOT_1ELE_1RUD (3-servo, traditional tail)

  Legacy: -D ORNITHOPTER_GEARBOX=1 still works → defaults to GEARBOX_1MOT_2VTAIL_1RUD.
*/

#include <cstdint>

// ─── Mixer Profile Enum ───────────────────────────────────────────
enum MixerProfile : uint8_t {
    SERVO_2WING             = 0,  // 2 servo: left wing + right wing (waveform)
    SERVO_2WING_1RUD        = 1,  // 3 servo: left wing, right wing, crest rudder (waveform)
    SERVO_4WING             = 2,  // 4 servo: front L/R + back L/R flapping wings (waveform)
    GEARBOX_2VTAIL_1RUD     = 3,  // 3 servo: V-tail L/R + head rudder (gearbox)
    GEARBOX_1MOT_2VTAIL     = 4,  // 3 servo: motor + V-tail L/R (gearbox)
    GEARBOX_1MOT_2VTAIL_1RUD= 5,  // 4 servo: rudder, motor, V-tail L/R (gearbox)
    GEARBOX_1ELE_1RUD       = 6,  // 2 servo: traditional elevator + rudder (gearbox)
    GEARBOX_1MOT_1ELE_1RUD  = 7,  // 3 servo: motor + elevator + rudder (gearbox)
    PROFILE_COUNT
};

// Backward compat + build-flag resolution
#ifndef MIXER_PROFILE
  #ifdef ORNITHOPTER_GEARBOX
    #define MIXER_PROFILE GEARBOX_1MOT_2VTAIL_1RUD
  #else
    #define MIXER_PROFILE SERVO_2WING_1RUD
  #endif
#endif

// Runtime-selectable active profile. Defaults to the MIXER_PROFILE build flag,
// but can be changed at runtime via the WebUI (see setOrnithopterProfile).
extern MixerProfile activeProfile;
void setOrnithopterProfile(uint8_t p);

#define ACTIVE_PROFILE activeProfile
#define PROFILE_IS_GEARBOX (activeProfile >= GEARBOX_2VTAIL_1RUD)

// ─── Servo Function Tags ───────────────────────────────────────────
enum ServoFunc : uint8_t {
    SF_NONE            = 0,
    SF_LEFT_WING       = 1,
    SF_RIGHT_WING      = 2,
    SF_RUDDER          = 3,
    SF_MOTOR           = 4,
    SF_VTAIL_LEFT      = 5,   // V-tail: elevator+rudder combined
    SF_VTAIL_RIGHT     = 6,   // V-tail: elevator−rudder combined
    SF_ELEVATOR        = 7,   // traditional separate elevator
    SF_BACK_LEFT_WING  = 8,   // rear wing pair (4-wing models)
    SF_BACK_RIGHT_WING = 9,
    SF_COUNT           = 10
};

// ─── Profile Descriptor ────────────────────────────────────────────
struct ProfileDesc {
    uint8_t servoCount;
    uint8_t funcMap[7];   // PWM index → ServoFunc
};

constexpr ProfileDesc PROFILES[PROFILE_COUNT] = {
    // Servos are mapped to PWM channels 4/5/6 (GPIO9/GPIO10/GPIO5) so CH1 (BOOT),
    // CH2 (I2C SDA) and CH3 (I2C SCL) stay free for boot/I2C. funcMap is indexed
    // by PWM output index (0-based): index 3 = CH4, 4 = CH5, 5 = CH6.
    // 0: SERVO_2WING
    { 2, { SF_NONE, SF_NONE, SF_NONE, SF_LEFT_WING, SF_RIGHT_WING, SF_NONE, SF_NONE } },
    // 1: SERVO_2WING_1RUD
    { 3, { SF_NONE, SF_NONE, SF_NONE, SF_LEFT_WING, SF_RIGHT_WING, SF_RUDDER, SF_NONE } },
    // 2: SERVO_4WING — 4 servos; only 3 PWM pins available (back-right dropped)
    { 4, { SF_NONE, SF_NONE, SF_NONE, SF_LEFT_WING, SF_RIGHT_WING, SF_BACK_LEFT_WING, SF_NONE } },
    // 3: GEARBOX_2VTAIL_1RUD
    { 3, { SF_NONE, SF_NONE, SF_NONE, SF_RUDDER, SF_VTAIL_LEFT, SF_VTAIL_RIGHT, SF_NONE } },
    // 4: GEARBOX_1MOT_2VTAIL
    { 3, { SF_NONE, SF_NONE, SF_NONE, SF_MOTOR, SF_VTAIL_LEFT, SF_VTAIL_RIGHT, SF_NONE } },
    // 5: GEARBOX_1MOT_2VTAIL_1RUD — 4 servos; only 3 PWM pins (vtail-right dropped)
    { 4, { SF_NONE, SF_NONE, SF_NONE, SF_RUDDER, SF_MOTOR, SF_VTAIL_LEFT, SF_NONE } },
    // 6: GEARBOX_1ELE_1RUD
    { 2, { SF_NONE, SF_NONE, SF_NONE, SF_RUDDER, SF_ELEVATOR, SF_NONE, SF_NONE } },
    // 7: GEARBOX_1MOT_1ELE_1RUD
    { 3, { SF_NONE, SF_NONE, SF_NONE, SF_RUDDER, SF_MOTOR, SF_ELEVATOR, SF_NONE } },
};

#define PROFILE PROFILES[activeProfile]

// ─── CRSF Channel Indices (0-based into ChannelData[]) ─────────────
#define ORNI_CH_AILERON         0
#define ORNI_CH_ELEVATOR        1
#define ORNI_CH_THROTTLE        2
#define ORNI_CH_RUDDER          3
#define ORNI_CH_ARM             4
#define ORNI_CH_FREQ            5   // flap frequency modulator channel
#define ORNI_CH_PROFILE         6   // flight profile selector (multi-position)

// ─── Servo limits ──────────────────────────────────────────────────
#define ORNI_SERVO_CENTER_US    1500
#define ORNI_SERVO_MIN_US       988    // default wing servo 0° pulse
#define ORNI_SERVO_MAX_US       2012   // default wing servo 180° pulse

// Absolute hard-safety clamp envelope for ALL servo outputs. The wing sweep
// min/max (Ornithopter::servoMinUs/servoMaxUs) are runtime-configurable via
// the WebUI kernel panel, but never outside this envelope.
#define ORNI_SERVO_ABS_MIN_US   500
#define ORNI_SERVO_ABS_MAX_US   2500
#define ORNI_SERVO_TRIM_US      0      // per-servo correction default (µs, signed)

// ─── Flapping threshold (CRSF µs) ──────────────────────────────────
#define ORNI_FLAP_THRESHOLD_US  303     // 1080µs PWM glide/flap boundary (CRSF raw: 172=988µs, 1811=2012µs)
#define ORNI_FLAP_HYSTERESIS_US 50

// ─── Amplitude scaling ─────────────────────────────────────────────
#define ORNI_MAGNITUDE_SCALE    0.04f

// ─── Waveform ferocity range ───────────────────────────────────────
// GralhaAzul: FEROCIDADE_MINIMA_PADRAO=0, FEROCIDADE_MAXIMA_PADRAO=8.
// Min 0 = pure cosine ramp (no dwell); 8 = pure square wave.
#define ORNI_FEROCITY_MIN       0.0f
#define ORNI_FEROCITY_MAX       8.0f

// ─── Steering ──────────────────────────────────────────────────────
#define ORNI_DIFFERENTIAL_MIN  -4.0f
#define ORNI_DIFFERENTIAL_MAX   4.0f
// Full-stick steering deflection (degrees, pre-multiplier). The runtime
// aileron_scale / elevator_scale WebUI params (0–100) scale this down.
#define ORNI_STEER_MAX_DEG      60.0f
#define ORNI_ANGULAR_MULTIPLIER 2.0f

// ─── Neutral servo angle (servo.write() degrees) ───────────────────
#define ORNI_NEUTRAL_ANGLE_DEG  100
#define ORNI_GLIDE_ANGLE_DEG_DEFAULT    -4

// ─── Flap Frequency Modulator Channel ──────────────────────────────
// GralhaAzul BIRD-LIKE mode: CH6 (1000–2000µs) sets flap rate within the
// window [ORNI_FREQ_MIN … flapBaseFreq]; throttle scales stroke amplitude
// as % of the servo-speed-limited max.
//   servoSpeed   = seconds per 60° stroke (0.025–0.250 s/60°), stored in ms.
//   v_servo      = 60° / servoSpeed          (e.g. 0.070s → 857°/s)
//   A_permitida  = v_servo / (2·f)           (capped at A_max).
// flapBaseFreq (deci-Hz) is the upper bound of the CH6 frequency window:
// raising it raises the reachable flap rate AND tightens the amplitude cap.
#define ORNI_SERVO_SPEED_MS_DEFAULT  70   // 0.070 s/60° (857°/s)
#define ORNI_SERVO_SPEED_MIN_MS  25      // 0.025 s/60° (2400°/s)
#define ORNI_SERVO_SPEED_MAX_MS  250     // 0.250 s/60° (240°/s)
#define ORNI_AMP_MAX             55.0f   // AMPLITUDE_MAXIMA_SERVO_PADRAO (pre-multiplier)
#define ORNI_FLAP_ANGLE_DEG_DEFAULT  0      // per-flight-profile flap stroke centre offset (deg)
#define ORNI_FREQ_MIN            0.5f    // Hz at 1000µs
#define ORNI_FLAP_BASE_FREQ_DHZ_DEFAULT  50   // 5.0 Hz at 2000µs
#define ORNI_FLAP_BASE_FREQ_MIN_DHZ   10   // 1.0 Hz
#define ORNI_FLAP_BASE_FREQ_MAX_DHZ   200  // 20.0 Hz

// Blend the independent CH6 frequency command toward throttle. Inputs and
// output are normalized: 0 = ORNI_FREQ_MIN, 1 = flapBaseFreq. Keeping this
// helper Arduino-free makes the control law directly unit-testable.
constexpr float orniClamp01(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

constexpr float orniThrottleFrequencyCommand(float independentFreq01,
                                              float throttle01,
                                              float couplingPercent) {
    const float mix = orniClamp01(couplingPercent * 0.01f);
    const float independent = orniClamp01(independentFreq01);
    const float throttle = orniClamp01(throttle01);
    return independent + (throttle - independent) * mix;
}

// ─── Flight Profiles (multi-position channel) ──────────────────────
// Up to 3 tuning param sets switchable in flight by the PROFILE channel.
// Kernel (MixerProfile / servo geometry) is NOT per-profile — it stays fixed.
#define FLIGHT_PROFILE_COUNT    3

struct FlightProfileParams {
    float   strokeFerocity;       // 0–100
    float   returnFerocity;       // 0–100
    int8_t  glideAngleDeg;        // -15…+15
        int8_t  flappingAngleDeg;     // -15…+15, flap stroke centre offset (deg)
    float   aileronScale;         // 0–100
    float   elevatorScale;        // 0–100
    float   rudderFerocityRange;  // 0–100
    float   rudderAmplitudeDifferential; // 0–100, rudder → L/R differential flap amplitude
    float   elevatorFerocityMix;  // 0–100, extra ferocity per |elevator| deflection
    float   throttleFerocityMix;  // 0–100, throttle→ferocity coupling (dwell)
    float   throttleFrequencyMix; // 0–100, CH6→throttle frequency-command blend
    float   ferocityShapeMix;     // 0–100, plateau/square → rounded pyramidal
};

// ─── Rudder ────────────────────────────────────────────────────────
#define ORNI_RUDDER_CENTER_US   1500
#define ORNI_RUDDER_YAW_WEIGHT  0.65f   // head rudder: yaw component
#define ORNI_RUDDER_ROLL_WEIGHT 0.35f   // head rudder: roll component

// ─── Cadence/Phase Advance (was Ondas P→Phase) ─────────────────────
#define ORNI_CADENCE_GAIN     20      // 0-100, P-term compresses/decompresses stroke phase
// ─── Ferocity Modulation (was Ondas D→Ferocity) ────────────────────
#define ORNI_FEROCITY_D_GAIN  20      // 0-100, D-term contribution to dwell ratio (PD-blend)
// ─── Balance/Asymmetry Bias (was Ondas I→Asymmetry) ────────────────
#define ORNI_BALANCE_GAIN     10      // 0-100, I-term shifts stroke center bias
// ─── PD-Blend Ferocity ─────────────────────────────────────────────
#define ORNI_FEROCITY_P_GAIN  0       // 0-100, P-term contribution to dwell ratio
// ─── Anchor Damping ────────────────────────────────────────────────
#define ORNI_ANCHOR_GAIN      0       // 0-100, k₂ damping delta (0=k₂=10, 100=k₂=20)
// ─── Resonance Lock-In Amplifier ───────────────────────────────────
#define ORNI_RESONANCE_GAIN   0       // 0-100, phase-locked error accumulation (0=disabled)
// ─── Deferred Fields (defined for future use, not wired) ───────────
#define ORNI_FEROCITY_ROLL_GAIN 0     // 0-100, roll-axis ferocity (Zephyrus no per-axis)
#define ORNI_FEROCITY_YAW_GAIN  0     // 0-100, yaw-axis ferocity (Zephyrus no per-axis)
#define ORNI_WARP_GAIN          0     // 0-100, dual-waveform blend (ESP8285 constrained)
#define ORNI_WARP_YAW_GAIN      0     // 0-100, yaw-axis warp (ESP8285 constrained)

// ─── SSFF: Stroke-Synchronous Feed-Forward ─────────────────────────
// Accumulates pitch error over each half-stroke, biases next stroke.
#define ORNI_SSFF_GAIN         0      // 0-100, 0=disabled

// ─── Aeroelastic PID Gain Modulation ───────────────────────────────
// Dynamically scales Zephyrus pitch PID gains based on stroke phase.
#define ORNI_AERO_GLIDE_COEFF 40      // 0-100, gain modulation during glide
#define ORNI_AERO_FLAP_COEFF  40      // 0-100, gain modulation during flap
