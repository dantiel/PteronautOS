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

class Zephyrus {
public:
    bool enabled;           // True if MPU6050 WHO_AM_I matched
    bool gyroEnabled;       // Runtime disable — when false, no I2C, no MPU, no PID
    bool calibrated;        // True after bias calibration succeeds
    uint8_t boardRotation;  // Runtime board orientation (0-6, see ZephyrusConfig.h)

    // AHRS outputs (updated each update() call)
    float rollDeg;          // Roll angle in degrees
    float pitchDeg;         // Pitch angle in degrees
    float yawRate;          // Yaw rate in °/s (gyro Z, no magnetometer)

    // PID correction outputs
    float rollCorrection;   // Roll PID output magnitude
    float yawCorrection;    // Yaw rate PID output magnitude
    float pitchCorrection;  // Pitch PID output magnitude
    float rudderCorrection; // Combined rudder offset in µs (clamped)

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