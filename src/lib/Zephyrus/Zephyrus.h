#pragma once
/*
  Zephyrus Gyro Module — PteronautOS Crest Rudder Stabilization
  Independent from Ornithopter; passes correction via float field.

  Data flow:
    MPU6050 (raw I2C) → Mahony AHRS (quaternion→euler+rates)
    → Dual PID (roll angle + yaw rate) → rudderCorrection (µs)
*/

#include <cstdint>
#include "ZephyrusConfig.h"
#include "Mesozoic.h"
#ifdef MESOZOIC_HORIZON
#include "MesozoicGravity.h"
#include "MesozoicAttitude.h"
#endif

// --- Blackbox frame (12 bytes) -----------------------------------------------
// Compact per-tick record of the stabilizer's hot path. The ring buffer is
// compiled only under ZEPHYR_BLACKBOX; the fault register (Zephyrus members
// below) is always present. See ZephyrusConfig.h for the reason codes.
struct ZephyrBbFrame {
    uint16_t tMs;         // millis() low 16 — wraps 65.5s, fine for the window
    int16_t  rxLsb;       // measured roll rate (bias-corrected, board-rotated)
    int16_t  ryLsb;       // measured pitch rate
    int16_t  rollCorrLsb; // roll damper output (LSB)
    int16_t  pitchCorrLsb;// pitch damper output (LSB)
    uint8_t  flags;       // bit0 I2C-fail, bit1 calibrating, bit2 disabled, bit3 bus-recover
    uint8_t  i2cFail;     // consecutive I2C failure counter (0..ZEPHYR_I2C_FAIL_RESET)
};

class Zephyrus {
public:
    bool enabled;           // True if MPU6050 WHO_AM_I matched
    bool gyroEnabled;       // Runtime toggle — true = auto-detect MPU at boot; false = no I2C/PID
    bool calibrated;        // True after bias calibration succeeds
    uint8_t boardRotation;  // Runtime board orientation (0-6, see ZephyrusConfig.h)
 
    float slewGain;   // 0–100, 0=off — transient rudder boost on fast attitude change

    // Runtime-tunable PID gains — initialized from ZephyrusConfig.h defaults,
    // adjustable via the WebUI. Under MESOZOIC_ONLY these are the RATE-damper
    // gains (all three axes): the Mesozoic path converts them to Q12 fixed-point
    // (real gain = value/4096) each tick and feeds mesoPid directly.
    //   Kp   → rate P (dimensionless; °/s correction per °/s rate error)
    //   Ki   → rate integral accumulation rate
    //   Kd   → rate derivative gain
    //   Imax → integrator anti-windup clamp (°/s)
    float rollKp, rollKi, rollKd, rollImax;
    float pitchKp, pitchKi, pitchKd, pitchImax;
    float yawKp, yawKi, yawKd, yawImax;

    // AHRS outputs (updated each update() call)
    float rollDeg;          // Roll angle in degrees
    float pitchDeg;         // Pitch angle in degrees
    float yawRate;          // Yaw rate in °/s (gyro Z, no magnetometer)

#ifdef MESOZOIC_HORIZON
    // --- Mesozoic HORIZON attitude layer (flap-decoupled gravity) ---
    float flapSin;          // flap phase sin — bridged from Ornithopter each tick
    float flapCos;          // flap phase cos
    bool  flapping;         // flap active — gates the gravity ESPELHO lock-in
    float gravX, gravY, gravZ;  // recovered gravity vector (g), ~1 Hz
    // --- HORIZON stick commands (T3 outer loop), normalized -1..1 ---
    // Bridged from Ornithopter's aileron/elevator/rudder. Stick centre → level
    // hold; full deflection → pure rate command (classic Betaflight HORIZON).
    float stickRollNorm;
    float stickPitchNorm;
    float stickYawNorm;
#endif

    // PID correction outputs
    float rollCorrection;   // Roll RATE correction (°/s) → wing amplitude (fast)
    float yawCorrection;    // Yaw RATE correction (°/s) → differential ferocity
    float pitchCorrection;  // Pitch RATE correction (°/s) → ferocity dwell (fast)
    float rudderCorrection; // Combined rudder offset in µs (clamped)

    // Attitude error (deg) for the wing CENTRE axis (slow level hold). Under
    // MESOZOIC_HORIZON this is the accelerometer-derived attitude error faded
    // by |stick|; 0 in the rate-only brain (no level reference).
    float rollAttitudeCorrection;
    float pitchAttitudeCorrection;

    // Raw pitch PID terms — exposed for ornithopter waveform modulation
    // (Ondas: P→Phase Advance, D→Ferocity, I→Asymmetry; SSFF uses errorRate)
    float pitchPTerm;       // Raw proportional term
    float pitchITerm;       // Raw integral term (accumulated error)
    float pitchDTerm;       // Raw derivative term (low-pass filtered)
    float pitchErrorRate;   // Pitch error derivative in °/s (for SSFF)

