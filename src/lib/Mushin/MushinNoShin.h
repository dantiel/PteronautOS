#pragma once
/*
  MushinNoShin (無心の心 — the no-mind bridge) — PteronautOS ⇄ YOSHIMITSU link.

  The spirit (ESP8285 RX) plans the wave; the muscle (RP2040) strikes.
  This module is the spirit's half of the no-mind bridge: it listens for the
  muscle's heartbeat (ANNOUNCE), and — while the link is fresh — streams the
  complete pilot mix as a Motion::Intent (the precomputed waveform, boundary,
  skew, mix, trims and output routing) instead of driving local PWM. The muscle
  evaluates the intent from its own phase oscillator; no servo µs and no wave
  samples cross the wire.

  MUSHIN_ENABLED selects an EP2 companion build: UART0 belongs to this module,
  while the RP2040 owns servos and gyro. No companion means no actuator output.
  The separate PWMP7 target retains its standalone PWM + local gyro path.

  CRSF at 420000 baud shares UART0 with ROM flashing at 115200. The Motion
  intent (type 0x81) is a chunked CRSF frame. The muscle's return channel
  travels inside a project-private 0x80 envelope carrying the legacy 0x9B frame:
    [0x9B][len][type][payload…][xor]    xor over len+type+payload
      0x02 ANNOUNCE  muscle → spirit   version + posture heartbeat (1 Hz)
      0x03 TELEMETRY muscle → spirit   gyro rate + correction
      0x04 STOP      spirit → muscle   RF/model-match loss, centring the muscle
  No dynamic memory, no delay(), no partial writes. Update both peers together;
  the former bare 0x9B SoftwareSerial transport is not accepted.
*/

#include <cstdint>
#include "../../../sketches/yoshimitsu/src/CompanionTransport.h"
#include "../../../sketches/yoshimitsu/src/PreparedMotion.h"

#define MUSHIN_SYNC      0x9B   // 無心 — the no-mind sync
#define MUSHIN_VER       1      // protocol version — reported in ANNOUNCE byte 0
#define MUSHIN_ANNOUNCE  0x02   // muscle → spirit: version + posture
#define MUSHIN_TELEMETRY 0x03   // muscle → spirit: gyro telemetry
#define MUSHIN_SWEEP     0x05   // spirit → muscle: raw µs bench sweep (u16 LE)
#define MUSHIN_MAX_PAY   16     // legacy 0x9B payload ceiling (announce/telemetry)

// Single UART0. No SoftwareSerial or second wiring pair.
#if defined(PLATFORM_ESP8266)
  #ifndef MUSHIN_RX_PIN
    #define MUSHIN_RX_PIN 3   // UART0 RX ← RP2040 TX
  #endif
  #ifndef MUSHIN_TX_PIN
    #define MUSHIN_TX_PIN 1   // UART0 TX → RP2040 RX
  #endif
#endif

#if defined(PLATFORM_ESP8266) && defined(MUSHIN_ENABLED)
static_assert(MUSHIN_RX_PIN == 3 && MUSHIN_TX_PIN == 1,
              "EP2 requires UART0 RX=3 / TX=1, not GPIO9/10");
#endif

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
    uint8_t announcedServoCount() const { return _announcedServos; }
    uint8_t muscleVersion() const { return _announcedVersion; }
    const MushinTelemetry &telemetry() const { return _tele; }
    bool telemetryFresh(uint32_t nowMs) const
    {
        return _tele.fresh && (nowMs - _lastTelemetryMs <= MUSHIN_TELEMETRY_STALE_MS);
    }

private:
    Companion::Parser _transport;
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
void mushinEmitChannels(const uint32_t *channels);
void mushinStop();
void mushinEmitSweep(uint16_t us);
void mushinEmitIntent(const Motion::Intent &intent, uint32_t nowMs);
uint8_t mushinMuscleVersion();
const MushinTelemetry &mushinTelemetry();
bool mushinTelemetryFresh(uint32_t nowMs);
#endif