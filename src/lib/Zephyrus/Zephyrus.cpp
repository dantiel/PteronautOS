/*
  Zephyrus Gyro Module — PteronautOS Crest Rudder Stabilization
  Full implementation: MPU6050 I2C driver, Mahony AHRS, Dual PID, Auto-calibration

  All names, structures, and comments are original work for PteronautOS.
  No external AHRS/MPU6050/PID libraries used — register-level I2C only.
*/

#include "Zephyrus.h"

#ifdef UNIT_TEST
  #include <stdint.h>
  #include <math.h>

  class WireClass {
  public:
    void begin() {}
    void begin(int,int) {}
    void setClock(unsigned long) {}
    void beginTransmission(int) {}
    void write(uint8_t) {}
    uint8_t endTransmission(bool=true) { return 0; }
    int requestFrom(int,uint8_t) { return 0; }
    int available() { return 0; }
    uint8_t read() { return 0; }
  };
  static WireClass Wire;

  static inline uint32_t micros() { return 0; }
  static inline void delay(unsigned long) {}

  #ifndef sqrtf
  inline float sqrtf(float x) { return (float)sqrt((double)x); }
  #endif
  #ifndef atan2f
  inline float atan2f(float y, float x) { return (float)atan2((double)y,(double)x); }
  #endif
  #ifndef asinf
  inline float asinf(float x) { return (float)asin((double)x); }
  #endif
  #ifndef fabsf
  inline float fabsf(float x) { return (float)fabs((double)x); }
  #endif

#else
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
    , gyroEnabled(false)   // OFF by default — enable via WebUI when MPU6050 connected
    , _begun(false)
    , calibrated(false)
    , rollDeg(0.0f), pitchDeg(0.0f), yawRate(0.0f)
    , rollCorrection(0.0f), yawCorrection(0.0f), pitchCorrection(0.0f), rudderCorrection(0.0f)
    , pitchPTerm(0.0f), pitchITerm(0.0f), pitchDTerm(0.0f), pitchErrorRate(0.0f)
    , _mpuPresent(false)
    , _accelScale(ACCEL_LSB_PER_G[0])
    , _gyroScale(GYRO_LSB_PER_DPS[0])
    , _lastAhrsUs(0)
    , boardRotation(ZEPHYR_BOARD_ROTATION)
    , _calibrating(false)
    , _calibCount(0), _calibStable(0)
    , _accelRefRoll(0.0f), _accelRefPitch(0.0f)
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
        _accelCalSum[i] = 0.0f;
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
    enabled = false;

    if (!gyroEnabled) {
        return;
    }

#if ZEPHYR_I2C_PRE_DETECT && defined(ARDUINO)
    pinMode(ZEPHYR_I2C_SCL, INPUT_PULLUP);
    delay(1);
    if (digitalRead(ZEPHYR_I2C_SCL) == LOW) {
        pinMode(ZEPHYR_I2C_SCL, INPUT);
        return;
    }
    pinMode(ZEPHYR_I2C_SCL, INPUT);
#endif

    Wire.begin(ZEPHYR_I2C_SDA, ZEPHYR_I2C_SCL);
    Wire.setClock(ZEPHYR_I2C_CLOCK);

    if (!_mpuInit()) {
        return;
    }

    _mpuPresent = true;
    enabled = true;

    // Start auto-calibration
    _calibCount = 0;
    _calibStable = 0;
    _calibrating = true;
    calibrated = false;
}

// ---------------------------------------------------------------------------
//  Public: onLinkUp() — reset integrators when arming
// ---------------------------------------------------------------------------
void Zephyrus::onLinkUp() {
    _pidReset(_pidRoll);
    _pidReset(_pidYaw);
    _pidReset(_pidPitch);
}

