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

class Zephyrus {
public:
    bool enabled;           // True if MPU6050 WHO_AM_I matched
    bool gyroEnabled;       // Runtime toggle — true = auto-detect MPU at boot; false = no I2C/PID
    bool calibrated;        // True after bias calibration succeeds
    uint8_t boardRotation;  // Runtime board orientation (0-6, see ZephyrusConfig.h)
 
    float slewGain;   // 0–100, 0=off — transient rudder boost on fast attitude change

    // Runtime-tunable PID gains — initialized from ZephyrusConfig.h defaults,
    // adjustable via the WebUI. Same units as the compile-time constants:
    //   Kp   → ° correction per ° error (roll/pitch) or per °/s (yaw)
    //   Ki   → integral accumulation rate
    //   Kd   → derivative gain
    //   Imax → integrator anti-windup clamp (° or °/s)
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

private:
    // --- MPU6050 Register-Level Driver ---
    bool    _begun;
    bool    _mpuPresent;
    int16_t _gyroRaw[3];    // X, Y, Z raw
    int16_t _accelRaw[3];   // X, Y, Z raw
    float   _gyroBias[3];   // Calibrated offsets
    int16_t _gyroBiasLsb[3];// Calibrated offsets in raw LSB (Mesozoic integer path)
    float   _accelScale;    // LSB/g from config
    float   _gyroScale;     // LSB/(°/s) from config

    void _mpuWrite(uint8_t reg, uint8_t val);
    uint8_t _mpuRead(uint8_t reg);
    bool _mpuBurstRead(uint8_t reg, uint8_t *buf, uint8_t len);
    bool _mpuInit();
    bool _mpuReadSensors();

    // --- Mahony AHRS ---
    float _q[4];            // Quaternion [w, x, y, z]
    float _integralFB[3];   // Integral feedback for gyro bias
    uint32_t _lastAhrsUs;   // Previous AHRS update timestamp
    uint32_t _lastProbeUs;  // Last begin() attempt timestamp (retry backoff)
 
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
};