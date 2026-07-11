/*
  Zephyrus Gyro Module — PteronautOS Crest Rudder Stabilization
  Full implementation: MPU6050 I2C driver, Mahony AHRS, Dual PID, Auto-calibration

  All names, structures, and comments are original work for PteronautOS.
  No external AHRS/MPU6050/PID libraries used — register-level I2C only.
*/

#include "Zephyrus.h"
#ifndef UNIT_TEST
#include <Arduino.h>
#include <Wire.h>
#endif

// ---------------------------------------------------------------------------
//  MPU6050 Register Map (only the registers we use)
// ---------------------------------------------------------------------------
#define MPU_REG_WHO_AM_I      0x75
#define MPU_REG_PWR_MGMT_1    0x6B
#define MPU_REG_SMPLRT_DIV    0x19
#define MPU_REG_CONFIG         0x1A
#define MPU_REG_GYRO_CONFIG    0x1B
#define MPU_REG_ACCEL_CONFIG   0x1C
#define MPU_REG_ACCEL_XOUT_H   0x3B   // Start of 14-byte sensor block
#define MPU_WHO_AM_I_VALUE     0x68

// PWR_MGMT_1 bits
#define MPU_PWR1_DEVICE_RESET  0x80
#define MPU_PWR1_SLEEP         0x40
#define MPU_PWR1_CLKSEL_PLL    0x01   // X-axis gyro PLL

// ---------------------------------------------------------------------------
//  Scale lookup — LSB per unit for each range setting
// ---------------------------------------------------------------------------
static const float ACCEL_LSB_PER_G[]   = { 16384.0f, 8192.0f, 4096.0f, 2048.0f };
static const float GYRO_LSB_PER_DPS[]  = { 131.0f, 65.5f, 32.8f, 16.4f };

// ---------------------------------------------------------------------------
//  Singleton
// ---------------------------------------------------------------------------
Zephyrus zephyrus;

// ---------------------------------------------------------------------------
//  Constructor
// ---------------------------------------------------------------------------
Zephyrus::Zephyrus()
    : enabled(false)
    , _begun(false)
    , calibrated(false)
    , rollDeg(0.0f), pitchDeg(0.0f), yawRate(0.0f)
    , rollCorrection(0.0f), yawCorrection(0.0f), rudderCorrection(0.0f)
    , _mpuPresent(false)
    , _accelScale(ACCEL_LSB_PER_G[0])
    , _gyroScale(GYRO_LSB_PER_DPS[0])
    , _lastAhrsUs(0)
    , _calibrating(false)
    , _calibCount(0), _calibStable(0)
{
    _q[0] = 1.0f; _q[1] = 0.0f; _q[2] = 0.0f; _q[3] = 0.0f;
    _integralFB[0] = 0.0f; _integralFB[1] = 0.0f; _integralFB[2] = 0.0f;
    for (int i = 0; i < 3; i++) {
        _gyroRaw[i] = 0;
        _accelRaw[i] = 0;
        _gyroBias[i] = 0.0f;
        _calibSum[i] = 0.0f;
        _calibSumSq[i] = 0.0f;
        _prevGyro[i] = 0.0f;
    }
    _pidReset(_pidRoll);
    _pidReset(_pidYaw);
}

// ---------------------------------------------------------------------------
//  I2C Helpers — Register-level MPU6050 access via Wire.h
// ---------------------------------------------------------------------------
void Zephyrus::_mpuWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ZEPHYR_MPU_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