// ---------------------------------------------------------------------------
//  Public: onLinkDown() — zero correction on disarm
// ---------------------------------------------------------------------------
void Zephyrus::onLinkDown() {
    rollCorrection = 0.0f;
    yawCorrection = 0.0f;
    pitchCorrection = 0.0f;
    rudderCorrection = 0.0f;
    _pidReset(_pidRoll);
    _pidReset(_pidYaw);
    _pidReset(_pidPitch);
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

    // Stability check — reject if any axis differs by >6°/s from previous.
    // Only resets _calibStable (not _calibCount) so the MAX_SAMPLES timeout
    // can still fire. Previously resetting _calibCount to 0 made the timeout
    // unreachable under any vibration — calibration ran forever.
    if (_calibCount > 0) {
        float dgx = gx - _prevGyro[0];
        float dgy = gy - _prevGyro[1];
        float dgz = gz - _prevGyro[2];
        if (fabsf(dgx) > 6.0f || fabsf(dgy) > 6.0f || fabsf(dgz) > 6.0f) {
            _calibStable = 0;
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

    // Accumulate accel for level reference
    float accel[3] = {
        (float)_accelRaw[0] / _accelScale,
        (float)_accelRaw[1] / _accelScale,
        (float)_accelRaw[2] / _accelScale
    };
    for (int i = 0; i < 3; i++) {
        _calibSum[i]    += raw[i];
        _calibSumSq[i]  += raw[i] * raw[i];
        _accelCalSum[i] += accel[i];
    }
    _calibCount++;

    // Timeout: regardless of stability, after MAX_SAMPLES accept whatever bias we have
    if (_calibCount >= ZEPHYR_CALIB_MAX_SAMPLES) {
        float n = (float)_calibCount;
        for (int i = 0; i < 3; i++) {
            _gyroBias[i] = _calibSum[i] / n;
        }
        _finishCalibration(n);
        return;
    }

    // Early completion: enough samples + enough consecutive stable reads
    if (_calibCount >= ZEPHYR_CALIB_SAMPLES && _calibStable >= ZEPHYR_CALIB_STABLE_COUNT) {
        float n = (float)_calibCount;
        for (int i = 0; i < 3; i++) {
            float mean     = _calibSum[i] / n;
            float meanSq   = _calibSumSq[i] / n;
            float variance = meanSq - mean * mean;
            if (variance < ZEPHYR_CALIB_VARIANCE_MAX) {
                _gyroBias[i] = mean;
            }
        }
        _finishCalibration(n);
    }
}

// ---------------------------------------------------------------------------
//  Finish calibration: compute accel level reference from accumulated data
// ---------------------------------------------------------------------------
void Zephyrus::_finishCalibration(float n) {
    _calibrating = false;
    calibrated   = true;
    _lastAhrsUs  = micros();

    // Compute level reference from mean accel during calibration
    float ax = _accelCalSum[0] / n;
    float ay = _accelCalSum[1] / n;
    float az = _accelCalSum[2] / n;

    // Compute roll/pitch from accel: standard aerospace (Z-up frame for GY-521 flat)
    // roll = atan2(ay, az): positive = right wing down
    // pitch = atan2(-ax, sqrt(ay²+az²)): positive = nose up
    _accelRefRoll  = atan2f(ay, az);
    float normYZ   = sqrtf(ay * ay + az * az);
    _accelRefPitch = atan2f(-ax, normYZ);

    // Reset AHRS quaternion to match calibrated level
    // Start from identity, then the accel level ref will be subtracted on first update()
    _mahonyReset();
    rollDeg  = 0.0f;
    pitchDeg = 0.0f;

}

// ---------------------------------------------------------------------------
//  Public: forceCalibrate() — reset bias + restart calibration on demand
// ---------------------------------------------------------------------------
void Zephyrus::forceCalibrate() {
    if (!enabled) return;
    for (int i = 0; i < 3; i++) {
        _gyroBias[i]    = 0.0f;
        _calibSum[i]    = 0.0f;
        _calibSumSq[i]  = 0.0f;
        _prevGyro[i]    = 0.0f;
        _accelCalSum[i] = 0.0f;
    }
    _accelRefRoll  = 0.0f;
    _accelRefPitch = 0.0f;
    _calibCount  = 0;
    _calibStable = 0;
    _calibrating = true;
    calibrated   = false;
    _mahonyReset();
}

// ---------------------------------------------------------------------------
//  Public: setBoardRotation() — runtime orientation change
// ---------------------------------------------------------------------------
void Zephyrus::setBoardRotation(uint8_t rot) {
    if (rot <= 6) {
        boardRotation = rot;
    }
}

// ---------------------------------------------------------------------------
//  Mahony AHRS — Complementary filter (quaternion + accel correction)
//  Reference: S. O. H. Madgwick, "An efficient orientation filter..."
//  Adapted for PteronautOS: accel corrects roll/pitch, gyro Z for yaw rate
// ---------------------------------------------------------------------------

// Board rotation helper — remaps MPU-native axes to aircraft frame.
// Uses runtime boardRotation field (0=DEFAULT, 1=YAW_90, ..., 6=VERT_RIGHT).
// When 0: no-op, compiled out (optimizer eliminates identity transform).
void Zephyrus::_applyBoardRotation(float &gx, float &gy, float &gz,
                                   float &ax, float &ay, float &az) {
    if (boardRotation == 0) return;  // DEFAULT — no-op
    float tmp;
    switch (boardRotation) {
    case 1: // YAW_90: X→right(Y), Y→fwd(-X), Z unchanged
        tmp = gx; gx = gy; gy = -tmp;
        tmp = ax; ax = ay; ay = -tmp;
        break;
    case 2: // YAW_180: X→back(-X), Y→right(-Y), Z unchanged
        gx = -gx; gy = -gy;
        ax = -ax; ay = -ay;
        break;
    case 3: // YAW_270: X→left(-Y), Y→back(X), Z unchanged
        tmp = gx; gx = -gy; gy = tmp;
        tmp = ax; ax = -ay; ay = tmp;
        break;
    case 4: // UPSIDE_DOWN: X→fwd(X), Y→right(-Y), Z→down(-Z)
        gy = -gy; gz = -gz;
        ay = -ay; az = -az;
        break;
    case 5: // VERT_FWD: X→fwd(X), Y→up(Z), Z→right(-Y)
        tmp = gy; gy = gz; gz = -tmp;
        tmp = ay; ay = az; az = -tmp;
        break;
    case 6: // VERT_RIGHT: X→right(Y), Y→up(Z), Z→back(-X)
        tmp = gx; gx = gy; gy = gz; gz = -tmp;
        tmp = ax; ax = ay; ay = az; az = -tmp;
        break;
    default: break;
    }
}

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
    // Validatio: NaN-safe guard — NaN comparisons always false, so isnan() check needed
    if (dt <= 0.0f || dt > 0.1f || isnan(dt) || isnan(error)) return 0.0f;

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
//  Accel Level Reference — rotates measured accel so calibrated level reads [0,0,1g]
//  Uses cross-product rotation from calibrated gravity vector to target [0,0,1].
// ---------------------------------------------------------------------------
void Zephyrus::_applyAccelLevelRef(float &ax, float &ay, float &az) {
    // Compute level quaternion from accel reference (stored as roll/pitch in radians)
    // This maps the calibrated gravity direction → [0,0,1g]
    float cr = cosf(-_accelRefRoll * 0.5f);
    float sr = sinf(-_accelRefRoll * 0.5f);
    float cp = cosf(-_accelRefPitch * 0.5f);
    float sp = sinf(-_accelRefPitch * 0.5f);

    // Combined rotation quaternion: q_pitch ⊗ q_roll (applied pitch first, then roll)
    // q = [cp*cr, -sp*sr, cp*sr, sp*cr]  (simplified from quaternion multiplication)
    float qw = cp * cr + sp * sr;
    float qx = sp * cr - cp * sr;
    float qy = cp * sr + sp * cr;
    float qz = 0.0f;  // No yaw component in level calibration

    // Rotate accel vector by quaternion: a' = q* ⊗ a ⊗ q
    // Using: a' = a + 2*[qw*(q×a) + q×(q×a)] — Rodriguez formula via quaternion
    // Actually simpler: compute rotation matrix from quaternion and apply
    float xx = qx * qx, yy = qy * qy;
    float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    float wx = qw * qx, wy = qw * qy, wz = qw * qz;

    float r11 = 1.0f - 2.0f * (yy + qz*qz);
    float r12 = 2.0f * (xy - wz);
    float r13 = 2.0f * (xz + wy);
    float r21 = 2.0f * (xy + wz);
    float r22 = 1.0f - 2.0f * (xx + qz*qz);
    float r23 = 2.0f * (yz - wx);
    float r31 = 2.0f * (xz - wy);
    float r32 = 2.0f * (yz + wx);
    float r33 = 1.0f - 2.0f * (xx + yy);

    float rax = r11 * ax + r12 * ay + r13 * az;
    float ray = r21 * ax + r22 * ay + r23 * az;
    float raz = r31 * ax + r32 * ay + r33 * az;

    ax = rax; ay = ray; az = raz;
}

// ---------------------------------------------------------------------------
//  Public: update() — Main tick: read sensors, AHRS, PID, compute correction
// ---------------------------------------------------------------------------
void Zephyrus::update(uint32_t nowUs) {
    if (!_begun) {
        begin();
    }
    if (!enabled || !gyroEnabled) {
        rudderCorrection = 0.0f;
        rollCorrection = 0.0f;
        yawCorrection = 0.0f;
        pitchCorrection = 0.0f;
        return;
    }

    // Calibration phase — accumulate samples, skip AHRS/PID until done
    if (_calibrating) {
        _calibrationStep();
        if (_calibrating) return;  // Not done yet
        // Calibration complete — proceed to AHRS+PID
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

    // Convert raw to physical units (MPU-native axes, bias-corrected)
    float gx = (float)_gyroRaw[0] / _gyroScale - _gyroBias[0];  // °/s
    float gy = (float)_gyroRaw[1] / _gyroScale - _gyroBias[1];
    float gz = (float)_gyroRaw[2] / _gyroScale - _gyroBias[2];

    float ax = (float)_accelRaw[0] / _accelScale;  // g
    float ay = (float)_accelRaw[1] / _accelScale;
    float az = (float)_accelRaw[2] / _accelScale;

    // Apply board rotation: remap MPU axes → aircraft axes
    _applyBoardRotation(gx, gy, gz, ax, ay, az);

    // Apply accel level calibration: rotate so calibrated "level" reads [0,0,1g]
    _applyAccelLevelRef(ax, ay, az);

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

    // --- Pitch PID (target: 0°, stabilize pitch) ---
    float pitchErr = -pitchDeg;
    pitchCorrection = _pidCompute(_pidPitch, pitchErr, dt,
                                   ZEPHYR_PID_PITCH_KP,
                                   ZEPHYR_PID_PITCH_KI,
                                   ZEPHYR_PID_PITCH_KD,
                                   ZEPHYR_PID_PITCH_IMAX);

    // Expose raw P/I/D terms for ornithopter waveform modulation (Nigredo)
    // Validatio: guard against NaN propagation from corrupt MPU6050 data
    if (isnan(pitchErr) || isnan(_pidPitch.integrator) || isnan(_pidPitch.lastDerivative)) {
        pitchPTerm    = 0.0f;
        pitchITerm    = 0.0f;
        pitchDTerm    = 0.0f;
        pitchErrorRate = 0.0f;
    } else {
        pitchPTerm    = ZEPHYR_PID_PITCH_KP * pitchErr;
        pitchITerm    = _pidPitch.integrator;
        pitchDTerm    = ZEPHYR_PID_PITCH_KD * _pidPitch.lastDerivative;
        pitchErrorRate = _pidPitch.lastDerivative;  // already low-pass filtered °/s
    }

#ifdef ORNITHOPTER_GEARBOX
    // Gearbox: rudder = yaw-only (roll + pitch go to leg servos separately)
    rudderCorrection = yawCorrection * ZEPHYR_RUDDER_YAW_GAIN;
#else
    // Servo: crest rudder = roll + yaw combined
    rudderCorrection = rollCorrection * ZEPHYR_RUDDER_ROLL_GAIN
                     + yawCorrection * ZEPHYR_RUDDER_YAW_GAIN;
#endif

    // Clamp to ±200µs
    if (rudderCorrection > ZEPHYR_RUDDER_CLAMP_US)
        rudderCorrection = ZEPHYR_RUDDER_CLAMP_US;
    if (rudderCorrection < -ZEPHYR_RUDDER_CLAMP_US)
        rudderCorrection = -ZEPHYR_RUDDER_CLAMP_US;
}