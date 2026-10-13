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
#define ZEPHYR_I2C_CLOCK     100000    // 100kHz standard mode — far more robust against servo EMI on long leads
#define ZEPHYR_I2C_TIMEOUT_US 3000     // 3ms I2C read timeout

// Max per-bit I2C clock-stretch wait. The ESP8266 software I2C (Twi) default is
// 150ms/bit — a stuck MPU holding SCL low then blocks the main loop for up to
// 150ms × 9 bits × 16 bytes ≈ 21s, starving the hardware watchdog (6s) and
// rebooting. The MPU6050 never legitimately stretches the clock; 2ms is 200× a
// 100kHz bit time and fails fast so the bus-recovery path can revive the gyro.
#define ZEPHYR_I2C_CLOCK_STRETCH_US 2000

// Consecutive failed I2C reads before declaring the bus stuck and forcing a
// clock-out + MPU re-init. 5 ticks ≈ 15ms (SERVO_TICK_MS=3) — long enough to
// ignore a single noise glitch, short enough to revive a locked MPU fast.
#define ZEPHYR_I2C_FAIL_RESET 5

// --- Blackbox (debug data recorder) ------------------------------------------
// Compile with -D ZEPHYR_BLACKBOX=1 (HORIZON debug build) to enable the RAM
// ring buffer. It captures the last ~0.77s (256 × 3ms) of the stabilizer's
// inputs/outputs + fault flags, dumped as JSON via GET /pteronautos/blackbox.
// The fault register below is ALWAYS compiled (a few uint16 — negligible cost)
// so even a release build reports WHY the gyro stalled.
#ifdef ZEPHYR_BLACKBOX
#define ZEPHYR_BB_SAMPLES      128      // 128 × 3ms ≈ 0.38s pre-freeze history
#endif

// Always-on fault register — why did the stabilizer last stall?
#define ZEPHYR_FAULT_NONE      0
#define ZEPHYR_FAULT_NAN       1
#define ZEPHYR_FAULT_I2C_FAIL  2
#define ZEPHYR_FAULT_BUS_REC   3
#define ZEPHYR_FAULT_REINIT    4

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
// KP/KI are in rad/s (the filter converts the °/s gyro to rad/s first). The
// old code added them to the °/s gyro instead, which made the accelerometer
// correction ~57× too weak (2 rad/s ≈ 114.6 °/s) and let the attitude latch
// onto a wrong value after a fast rotation past ±90°.
#define ZEPHYR_MAHONY_KP     2.0f      // Proportional gain (accel correction strength)
#define ZEPHYR_MAHONY_KI     0.005f    // Integral gain (slow gyro bias drift correction)
#define ZEPHYR_MAHONY_KI_MAX 0.5f      // Integral feedback anti-windup clamp (rad/s ≈ ±28.6°/s)

// --- PID: Roll Axis (rate damper, °/s in → correction out) ---
// These are the runtime-tunable float gains (WebUI zephyrus.pid.*). Under
// MESOZOIC_ONLY they are converted to Q12 fixed-point and drive the mesoPid
// rate damper directly; Imax is the integrator anti-windup clamp in °/s.
// Defaults mirror the MESO_*_* Q12 constants below (real gain = value/4096).
#define ZEPHYR_PID_ROLL_KP   0.75f
#define ZEPHYR_PID_ROLL_KI   0.016f
#define ZEPHYR_PID_ROLL_KD   0.25f
#define ZEPHYR_PID_ROLL_IMAX 10.0f     // Integrator anti-windup clamp (°/s)

// --- PID: Yaw Axis (rate damper, °/s in → correction out) ---
#define ZEPHYR_PID_YAW_KP    0.5f
#define ZEPHYR_PID_YAW_KI    0.012f
#define ZEPHYR_PID_YAW_KD    0.19f
#define ZEPHYR_PID_YAW_IMAX  5.0f      // Integrator anti-windup clamp (°/s)

