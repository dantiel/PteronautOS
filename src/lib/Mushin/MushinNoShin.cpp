#include "MushinNoShin.h"

#if defined(MUSHIN_ENABLED)

#include <Arduino.h>

// ── Bridge serial selection ──────────────────────────────────────────
// The RP2040 muscle speaks MUSHIN on its UART1 (TX=8, RX=9). On the
// ESP8285 PWMP7 v1.1 the only free full-duplex pins are GPIO9/GPIO10, so the
// default is a SoftwareSerial there; on ESP32 we use the hardware UART1.
// Override the pins/baud via build flags in pteronautos-rx.ini.
#ifndef MUSHIN_BAUD
  #define MUSHIN_BAUD 57600
#endif

#if defined(PLATFORM_ESP8266)
  #ifndef MUSHIN_RX_PIN
    #define MUSHIN_RX_PIN 9   // ESP8285 GPIO9  ← RP2040 TX (bridge UART1 TX)
  #endif
  #ifndef MUSHIN_TX_PIN
    #define MUSHIN_TX_PIN 10  // ESP8285 GPIO10 → RP2040 RX (bridge UART1 RX)
  #endif
  #include <SoftwareSerial.h>
  static SoftwareSerial mushinSerial(MUSHIN_RX_PIN, MUSHIN_TX_PIN);
#else  // PLATFORM_ESP32
  #define mushinSerial Serial1
#endif

// Emit cadence divider: hardware UART can push intents at the full 333 Hz
// servo tick; SoftwareSerial TX is bit-banged and blocking, so it throttles to
// ~111 Hz (still ~5 samples per 20 Hz wingbeat stroke).
#ifndef MUSHIN_INTENT_DIVIDER
  #if defined(PLATFORM_ESP8266)
    #define MUSHIN_INTENT_DIVIDER 3
  #else
    #define MUSHIN_INTENT_DIVIDER 1
  #endif
#endif

static MushinNoShin mushin;
static uint8_t  mushinDivider = 0;

void mushinInit()
{
    mushinSerial.begin(MUSHIN_BAUD);
    mushin.begin(mushinSerial);
}

void mushinUpdate(uint32_t nowMs)
{
    mushin.update(nowMs);
}

bool mushinIsLinked()
{
    return mushin.isLinked();
}

void mushinEmitIntents(const uint16_t *us, uint8_t count)
{
    if (++mushinDivider < MUSHIN_INTENT_DIVIDER)
        return;
    mushinDivider = 0;
    mushin.emitIntents(us, count);
}

const MushinTelemetry &mushinTelemetry()
{
    return mushin.telemetry();
}

bool mushinTelemetryFresh(uint32_t nowMs)
{
    return mushin.telemetryFresh(nowMs);
}

// ── Class implementation ─────────────────────────────────────────────
void MushinNoShin::begin(Stream &serial)
{
    _serial = &serial;
    _state = MS_IDLE;
    _linked = false;
    _lastAnnounceMs = 0;
    _announcedServos = 0;
}

void MushinNoShin::update(uint32_t nowMs)
{
    if (!_serial)
        return;

    while (_serial->available())
    {
        uint8_t b = (uint8_t)_serial->read();
        switch (_state)
        {
        case MS_IDLE:
            if (b == MUSHIN_SYNC) { _state = MS_LEN; _xor = 0; }
            break;
        case MS_LEN:
            _len = b; _xor ^= b;
            _state = (_len <= MUSHIN_MAX_PAY) ? MS_TYPE : MS_IDLE;
            break;
        case MS_TYPE:
            _type = b; _xor ^= b; _idx = 0;
            _state = (_len == 0) ? MS_XOR : MS_PAY;
            break;
        case MS_PAY:
            _buf[_idx++] = b; _xor ^= b;
            if (_idx >= _len) _state = MS_XOR;
            break;
        case MS_XOR:
            _state = MS_IDLE;
            if (b != _xor)
                break;
            if (_type == MUSHIN_ANNOUNCE && _len >= 6)
            {
                _announcedServos = _buf[1];
                _linked = true;
                _lastAnnounceMs = nowMs;
            }
            else if (_type == MUSHIN_TELEMETRY && _len >= 6)
            {
                _tele.gyroRate   = (int16_t)(_buf[0] | (_buf[1] << 8));
                _tele.correction = (int16_t)(_buf[2] | (_buf[3] << 8));
                _tele.linked     = _buf[4];
                _tele.version    = _buf[5];
                _tele.fresh      = true;
                _lastTelemetryMs = nowMs;
            }
            break;
        }
    }

    // Heartbeat timeout → the spirit returns to its own muscle.
    if (_linked && (nowMs - _lastAnnounceMs > MUSHIN_ANNOUNCE_STALE_MS))
        _linked = false;
}

bool MushinNoShin::isLinked() const
{
    return _linked;
}

void MushinNoShin::emitIntents(const uint16_t *us, uint8_t count)
{
    if (!_serial)
        return;
    if (count > (MUSHIN_MAX_PAY / 2))
        count = MUSHIN_MAX_PAY / 2;

    uint8_t n = count * 2;
    uint8_t hdr[3] = { MUSHIN_SYNC, n, MUSHIN_INTENT };
    uint8_t x = (uint8_t)(n ^ MUSHIN_INTENT);
    _serial->write(hdr, 3);
    for (uint8_t i = 0; i < count; i++)
    {
        uint8_t lo = (uint8_t)(us[i] & 0xFF);
        uint8_t hi = (uint8_t)((us[i] >> 8) & 0xFF);
        _serial->write(lo); x ^= lo;
        _serial->write(hi); x ^= hi;
    }
    _serial->write(x);
}

#endif  // MUSHIN_ENABLED
