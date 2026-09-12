#pragma once
/*
  MushinNoShin (無心の心 — the no-mind bridge) — PteronautOS ⇄ YOSHIMITSU link.

  The spirit (ESP8285 RX) plans the wave; the muscle (RP2040) strikes.
  This module is the spirit's half of the no-mind bridge: it listens for the
  muscle's heartbeat (ANNOUNCE), and — while the link is fresh — streams the
  unified waveform+gyro result as servo intents instead of driving local PWM.

  Standalone-first: PteronautOS keeps its own PWM + gyro (Zephyrus) untouched
  until MUSHIN_ENABLED is compiled in — the shared-compute mode where the
  muscle owns the servos and the gyro. No RP2040 attached ⇒ no change.

  Wire format mirrors sketches/yoshimitsu/src/Yoshimitsu.h MUSHIN v1:
    [0x9B][len][type][payload…][xor]    xor over len+type+payload
      0x01 INTENT    spirit → muscle   v1: 11-byte wave parameters (the muscle
                                       computes phase + shapeWave locally, so
                                       the bit-banged bridge no longer limits
                                       flapping resolution); v0: n × uint16
                                       servo µs (little-endian) as fallback.
      0x02 ANNOUNCE  muscle → spirit   version + posture heartbeat (1 Hz)
      0x03 TELEMETRY muscle → spirit   gyro rate + correction
  Version handshake: the muscle announces its protocol version in ANNOUNCE
  byte 0. A v1 muscle gets parameter frames; a v0 muscle keeps the old µs
  path. The muscle parses by payload length (11 → v1, else even → v0), so
  both generations coexist on the same wire.

  No dynamic memory, no delay(), never blocking. Graceful degradation: if the
  heartbeat falls quiet, the spirit returns to its own local PWM.
*/

#include <cstdint>

#define MUSHIN_SYNC      0x9B   // 無心 — the no-mind sync
#define MUSHIN_VER       1      // protocol version — 1 = parameter intents
#define MUSHIN_INTENT    0x01   // spirit → muscle: servo intents / wave params
#define MUSHIN_ANNOUNCE  0x02   // muscle → spirit: version + posture
#define MUSHIN_TELEMETRY 0x03   // muscle → spirit: gyro telemetry
#define MUSHIN_MAX_PAY   16     // 8 servos × 2 bytes
#define MUSHIN_INTENT_V1_LEN 11 // v1 parameter intent payload (MushinIntentV1)

#ifndef MUSHIN_ANNOUNCE_STALE_MS
  #define MUSHIN_ANNOUNCE_STALE_MS 1500   // announce is 1 Hz; 1.5× grace before unlink
#endif

#ifndef MUSHIN_TELEMETRY_STALE_MS
  #define MUSHIN_TELEMETRY_STALE_MS 1500  // telemetry is 1 Hz; same grace before stale
#endif

#ifndef MUSHIN_GYRO_SCALE_LSB
  #define MUSHIN_GYRO_SCALE_LSB 131       // mirrors Yoshimitsu_Loadout.h GYRO_SCALE_LSB_DEFAULT (±250 dps)
#endif

// The muscle's return channel — parsed from MUSHIN_TELEMETRY frames. Sent at
// 1 Hz (same cadence as ANNOUNCE): raw gyro rate (LSB), the µs correction the
// muscle's PID applied, and the muscle's own view of the link.
// MUSHIN v1 parameter intent — the spirit plans the wave, the muscle strikes.
// Payload layout (11 bytes, little-endian multi-byte):
//   [0..1]  throttle       u16   0..1000   (throttlePct × 1000)
//   [2]     flapFreq       u8    10..200   deci-Hz (0 = cadence decay)
//   [3]     ferocity       u8    0..100    post-mix stroke/return ferocity
//   [4]     skew           i8    -100..100 post-mix stroke skew (return = −skew)
//   [5]     slew           u8    0..255    servoSpeed ms/60° (0 = unlimited)
//   [6]     stance         u8    0..5      active flight profile
//   [7..8]  setpointRoll   i16   ±250      dps stick→rate setpoint (muscle PID)
//   [9..10] setpointPitch  i16   ±250      dps stick→rate setpoint
struct MushinIntentV1
{
    uint16_t throttle = 0;
    uint8_t  flapFreq = 0;
    uint8_t  ferocity = 0;
    int8_t   skew = 0;
    uint8_t  slew = 0;
    uint8_t  stance = 0;
    int16_t  setpointRoll = 0;
    int16_t  setpointPitch = 0;
};

// The muscle's return channel — parsed from MUSHIN_TELEMETRY frames. Sent at
// 1 Hz (same cadence as ANNOUNCE): raw gyro rate (LSB), the µs correction the
// muscle's PID applied, and the muscle's own view of the link.
struct MushinTelemetry
{
    int16_t gyroRate = 0;   // raw yaw-rate LSB (±250 dps full-scale on the muscle)
    int16_t correction = 0; // µs added to the crest servo by the muscle's PID
    uint8_t linked = 0;     // the muscle's own view of the link (1 = linked)
    uint8_t version = 0;    // muscle protocol version
    bool    fresh = false;  // set on every parsed TELEMETRY frame

    float gyroDps() const { return (float)gyroRate / MUSHIN_GYRO_SCALE_LSB; }
};

class Stream;

class MushinNoShin
{
public:
    void begin(Stream &serial);
    void update(uint32_t nowMs);
    bool isLinked() const;
    void emitIntents(const uint16_t *us, uint8_t count);
    void emitIntentV1(const MushinIntentV1 &p);
    uint8_t announcedServoCount() const { return _announcedServos; }
    uint8_t muscleVersion() const { return _announcedVersion; }
    const MushinTelemetry &telemetry() const { return _tele; }
    bool telemetryFresh(uint32_t nowMs) const
    {
        return _tele.fresh && (nowMs - _lastTelemetryMs <= MUSHIN_TELEMETRY_STALE_MS);
    }

private:
    enum : uint8_t { MS_IDLE, MS_LEN, MS_TYPE, MS_PAY, MS_XOR };

    Stream  *_serial = nullptr;
    uint8_t  _state  = MS_IDLE;
    uint8_t  _len = 0, _type = 0, _idx = 0, _xor = 0;
    uint8_t  _buf[MUSHIN_MAX_PAY];
    uint32_t _lastAnnounceMs = 0;
    uint8_t  _announcedServos = 0;
    uint8_t  _announcedVersion = 0;
    bool     _linked = false;
    MushinTelemetry _tele;
    uint32_t _lastTelemetryMs = 0;
};

#if defined(MUSHIN_ENABLED)
// Bridge singleton + platform serial (see MushinNoShin.cpp).
void mushinInit();
void mushinUpdate(uint32_t nowMs);
bool mushinIsLinked();
void mushinEmitIntents(const uint16_t *us, uint8_t count);
void mushinEmitIntentV1(const MushinIntentV1 &p);
uint8_t mushinMuscleVersion();
const MushinTelemetry &mushinTelemetry();
bool mushinTelemetryFresh(uint32_t nowMs);
#endif