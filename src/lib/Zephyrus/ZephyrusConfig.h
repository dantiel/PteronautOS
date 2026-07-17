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
#define ZEPHYR_MAHONY_KI     0.0f      // Integral gain (gyro bias drift correction — zero for yaw-rate-only)

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

// --- Crest Rudder Mix: maps PID corrections to µs offset ---
#define ZEPHYR_RUDDER_ROLL_GAIN  2.5f  // µs per degree of roll correction
#define ZEPHYR_RUDDER_YAW_GAIN   1.5f  // µs per °/s of yaw rate correction
#define ZEPHYR_RUDDER_CLAMP_US   200   // Hard clamp on gyro correction (±µs)

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