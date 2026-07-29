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

constexpr MixerProfile ACTIVE_PROFILE = (MixerProfile)MIXER_PROFILE;
constexpr bool PROFILE_IS_GEARBOX = (ACTIVE_PROFILE >= GEARBOX_2VTAIL_1RUD);

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
    // 0: SERVO_2WING
    { 2, { SF_LEFT_WING, SF_RIGHT_WING, SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE } },
    // 1: SERVO_2WING_1RUD
    { 3, { SF_LEFT_WING, SF_RIGHT_WING, SF_RUDDER, SF_NONE, SF_NONE, SF_NONE, SF_NONE } },
    // 2: SERVO_4WING — front wings idx0,1; back wings idx3,4
    { 4, { SF_LEFT_WING, SF_RIGHT_WING, SF_NONE, SF_BACK_LEFT_WING, SF_BACK_RIGHT_WING, SF_NONE, SF_NONE } },
    // 3: GEARBOX_2VTAIL_1RUD
    { 3, { SF_RUDDER, SF_NONE, SF_NONE, SF_VTAIL_LEFT, SF_VTAIL_RIGHT, SF_NONE, SF_NONE } },
    // 4: GEARBOX_1MOT_2VTAIL
    { 3, { SF_MOTOR, SF_NONE, SF_NONE, SF_VTAIL_LEFT, SF_VTAIL_RIGHT, SF_NONE, SF_NONE } },
    // 5: GEARBOX_1MOT_2VTAIL_1RUD
    // PWM indices used: 0(GPIO0)=Rudder, 3(GPIO9)=Motor, 4(GPIO10)=VTAIL L, 6(GPIO16)=VTAIL R
    // GPIO5=radio_busy; GPIO1,3=I2C excluded
    { 4, { SF_RUDDER, SF_NONE, SF_NONE, SF_MOTOR, SF_VTAIL_LEFT, SF_NONE, SF_VTAIL_RIGHT } },
    // 6: GEARBOX_1ELE_1RUD
    { 2, { SF_RUDDER, SF_NONE, SF_NONE, SF_ELEVATOR, SF_NONE, SF_NONE, SF_NONE } },
    // 7: GEARBOX_1MOT_1ELE_1RUD
    { 3, { SF_RUDDER, SF_NONE, SF_NONE, SF_MOTOR, SF_ELEVATOR, SF_NONE, SF_NONE } },
};

inline constexpr const ProfileDesc& PROFILE = PROFILES[ACTIVE_PROFILE];

// ─── CRSF Channel Indices (0-based into ChannelData[]) ─────────────
#define ORNI_CH_AILERON         0
#define ORNI_CH_ELEVATOR        1
#define ORNI_CH_THROTTLE        2
#define ORNI_CH_RUDDER          3
#define ORNI_CH_ARM             4
#define ORNI_CH_RUDDER_FEROCITY 5
#define ORNI_CH_STROKE_FEROCITY 6
#define ORNI_CH_RETURN_FEROCITY 7
#define ORNI_CH_CADENCE         8
#define ORNI_CH_ALTITUDE_HOLD   9

// ─── Servo limits ──────────────────────────────────────────────────
#define ORNI_SERVO_CENTER_US    1500
#define ORNI_SERVO_MIN_US       988
#define ORNI_SERVO_MAX_US       2012

// ─── Flapping threshold (CRSF µs) ──────────────────────────────────
#define ORNI_FLAP_THRESHOLD_US  1040
#define ORNI_FLAP_HYSTERESIS_US 50

// ─── Amplitude scaling ─────────────────────────────────────────────
#define ORNI_MAGNITUDE_SCALE    0.04f

// ─── Waveform ferocity range ───────────────────────────────────────
#define ORNI_FEROCITY_MIN       1.0f
#define ORNI_FEROCITY_MAX       8.0f

// ─── Steering ──────────────────────────────────────────────────────
#define ORNI_DIFFERENTIAL_MIN  -4.0f
#define ORNI_DIFFERENTIAL_MAX   4.0f
#define ORNI_ELEVATOR_SCALE     0.06f
#define ORNI_AILERON_SCALE      0.04f
#define ORNI_ANGULAR_MULTIPLIER 2.0f

// ─── Neutral servo angle (servo.write() degrees) ───────────────────
#define ORNI_NEUTRAL_ANGLE_DEG  100
#define ORNI_GLIDE_ANGLE_DEG    -4

// ─── Cadence ───────────────────────────────────────────────────────
#define ORNI_CYCLE_RATING       0.070f

// ─── Rudder ────────────────────────────────────────────────────────
#define ORNI_RUDDER_CENTER_US   1500
#define ORNI_RUDDER_YAW_WEIGHT  0.65f   // head rudder: yaw component
#define ORNI_RUDDER_ROLL_WEIGHT 0.35f   // head rudder: roll component