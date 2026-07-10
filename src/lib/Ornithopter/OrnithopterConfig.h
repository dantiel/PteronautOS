#pragma once
/*
  Ornithopter Configuration Parameters
  Will be persisted in EEPROM / Web UI eventually.
  Compile-time defaults matching GralhaAzul v1.29.0.
*/

#include <cstdint>

// --- Servo Assignments (PWM output indices) ---
#define ORNITHOPTER_SERVO_LEFT    0
#define ORNITHOPTER_SERVO_RIGHT   1
#define ORNITHOPTER_SERVO_RUDDER  2

// --- CRSF Channel Indices (0-based into ChannelData[]) ---
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

// --- Servo limits ---
#define ORNI_SERVO_CENTER_US    1500
#define ORNI_SERVO_MIN_US       988
#define ORNI_SERVO_MAX_US       2012

// --- Flapping threshold (CRSF µs) ---
#define ORNI_FLAP_THRESHOLD_US  1040
#define ORNI_FLAP_HYSTERESIS_US 50

// --- Amplitude scaling ---
#define ORNI_MAGNITUDE_SCALE    0.04f

// --- Waveform ferocity range ---
#define ORNI_FEROCITY_MIN       1.0f
#define ORNI_FEROCITY_MAX       8.0f

// --- Steering ---
#define ORNI_DIFFERENTIAL_MIN  -4.0f
#define ORNI_DIFFERENTIAL_MAX   4.0f
#define ORNI_ELEVATOR_SCALE     0.06f
#define ORNI_AILERON_SCALE      0.04f
#define ORNI_ANGULAR_MULTIPLIER 2.0f

// --- Neutral servo angle (servo.write() degrees) ---
#define ORNI_NEUTRAL_ANGLE_DEG  100
#define ORNI_GLIDE_ANGLE_DEG    -4

// --- Cadence ---
#define ORNI_CYCLE_RATING       0.070f

// --- Front Rudder ---
#define ORNI_RUDDER_CENTER_US   1500
#define ORNI_RUDDER_MIX_SCALE   0.25f   // aileron→rudder bleed for coordinated turns