    Zephyrus();

    // Lifecycle — called from devServoOutput.cpp integration points
    void begin();           // Init I2C, probe MPU6050, start calibration
    void update(uint32_t nowUs);  // Read sensors, run AHRS+PID, compute correction
    void onLinkUp();        // Reset integrators on arm
    void onLinkDown();      // Disable stabilization on disarm
    void forceCalibrate();  // Reset bias + restart calibration on demand
    void setBoardRotation(uint8_t rot);  // Runtime orientation change
    void reprobe();         // Force a fresh MPU probe on the next update() tick

    // Calibration progress (readable from WebUI state endpoint)
    int    _calibCount;     // Samples accumulated so far
    bool   _calibrating;    // True during calibration phase

    // --- Always-on fault register (why did the gyro stall?) ---
    uint16_t faultNan;
    uint16_t faultI2cFail;
    uint16_t faultBusRecover;
    uint16_t faultReinit;
    uint8_t  lastFaultReason;

#ifdef ZEPHYR_BLACKBOX
    ZephyrBbFrame bbRing[ZEPHYR_BB_SAMPLES];
    uint16_t bbHead;      // next write index
    uint16_t bbCount;     // number of valid frames (0..ZEPHYR_BB_SAMPLES)
#endif

private:
    // --- MPU6050 Register-Level Driver ---
    bool    _begun;
    bool    _mpuPresent;
    bool    _fullResetDone; // MPU6050 device-reset + settle performed (once per boot)
    bool    _wireBegun;     // Wire.begin() performed once per boot — never re-run
    int16_t _gyroRaw[3];    // X, Y, Z raw
    int16_t _accelRaw[3];   // X, Y, Z raw
    float   _gyroBias[3];   // Calibrated offsets
    int16_t _gyroBiasLsb[3];// Calibrated offsets in raw LSB (Mesozoic integer path)
    float   _accelScale;    // LSB/g from config
    float   _gyroScale;     // LSB/(°/s) from config

    void _mpuWrite(uint8_t reg, uint8_t val);
    uint8_t _mpuRead(uint8_t reg);
    bool _mpuBurstRead(uint8_t reg, uint8_t *buf, uint8_t len);
    void _i2cBusRecover();
    bool _mpuInit();
    bool _mpuReadSensors();

    // --- Mahony AHRS ---
    float _q[4];            // Quaternion [w, x, y, z]
    float _integralFB[3];   // Integral feedback for gyro bias
    uint32_t _lastAhrsUs;   // Previous AHRS update timestamp
    uint32_t _lastProbeUs;  // Last begin() attempt timestamp (retry backoff)
    uint8_t  _i2cFailCount; // Consecutive I2C read failures (stuck-bus recovery)
    int16_t  _lastRxLsb;    // last bias-corrected roll rate (blackbox)
    int16_t  _lastRyLsb;    // last bias-corrected pitch rate (blackbox)
 
    float   _slewLPF; // filtered roll-error rate (°/s) for the slew boost

    void _mahonyUpdate(float gx, float gy, float gz,
                       float ax, float ay, float az,
                       float dt);
    void _mahonyReset();
    void _applyAccelLevelRef(float &ax, float &ay, float &az);
    void _applyBoardRotation(float &gx, float &gy, float &gz,
                             float &ax, float &ay, float &az);

    // --- Dual PID Controllers ---
    struct PidState {
        float integrator;
        float lastError;
        float lastDerivative;
    };

    PidState _pidRoll;
    PidState _pidYaw;
    PidState _pidPitch;

    // Mesozoic rate-damper state (fixed-point, integer-only)
    MesoPid _mesoRoll;
    MesoPid _mesoPitch;
    MesoPid _mesoYaw;

#ifdef MESOZOIC_HORIZON
    // Mesozoic HORIZON state (float; slow outer loop, off the hot PID path)
    MesoGravity  _mesoGravity;
    MesoAttitude _mesoAttitude;
#endif

    float _pidCompute(PidState &s, float error, float dt,
                      float kp, float ki, float kd, float imax);
    void  _pidReset(PidState &s);

    // --- Accel Level Reference ---
    float  _accelRefRoll;   // Roll offset from level calibration (rad)
    float  _accelRefPitch;  // Pitch offset from level calibration (rad)

    // --- Auto-Calibration ---
    int    _calibStable;
    float  _calibSum[3];
    float  _calibSumSq[3];   // For variance check
    float  _prevGyro[3];     // Previous sample for stability check
    float  _accelCalSum[3];  // Accel accumulated during calibration

    void _calibrationStep();
    void _finishCalibration(float n);

    void _bbFault(uint8_t reason);              // always-on fault register
    void _bbTick(uint32_t nowUs, uint8_t flags); // ring-buffer frame (ZEPHYR_BLACKBOX)
};