#include "MushinNoShin.h"

#if defined(MUSHIN_ENABLED)

#include <Arduino.h>

// EP2 companion mode owns the normal receiver/programming UART exclusively.
#define mushinSerial Serial // sole UART0 owner in companion firmware
#if defined(DEBUG_LOG) || defined(DEBUG_RCVR_LINKSTATS) || defined(ZEPHYRUS_ENABLED)
#error "Companion UART0 cannot share pins with receiver debug output or local Zephyrus I2C"
#endif

static MushinNoShin mushin;
static uint8_t motionBytes[Motion::wireSize];
static uint8_t motionPart = Motion::chunks;
static uint16_t motionGeneration = 0;
static uint32_t motionSentMs = 0;

void mushinEmitIntent(const Motion::Intent &intent, uint32_t nowMs)
{
    // One immutable in-flight snapshot, no backlog of obsolete commands.
    // 200 Hz ceiling, <=29.4 kB/s including framing; the 3 ms preparation
    // cadence normally yields <=167 Hz. No duplicate raw RC stream when linked.
    if (motionPart == Motion::chunks) {
        if (nowMs - motionSentMs < 5 || !Motion::valid(intent)) return;
        Motion::encode(intent, motionBytes);
        ++motionGeneration;
        motionPart = 0;
        motionSentMs = nowMs;
    }
    if (nowMs - motionSentMs > 30) { motionPart = Motion::chunks; return; }
    while (motionPart < Motion::chunks) {
        const size_t offset = motionPart * Motion::chunkSize;
        const size_t count = Motion::wireSize-offset < Motion::chunkSize ? Motion::wireSize-offset : Motion::chunkSize;
        uint8_t payload[59] = {uint8_t(motionGeneration), uint8_t(motionGeneration>>8), motionPart};
        memcpy(payload+3, motionBytes+offset, count);
        if (!Companion::send(mushinSerial, Motion::frameType, payload, count+3)) return;
        ++motionPart;
    }
}

void mushinInit()
{
    mushinSerial.begin(Companion::runtimeBaud);
    mushin.begin(mushinSerial);
}

void mushinEmitChannels(const uint32_t *channels)
{
    uint8_t packed[22];
    Companion::packChannels(channels, packed);
    Companion::send(mushinSerial, Companion::rcType, packed, sizeof(packed));
}

void mushinStop()
{
    motionPart = Motion::chunks; // never finish an old flying intent after STOP
    const uint8_t stop[] = {MUSHIN_SYNC, 0, 4, 4};
    Companion::send(mushinSerial, Companion::mushinType, stop, sizeof(stop));
}

void mushinEmitSweep(uint16_t us)
{
    // Raw bench sweep: a u16 µs payload inside the private 0x80 envelope. The
    // muscle writes it straight to its servos — no wave core, no mixer.
    uint8_t p[6];
    p[0] = MUSHIN_SYNC;
    p[1] = 2;                          // payload length
    p[2] = MUSHIN_SWEEP;               // spirit → muscle sweep
    p[3] = (uint8_t)(us & 0xFF);
    p[4] = (uint8_t)(us >> 8);
    p[5] = (uint8_t)(2 ^ MUSHIN_SWEEP ^ p[3] ^ p[4]);
    Companion::send(mushinSerial, Companion::mushinType, p, sizeof(p));
}

void mushinUpdate(uint32_t nowMs)
{
    mushin.update(nowMs);
}

bool mushinIsLinked()
{
    return mushin.isLinked();
}

uint8_t mushinMuscleVersion()
{
    return mushin.muscleVersion();
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
    _transport.reset();
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
        if (!_transport.feed((uint8_t)_serial->read(), nowMs) ||
            _transport.type() != Companion::mushinType) continue;
        // Return-channel payload is isolated inside one CRC-checked envelope.
        _state = MS_IDLE;
        for (uint8_t j = 0; j < _transport.length(); ++j) {
        uint8_t b = _transport.payload()[j];
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
                _announcedVersion = _buf[0];
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
    }

    // Heartbeat timeout → the spirit returns to its own muscle.
    if (_linked && (nowMs - _lastAnnounceMs > MUSHIN_ANNOUNCE_STALE_MS))
        _linked = false;
}

bool MushinNoShin::isLinked() const
{
    return _linked;
}

#endif  // MUSHIN_ENABLED