uint8_t Zephyrus::_mpuRead(uint8_t reg) {
    Wire.beginTransmission(ZEPHYR_MPU_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(ZEPHYR_MPU_ADDR, (uint8_t)1);
    uint32_t deadline = micros() + ZEPHYR_I2C_TIMEOUT_US;
    while (Wire.available() < 1 && micros() < deadline) { /* wait */ }
    return (Wire.available() >= 1) ? Wire.read() : 0;
}

bool Zephyrus::_mpuBurstRead(uint8_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(ZEPHYR_MPU_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;

    Wire.requestFrom(ZEPHYR_MPU_ADDR, len);
    uint32_t deadline = micros() + ZEPHYR_I2C_TIMEOUT_US;
    while (Wire.available() < len && micros() < deadline) { /* wait */ }
    if (Wire.available() < len) return false;

    for (uint8_t i = 0; i < len; i++) {
        buf[i] = Wire.read();
    }
    return true;
}

// ---------------------------------------------------------------------------
//  MPU6050 Initialization
// ---------------------------------------------------------------------------
bool Zephyrus::_mpuInit() {
    // Reset device
    _mpuWrite(MPU_REG_PWR_MGMT_1, MPU_PWR1_DEVICE_RESET);
    delay(100);

    // Wake up with X-gyro PLL clock
    _mpuWrite(MPU_REG_PWR_MGMT_1, MPU_PWR1_CLKSEL_PLL);
    delay(5);

    // Verify presence
    uint8_t who = _mpuRead(MPU_REG_WHO_AM_I);
    if (who != MPU_WHO_AM_I_VALUE) {
        return false;
    }

    // Sample rate divider
    _mpuWrite(MPU_REG_SMPLRT_DIV, ZEPHYR_SMPLRT_DIV);

    // DLPF configuration
    _mpuWrite(MPU_REG_CONFIG, ZEPHYR_DLPF_CFG);

    // Gyro scale (±250°/s default = 0)
    uint8_t gyroCfg = 0;
    switch (ZEPHYR_GYRO_SCALE) {
        case 250:  gyroCfg = 0x00; break;
        case 500:  gyroCfg = 0x08; break;
        case 1000: gyroCfg = 0x10; break;
        case 2000: gyroCfg = 0x18; break;
        default:   gyroCfg = 0x00; break;
    }
    _mpuWrite(MPU_REG_GYRO_CONFIG, gyroCfg);

    // Accel scale (±2g default = 0)
    uint8_t accelCfg = 0;
    switch (ZEPHYR_ACCEL_SCALE) {
        case 2:  accelCfg = 0x00; break;
        case 4:  accelCfg = 0x08; break;
        case 8:  accelCfg = 0x10; break;
        case 16: accelCfg = 0x18; break;
        default: accelCfg = 0x00; break;
    }
    _mpuWrite(MPU_REG_ACCEL_CONFIG, accelCfg);

    // Set scale factors for conversion
    uint8_t accelIdx = (accelCfg >> 3) & 0x03;
    uint8_t gyroIdx  = (gyroCfg >> 3) & 0x03;
    _accelScale = ACCEL_LSB_PER_G[accelIdx];
    _gyroScale  = GYRO_LSB_PER_DPS[gyroIdx];

    return true;
}

// ---------------------------------------------------------------------------
//  Read sensor block — 14 bytes: AccelX..AccelZ, Temp, GyroX..GyroZ
// ---------------------------------------------------------------------------
bool Zephyrus::_mpuReadSensors() {
    uint8_t buf[14];
    if (!_mpuBurstRead(MPU_REG_ACCEL_XOUT_H, buf, 14)) {
        return false;
    }

    // Big-endian int16 from consecutive bytes
    _accelRaw[0] = (int16_t)((buf[0]  << 8) | buf[1]);   // X
    _accelRaw[1] = (int16_t)((buf[2]  << 8) | buf[3]);   // Y
    _accelRaw[2] = (int16_t)((buf[4]  << 8) | buf[5]);   // Z
    // Skip temp at buf[6..7]
    _gyroRaw[0]  = (int16_t)((buf[8]  << 8) | buf[9]);   // X
    _gyroRaw[1]  = (int16_t)((buf[10] << 8) | buf[11]);  // Y
    _gyroRaw[2]  = (int16_t)((buf[12] << 8) | buf[13]);  // Z

    return true;
}

// ---------------------------------------------------------------------------
//  Public: begin()
// ---------------------------------------------------------------------------
void Zephyrus::begin() {
    _begun = true;
    Wire.begin(ZEPHYR_I2C_SDA, ZEPHYR_I2C_SCL);
    Wire.setClock(ZEPHYR_I2C_CLOCK);

    _mpuPresent = _mpuInit();
    if (!_mpuPresent) {
        enabled = false;
        return;
    }

    enabled = true;
    _calibrating = true;
    _calibCount = 0;
    _calibStable = 0;
    for (int i = 0; i < 3; i++) {
        _calibSum[i]   = 0.0f;
        _calibSumSq[i] = 0.0f;
        _prevGyro[i]   = 0.0f;
    }
    _mahonyReset();
    _pidReset(_pidRoll);
    _pidReset(_pidYaw);
}

// ---------------------------------------------------------------------------
//  Public: onLinkUp() — reset integrators when arming
// ---------------------------------------------------------------------------
void Zephyrus::onLinkUp() {
    _pidReset(_pidRoll);
    _pidReset(_pidYaw);
}

// ---------------------------------------------------------------------------
//  Public: onLinkDown() — zero correction on disarm
// ---------------------------------------------------------------------------
void Zephyrus::onLinkDown() {
    rollCorrection = 0.0f;
    yawCorrection = 0.0f;
    rudderCorrection = 0.0f;
    _pidReset(_pidRoll);
    _pidReset(_pidYaw);
}

// ---------------------------------------------------------------------------
//  Auto-Calibration — accumulate gyro samples, check bias + variance
// ---------------------------------------------------------------------------
void Zephyrus::_calibrationStep() {
    if (!_mpuReadSensors()) return;   // I2C error — skip this sample

    // Convert raw gyro to °/s (with running bias)
    float gx = (float)_gyroRaw[0] / _gyroScale - _gyroBias[0];
    float gy = (float)_gyroRaw[1] / _gyroScale - _gyroBias[1];
    float gz = (float)_gyroRaw[2] / _gyroScale - _gyroBias[2];

    // Stability check — reject if any axis differs by >2°/s from previous
    if (_calibCount > 0) {
        float dgx = gx - _prevGyro[0];
        float dgy = gy - _prevGyro[1];
        float dgz = gz - _prevGyro[2];
        if (fabsf(dgx) > 2.0f || fabsf(dgy) > 2.0f || fabsf(dgz) > 2.0f) {
            _calibStable = 0;  // Reset stable counter on disturbance
            // Also reset the running sums to get a clean set
            _calibCount = 0;
            for (int i = 0; i < 3; i++) {
                _calibSum[i] = 0.0f;
                _calibSumSq[i] = 0.0f;
            }
        } else {
            _calibStable++;
        }
    }

    _prevGyro[0] = gx;
    _prevGyro[1] = gy;
    _prevGyro[2] = gz;

    float raw[3] = {
        (float)_gyroRaw[0] / _gyroScale,
        (float)_gyroRaw[1] / _gyroScale,
        (float)_gyroRaw[2] / _gyroScale
    };

    for (int i = 0; i < 3; i++) {
        _calibSum[i]   += raw[i];
        _calibSumSq[i] += raw[i] * raw[i];
    }
    _calibCount++;

    // Timeout: if too many samples without stability, accept what we have
    if (_calibCount >= ZEPHYR_CALIB_MAX_SAMPLES) {
        float n = (float)_calibCount;
        for (int i = 0; i < 3; i++) {
            float mean     = _calibSum[i] / n;
            float meanSq   = _calibSumSq[i] / n;
            float variance = meanSq - mean * mean;
            if (variance < ZEPHYR_CALIB_VARIANCE_MAX) {
                _gyroBias[i] = mean;
            }
        }
        _calibrating = false;
        calibrated = true;
        _lastAhrsUs = micros();
        return;
    }

    if (_calibCount >= ZEPHYR_CALIB_SAMPLES && _calibStable >= ZEPHYR_CALIB_STABLE_COUNT) {
        // Compute mean and variance
        float n = (float)_calibCount;
        for (int i = 0; i < 3; i++) {
            float mean     = _calibSum[i] / n;
            float meanSq   = _calibSumSq[i] / n;
            float variance = meanSq - mean * mean;
            if (variance < ZEPHYR_CALIB_VARIANCE_MAX) {
                _gyroBias[i] = mean;
            }
            // else: bias stays at 0 for this axis (already zero from constructor)
        }

        _calibrating = false;
        calibrated = true;
        _lastAhrsUs = micros();  // Prime AHRS timestamp for first real update
    }
}

// ---------------------------------------------------------------------------
//  Mahony AHRS — Complementary filter (quaternion + accel correction)
//  Reference: S. O. H. Madgwick, "An efficient orientation filter..."
//  Adapted for PteronautOS: accel corrects roll/pitch, gyro Z for yaw rate
// ---------------------------------------------------------------------------
void Zephyrus::_mahonyUpdate(float gx, float gy, float gz,
                              float ax, float ay, float az,
                              float dt) {
    // Clamp dt to prevent integration blowup
    if (dt <= 0.0f || dt > 0.1f) dt = 0.01f;

    float q0 = _q[0], q1 = _q[1], q2 = _q[2], q3 = _q[3];
    float recipNorm;
    float halfvx, halfvy, halfvz;
    float halfex, halfey, halfez;

    // Compute feedback only if accelerometer data is valid (non-zero)
    // Normalize accel
    float accelNorm = sqrtf(ax * ax + ay * ay + az * az);
    if (accelNorm > 0.001f) {
        recipNorm = 1.0f / accelNorm;
        ax *= recipNorm;
        ay *= recipNorm;
        az *= recipNorm;

        // Estimated direction of gravity from quaternion
        halfvx = q1 * q3 - q0 * q2;
        halfvy = q0 * q1 + q2 * q3;
        halfvz = q0 * q0 - 0.5f + q3 * q3;

        // Error is cross product between estimated and measured direction
        halfex = (ay * halfvz - az * halfvy);
        halfey = (az * halfvx - ax * halfvz);
        halfez = (ax * halfvy - ay * halfvx);

        // Apply integral feedback (if Ki > 0)
        if (ZEPHYR_MAHONY_KI > 0.0f) {
            _integralFB[0] += ZEPHYR_MAHONY_KI * halfex * dt;
            _integralFB[1] += ZEPHYR_MAHONY_KI * halfey * dt;
            _integralFB[2] += ZEPHYR_MAHONY_KI * halfez * dt;

            // Apply integral to gyro
            gx += _integralFB[0];
            gy += _integralFB[1];
            gz += _integralFB[2];
        }

        // Apply proportional feedback to gyro
        gx += ZEPHYR_MAHONY_KP * halfex;
        gy += ZEPHYR_MAHONY_KP * halfey;
        gz += ZEPHYR_MAHONY_KP * halfez;
    }

    // Integrate rate of change of quaternion
    // qDot = 0.5 * q ⊗ ω  where ω = [0, gx, gy, gz]
    // (All gyro values in rad/s — convert from °/s)
    float gxR = gx * 0.0174533f;  // deg→rad
    float gyR = gy * 0.0174533f;
    float gzR = gz * 0.0174533f;

    float qDot0 = 0.5f * (-q1 * gxR - q2 * gyR - q3 * gzR);
    float qDot1 = 0.5f * ( q0 * gxR + q2 * gzR - q3 * gyR);
    float qDot2 = 0.5f * ( q0 * gyR - q1 * gzR + q3 * gxR);
    float qDot3 = 0.5f * ( q0 * gzR + q1 * gyR - q2 * gxR);

    q0 += qDot0 * dt;
    q1 += qDot1 * dt;
    q2 += qDot2 * dt;
    q3 += qDot3 * dt;

    // Normalize quaternion — guard against numerical instability
    float norm = q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3;
    if (norm < 1e-6f) {
        _mahonyReset();
        return;
    }
    recipNorm = 1.0f / sqrtf(norm);
    _q[0] = q0 * recipNorm;
    _q[1] = q1 * recipNorm;
    _q[2] = q2 * recipNorm;
    _q[3] = q3 * recipNorm;
}

void Zephyrus::_mahonyReset() {
    _q[0] = 1.0f; _q[1] = 0.0f; _q[2] = 0.0f; _q[3] = 0.0f;
    _integralFB[0] = 0.0f; _integralFB[1] = 0.0f; _integralFB[2] = 0.0f;
}

// ---------------------------------------------------------------------------
//  PID Controller — Anti-windup + derivative-on-measurement
// ---------------------------------------------------------------------------
float Zephyrus::_pidCompute(PidState &s, float error, float dt,
                             float kp, float ki, float kd, float imax) {
    if (dt <= 0.0f || dt > 0.1f) return 0.0f;

    // Proportional
    float pOut = kp * error;

    // Integral with anti-windup clamping
    s.integrator += ki * error * dt;
    if (s.integrator > imax)  s.integrator = imax;
    if (s.integrator < -imax) s.integrator = -imax;
    float iOut = s.integrator;

    // Derivative on measurement (not on error — avoids derivative kick)
    // dError/dt = -dMeasurement/dt when setpoint is constant (zero)
    float derivative = (error - s.lastError) / dt;
    // Low-pass filter the derivative (simple EMA with alpha=0.5)
    s.lastDerivative = s.lastDerivative * 0.5f + derivative * 0.5f;
    float dOut = kd * s.lastDerivative;

    s.lastError = error;

    return pOut + iOut + dOut;
}

void Zephyrus::_pidReset(PidState &s) {
    s.integrator = 0.0f;
    s.lastError = 0.0f;
    s.lastDerivative = 0.0f;
}

// ---------------------------------------------------------------------------
//  Quaternion → Euler angles
//  Roll: rotation around X (forward axis in standard aircraft frame)
//  Pitch: rotation around Y
//  Yaw rate: direct from gyro Z (no magnetometer integration needed)
// ---------------------------------------------------------------------------
static void quatToEuler(const float q[4], float &roll, float &pitch) {
    float q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];

    // Roll (X-axis rotation): atan2(2(q0*q1 + q2*q3), 1 - 2(q1^2 + q2^2))
    roll = atan2f(2.0f * (q0 * q1 + q2 * q3),
                  1.0f - 2.0f * (q1 * q1 + q2 * q2));

    // Pitch (Y-axis rotation): asin(2(q0*q2 - q3*q1))
    float sinPitch = 2.0f * (q0 * q2 - q3 * q1);
    if (sinPitch > 1.0f)  sinPitch = 1.0f;
    if (sinPitch < -1.0f) sinPitch = -1.0f;
    pitch = asinf(sinPitch);

    // Convert to degrees
    roll  *= 57.29578f;
    pitch *= 57.29578f;
}

// ---------------------------------------------------------------------------
//  Public: update() — Main tick: read sensors, AHRS, PID, compute correction
// ---------------------------------------------------------------------------
void Zephyrus::update(uint32_t nowUs) {
    if (!_begun) {
        begin();
    }
    if (!enabled) return;

    // Calibration phase — accumulate samples, skip AHRS/PID until done
    if (_calibrating) {
        _calibrationStep();
        if (_calibrating) return;  // Not done yet
    }

    // Read MPU6050
    if (!_mpuReadSensors()) {
        // I2C failure — decay correction toward zero gracefully
        rollCorrection *= 0.9f;
        yawCorrection *= 0.9f;
        rudderCorrection *= 0.9f;
        return;
    }

    // Compute dt for AHRS
    if (_lastAhrsUs == 0) { _lastAhrsUs = nowUs; return; }
    float dt = (float)(nowUs - _lastAhrsUs) * 1e-6f;
    _lastAhrsUs = nowUs;

    // Convert raw to physical units
    float gx = (float)_gyroRaw[0] / _gyroScale - _gyroBias[0];  // °/s
    float gy = (float)_gyroRaw[1] / _gyroScale - _gyroBias[1];
    float gz = (float)_gyroRaw[2] / _gyroScale - _gyroBias[2];

    float ax = (float)_accelRaw[0] / _accelScale;  // g
    float ay = (float)_accelRaw[1] / _accelScale;
    float az = (float)_accelRaw[2] / _accelScale;

    // Run Mahony AHRS
    _mahonyUpdate(gx, gy, gz, ax, ay, az, dt);

    // Extract roll/pitch from quaternion
    quatToEuler(_q, rollDeg, pitchDeg);

    // Yaw rate comes directly from gyro Z (drift-free for rate control)
    yawRate = gz;

    // --- Roll PID (target: 0°, stabilize roll) ---
    rollCorrection = _pidCompute(_pidRoll, -rollDeg, dt,
                                  ZEPHYR_PID_ROLL_KP,
                                  ZEPHYR_PID_ROLL_KI,
                                  ZEPHYR_PID_ROLL_KD,
                                  ZEPHYR_PID_ROLL_IMAX);

    // --- Yaw PID (target: 0°/s, dampen yaw rate) ---
    yawCorrection = _pidCompute(_pidYaw, -yawRate, dt,
                                 ZEPHYR_PID_YAW_KP,
                                 ZEPHYR_PID_YAW_KI,
                                 ZEPHYR_PID_YAW_KD,
                                 ZEPHYR_PID_YAW_IMAX);

    // --- Crest rudder correction (µs offset) ---
    rudderCorrection = rollCorrection * ZEPHYR_RUDDER_ROLL_GAIN
                     + yawCorrection * ZEPHYR_RUDDER_YAW_GAIN;

    // Clamp to ±200µs
    if (rudderCorrection > ZEPHYR_RUDDER_CLAMP_US)
        rudderCorrection = ZEPHYR_RUDDER_CLAMP_US;
    if (rudderCorrection < -ZEPHYR_RUDDER_CLAMP_US)
        rudderCorrection = -ZEPHYR_RUDDER_CLAMP_US;
}