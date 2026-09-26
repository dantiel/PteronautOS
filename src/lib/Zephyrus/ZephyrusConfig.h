#pragma once
/*
  Zephyrus Gyro Module — Compile-Time Configuration
  PteronautOS Crest Rudder Stabilization
  MPU6050 register-level I2C + Mahony AHRS + Dual PID
*/

#include <cstdint>

// --- I2C Pins (PWMP7: GPIO1=SDA, GPIO3=SCL on CH2/CH3 breakout) — overridden by build_flags ---
#ifndef ZEPHYR_I2C_SDA
#define ZEPHYR_I2C_SDA       4
#endif
#ifndef ZEPHYR_I2C_SCL
#define ZEPHYR_I2C_SCL       5
#endif
#define ZEPHYR_I2C_CLOCK     400000    // 400kHz fast mode
#define ZEPHYR_I2C_TIMEOUT_US 3000     // 3ms I2C read timeout

// Re-probe backoff for an absent/unready MPU6050. begin()→_mpuInit() blocks
// ~105ms (device reset + PLL wake), so an every-tick retry would stall the
// CRSF loop. Retrying once per second keeps the loop responsive while still
// hot-plug friendly (plug the MPU in, gyro comes up within ~1s).
#define ZEPHYR_PROBE_RETRY_US 1000000  // 1s between MPU re-probe attempts

// --- GPIO Pre-Detect: probe SCL line before I2C init to avoid boot loop when MPU absent ---
// When defined, begin() reads SCL as INPUT_PULLUP before calling Wire.begin().
// MPU breakout's 4.7kΩ pull-up → HIGH (proceed). Float/pull-down → LOW (skip I2C safely).
// Undefine if using external level shifters or non-standard pull-ups that defeat detection.
#ifndef ZEPHYR_I2C_PRE_DETECT
  #define ZEPHYR_I2C_PRE_DETECT 1
#endif

// --- MPU6050 I2C Address ---
#define ZEPHYR_MPU_ADDR      0x68

// --- MPU6050 Scale Ranges ---
#define ZEPHYR_ACCEL_SCALE   2         // ±2g → 16384 LSB/g
#define ZEPHYR_GYRO_SCALE    250       // ±250°/s → 131 LSB/(°/s)

// --- DLPF Bandwidth (MPU6050 DLPF_CFG=3 → 42Hz accel, 42Hz gyro) ---
#define ZEPHYR_DLPF_CFG      3

// --- Sample Rate Divider (1kHz / (1 + div)) ---
#define ZEPHYR_SMPLRT_DIV    0         // 1kHz sample rate

// --- Mahony AHRS Complementary Filter Gains ---
#define ZEPHYR_MAHONY_KP     2.0f      // Proportional gain (accel correction strength)
#define ZEPHYR_MAHONY_KI     0.005f    // Integral gain (slow gyro bias drift correction)

// --- PID: Roll Axis (stabilizes roll angle to 0°) ---
#define ZEPHYR_PID_ROLL_KP   1.2f
#define ZEPHYR_PID_ROLL_KI   0.05f
#define ZEPHYR_PID_ROLL_KD   0.15f
#define ZEPHYR_PID_ROLL_IMAX 30.0f     // Integrator anti-windup clamp (°)

// --- PID: Yaw Axis (dampens yaw rate to 0°/s) ---
#define ZEPHYR_PID_YAW_KP    0.8f
#define ZEPHYR_PID_YAW_KI    0.03f
#define ZEPHYR_PID_YAW_KD    0.10f
#define ZEPHYR_PID_YAW_IMAX  20.0f     // Integrator anti-windup clamp (°/s)

// --- PID: Pitch Axis (stabilizes pitch angle to 0°) ---
#define ZEPHYR_PID_PITCH_KP   1.0f
#define ZEPHYR_PID_PITCH_KI   0.04f
#define ZEPHYR_PID_PITCH_KD   0.12f
#define ZEPHYR_PID_PITCH_IMAX 25.0f    // Integrator anti-windup clamp (°)

// --- Crest Rudder Mix: maps PID corrections to µs offset ---
#define ZEPHYR_RUDDER_ROLL_GAIN  2.5f  // µs per degree of roll correction
#define ZEPHYR_RUDDER_YAW_GAIN   1.5f  // µs per °/s of yaw rate correction
#define ZEPHYR_RUDDER_CLAMP_US   200   // Hard clamp on gyro correction (±µs)

// --- Gearbox Leg Servo Mix: maps PID corrections to µs offset ---
#define ZEPHYR_GEARBOX_ROLL_GAIN  3.0f   // µs per degree roll → leg ailerons
#define ZEPHYR_GEARBOX_PITCH_GAIN 3.0f   // µs per degree pitch → leg elevons
#define ZEPHYR_GEARBOX_CLAMP_US   250    // Hard clamp on leg servo correction (±µs)

// --- Board Mounting Rotation ---
// How the MPU6050 is physically mounted in the aircraft.
// Selects axis remapping and sign flipping before AHRS.
//   0 = DEFAULT     GY-521 flat, pins forward, chip up    X→fwd  Y→left  Z→up
//   1 = YAW_90      rotated 90° clockwise (pins right)    X→right Y→fwd   Z→up
//   2 = YAW_180     rotated 180° (pins backward)          X→back  Y→right Z→up
//   3 = YAW_270     rotated 270° (pins left)              X→left  Y→back  Z→up
//   4 = UPSIDE_DOWN flipped over (pins fwd, chip down)    X→fwd   Y→right Z→down
//   5 = VERT_FWD    mounted vertically, pins forward      X→fwd   Y→up    Z→right
//   6 = VERT_RIGHT  mounted vertically, pins right        X→right Y→up    Z→back
// Default: 0 (GY-521 flat, pins toward nose)
#ifndef ZEPHYR_BOARD_ROTATION
  #define ZEPHYR_BOARD_ROTATION 0
