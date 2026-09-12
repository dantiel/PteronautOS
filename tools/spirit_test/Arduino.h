#pragma once
#include <stdint.h>
#include <stddef.h>

// Minimal Arduino.h surrogate for the standalone spirit-side functional test.
// Provides the Stream base class the real MushinNoShin.cpp expects, plus a
// recording SerialType so the emitted frame bytes can be asserted directly.
class Stream {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t*, size_t) = 0;
};

class SerialType : public Stream {
public:
    static uint8_t  tx[512];
    static int16_t  txlen;
    static uint8_t  rx[512];
    static int16_t  rxlen;
    static int16_t  rxpos;
    void begin(uint32_t) {}
    void begin(uint32_t, uint32_t) {}
    void begin(uint32_t, uint32_t, int, int) {}
    int available() override { return rxlen - rxpos; }
    int read() override { return rxpos < rxlen ? rx[rxpos++] : -1; }
    size_t write(uint8_t b) override { if (txlen < 512) tx[txlen++] = b; return 1; }
    size_t write(const uint8_t* b, size_t n) override { for (size_t i = 0; i < n; i++) write(b[i]); return n; }
};

extern SerialType Serial1;
