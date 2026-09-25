#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// One UART, one framing owner. Standard CRSF RC frames plus a project-private
// 0x80 envelope containing the existing MUSHIN frame (including its XOR).
// This private envelope is for the paired PteronautOS/Yoshimitsu link only.
namespace Companion {
constexpr uint32_t runtimeBaud = 420000;
constexpr uint32_t flashBaud = 115200;
constexpr uint8_t rcType = 0x16;
constexpr uint8_t mushinType = 0x80;
constexpr size_t maxFrame = 64;

inline uint8_t crc(const uint8_t* p, size_t n) {
    uint8_t c = 0;
    while (n--) {
        c ^= *p++;
        for (int b = 0; b < 8; ++b) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0xd5) : (uint8_t)(c << 1);
    }
    return c;
}

inline size_t encode(uint8_t type, const uint8_t* payload, size_t n, uint8_t* out) {
    if (n > maxFrame - 4) return 0;
    out[0] = 0xc8; out[1] = (uint8_t)(n + 2); out[2] = type;
    memcpy(out + 3, payload, n);
    out[n + 3] = crc(out + 2, n + 1); // length is NOT part of the CRSF CRC
    return n + 4;
}

template<class Serial> bool send(Serial& serial, uint8_t type, const uint8_t* payload, size_t n) {
    uint8_t frame[maxFrame];
    size_t size = encode(type, payload, n, frame);
    // Drop an update instead of blocking the RF loop or emitting a partial frame.
    if (!size || serial.availableForWrite() < (int)size) return false;
    return serial.write(frame, size) == size;
}

struct Parser {
    uint8_t frame[maxFrame] = {};
    uint8_t used = 0;
    uint32_t lastMs = 0;
    void reset() { used = 0; }
    bool feed(uint8_t b, uint32_t now) {
        if (used && now - lastMs > 5) reset();
        lastMs = now;
        if (!used) {
            if (b == 0xc8 || b == 0xee) frame[used++] = b;
            return false;
        }
        if (used == 1 && (b < 2 || b > maxFrame - 2)) { reset(); return false; }
        frame[used++] = b;
        if (used < 2 || used != frame[1] + 2) return false;
        const uint8_t size = used;
        reset();
        return crc(frame + 2, size - 3) == frame[size - 1];
    }
    uint8_t type() const { return frame[2]; }
    uint8_t length() const { return frame[1] - 2; }
    const uint8_t* payload() const { return frame + 3; }
};

inline void packChannels(const uint32_t* channels, uint8_t* payload) {
    memset(payload, 0, 22);
    for (unsigned ch = 0; ch < 16; ++ch)
        for (unsigned bit = 0; bit < 11; ++bit)
            if (channels[ch] & (1U << bit)) payload[(ch * 11 + bit) / 8] |= 1U << ((ch * 11 + bit) % 8);
}
inline void unpackChannels(const uint8_t* payload, uint16_t* channels, unsigned count) {
    if (count > 16) count = 16;
    for (unsigned ch = 0; ch < count; ++ch) {
        channels[ch] = 0;
        for (unsigned bit = 0; bit < 11; ++bit)
            if (payload[(ch * 11 + bit) / 8] & (1U << ((ch * 11 + bit) % 8))) channels[ch] |= 1U << bit;
    }
}
}