#endif

// --- Calibration ---
#define ZEPHYR_CALIB_SAMPLES      200   // Gyro bias sample count
#define ZEPHYR_CALIB_VARIANCE_MAX 250   // Reject if gyro variance exceeds this
#define ZEPHYR_CALIB_STABLE_COUNT 20    // Consecutive stable reads before accepting
#define ZEPHYR_CALIB_MAX_SAMPLES  500   // Give up after this many samples (timeout safety)

// --- Slew Boost: transient rudder boost on fast attitude change ---
// The slew idea, driven by the disturbance itself: a gust
// or hard maneuver slews the attitude error; the filtered roll-error rate
// briefly boosts the rudder correction (boost on disturbance, brake on
// recovery) and decays with the LPF τ once motion settles. Gain 0 = off.
#define ZEPHYR_SLEW_GAIN     1.5f   // µs per °/s of filtered roll-error rate at 100%
#define ZEPHYR_SLEW_CLAMP_US 80     // hard clamp on the boost term (±µs)
#define ZEPHYR_SLEW_LPF_TAU  0.12f  // transient decay time constant (s)

// ─── 2-Wing Flapping Stabilization (Mesozoic) ─────────────────────────
// Maps the raw Zephyrus PID outputs DIRECTLY onto the two flapping servos,
// riding the SAME axes the pilot sticks already use — a primitive, robust
// stabilizer for a 2-servo ornithopter with no separate control surfaces:
//   Roll  → differential flap AMPLITUDE   (aileron axis, proven roll torque)
//   Pitch → symmetric flap CENTRE shift    (elevator axis)
//   Yaw   → differential FEROCITY          (rudder axis, drag rate-damping)
// The runtime gains (Ornithopter::wingRollGain / wingPitchGain /
// wingYawGain, 0–100) scale these: 0 disables an axis, 100 = full authority.
// Scale constants below are the FULL-STICK authority per PID-output unit at
// 100% gain. PID outputs are roughly proportional to attitude error, so the
// clamps bound the maximum wing perturbation regardless of error magnitude.
#define ZEPHYR_WING_ROLL_AMP_SCALE      0.02f  // amp-fraction per roll-corr unit @100%
#define ZEPHYR_WING_ROLL_AMP_CLAMP      0.5f   // max differential amplitude fraction
#define ZEPHYR_WING_ROLL_CENTER_SCALE   0.5f   // deg per roll-corr unit @100% (glide)
#define ZEPHYR_WING_ROLL_CENTER_CLAMP   15.0f  // max glide self-level centre offset (deg)
#define ZEPHYR_WING_PITCH_CENTER_SCALE  0.6f   // deg per pitch-corr unit @100%
#define ZEPHYR_WING_PITCH_CENTER_CLAMP  12.0f  // max symmetric centre shift (deg)
#define ZEPHYR_WING_YAW_FER_SCALE       0.1f   // ferocity units per yaw-corr unit @100%
#define ZEPHYR_WING_YAW_FER_CLAMP       2.0f   // max differential ferocity

// ─── Mesozoic rate-only brain (MESOZOIC_ONLY) ──────────────────────────────
// Rate damper: corrections are °/s (roll/pitch/yaw angular RATE), not angles.
// Dropping Mahony means the level reference is gone; the brain only damps the
// tumble. The pilot + dihedral do the levelling. Integer path assumes the
// MPU6050 ±250 dps default: 131 LSB/(°/s), so bias→LSB conversion is exact.

// Q12 PID gains (real gain = value / 4096). err is raw gyro LSB; integrator
// clamp imax is in the same Q12·LSB units as the accumulator. Starting points
// for the simulator — retune against measured disturbance rates.
#define MESO_ROLL_KP     3072   // 0.75  P
#define MESO_ROLL_KI      64    // ~0.016
#define MESO_ROLL_KD     1024   // 0.25  D
#define MESO_ROLL_IMAX   5365760 // ≈ 10 °/s max integrator contribution

#define MESO_PITCH_KP    3072
#define MESO_PITCH_KI     64
#define MESO_PITCH_KD    1024
#define MESO_PITCH_IMAX  5365760

#define MESO_YAW_KP      2048   // 0.5
#define MESO_YAW_KI       48
#define MESO_YAW_KD       768   // 0.1875
#define MESO_YAW_IMAX    2682880 // ≈ 5 °/s

// Wing scales for rate (°/s) input — FULL-STICK authority per °/s at 100% gain.
// (The ONDAS angle-based ZEPHYR_WING_*_SCALE constants above are for the
// attitude path and are unused under MESOZOIC_ONLY.)
#define ZEPHYR_WING_ROLL_RATE_AMP_SCALE      0.02f   // amp-fraction per °/s @100%
#define ZEPHYR_WING_ROLL_RATE_AMP_CLAMP      0.5f
#define ZEPHYR_WING_ROLL_RATE_CENTER_SCALE   0.12f   // deg per °/s @100% (glide)
#define ZEPHYR_WING_ROLL_RATE_CENTER_CLAMP   15.0f
#define ZEPHYR_WING_PITCH_RATE_CENTER_SCALE  0.08f   // deg per °/s @100%
#define ZEPHYR_WING_PITCH_RATE_CENTER_CLAMP  12.0f
#define ZEPHYR_WING_YAW_RATE_FER_SCALE       0.02f   // ferocity per °/s @100%
#define ZEPHYR_WING_YAW_RATE_FER_CLAMP       2.0f