// --- PID: Pitch Axis (rate damper, °/s in → correction out) ---
#define ZEPHYR_PID_PITCH_KP   0.75f
#define ZEPHYR_PID_PITCH_KI   0.016f
#define ZEPHYR_PID_PITCH_KD   0.25f
#define ZEPHYR_PID_PITCH_IMAX 10.0f    // Integrator anti-windup clamp (°/s)

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
#define ZEPHYR_WING_ROLL_CENTER_CLAMP   20.0f  // max glide self-level centre offset (deg)
#define ZEPHYR_WING_PITCH_CENTER_SCALE  0.6f   // deg per pitch-corr unit @100%
#define ZEPHYR_WING_PITCH_CENTER_CLAMP  16.0f  // max symmetric centre shift (deg)
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
#define ZEPHYR_WING_ROLL_RATE_CENTER_SCALE   0.30f   // deg per °/s @100% (glide)
#define ZEPHYR_WING_ROLL_RATE_CENTER_CLAMP   20.0f
#define ZEPHYR_WING_PITCH_RATE_CENTER_SCALE  0.22f   // deg per °/s @100%
#define ZEPHYR_WING_PITCH_RATE_CENTER_CLAMP  16.0f
#define ZEPHYR_WING_YAW_RATE_FER_SCALE       0.02f   // ferocity per °/s @100%
#define ZEPHYR_WING_YAW_RATE_FER_CLAMP       2.0f
#define ZEPHYR_WING_PITCH_RATE_FER_SCALE     0.02f   // ferocity per °/s @100% (dwell split)
#define ZEPHYR_WING_PITCH_RATE_FER_CLAMP     2.0f   // max symmetric pitch dwell ferocity

// Skew bridge: pitch attitude error (deg) → differential stroke/return skew
// (±100 timing axis). Front-loads the downstroke while back-loading the
// upstroke (phase-aware attitude delivery) — the "local" per-half-stroke
// complement to the static CENTRE shift, and the accel-side counterpart to the
// gyro-driven ferocity dwell above. Sign is tunable via wing_pitch_skew_gain.
#define ZEPHYR_WING_PITCH_ATT_SKEW_SCALE     0.4f   // skew units per deg attitude error @100%
#define ZEPHYR_WING_ROLL_ATT_SKEW_SCALE      0.4f   // skew units per deg roll attitude @100%
#define ZEPHYR_WING_ROLL_ATT_SKEW_CLAMP      40.0f  // max differential roll skew
#define ZEPHYR_WING_YAW_SKEW_SCALE           0.1f   // skew units per deg/s yaw rate @100%
#define ZEPHYR_WING_YAW_SKEW_CLAMP           30.0f  // max differential yaw skew
#define ZEPHYR_WING_PITCH_ATT_SKEW_CLAMP     40.0f  // max differential skew (axis spans ±100)

// ─── Mesozoic HORIZON attitude layer (MESOZOIC_HORIZON, on top of MESOZOIC_ONLY) ───
// Flap-decoupled gravity recovery (MesozoicGravity) + slow complementary
// attitude (MesozoicAttitude). Float path; the fast inner rate PID stays
// fixed-point (Mesozoic.h). Tune the τ values against real gyro/accel logs.
#define MESO_GRAV_LOCK_TAU   0.4f    // ESPELHO lock-in integrator τ (s) — matches OrniFlight ESPELHO_TAU
#define MESO_GRAV_TAU        0.15f   // recovered-gravity low-pass τ (s) → ~1 Hz
#define MESO_ATT_TAU         0.30f   // complementary attitude correction τ (s) → ~0.5 Hz

// --- HORIZON attitude outer loop (T3): attitude error → rate setpoint ---
// Pure-P outer gain (deg/s per deg error); the inner mesoPid carries the rate
// I/D. Kept slow (≤ ~2 Hz closed loop) so no residual flap content can excite
// the attitude. MESO_ATT_MAX_RATE slew-clamps a hard stick throw. Pitch is
// hotter: it is the deliberately unstable axis that must hold level.
#define MESO_ATT_ROLL_KP      2.0f     // °/s per deg roll error (~0.32 Hz)
#define MESO_ATT_PITCH_KP     2.5f     // °/s per deg pitch error (~0.40 Hz)
#define MESO_ATT_MAX_RATE     45.0f    // rate-setpoint slew clamp + full-stick rate cmd (°/s)