#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <cmath>
#ifndef PROGMEM
#define PROGMEM
#endif
#include "MotionWaveform.h"

// Alpha wire contract, shared by both processors. No heap, packed C structs,
// host-endian fields or queued trajectories. Only complete intents go live.
namespace Motion {
constexpr uint8_t frameType = 0x81;
constexpr size_t wireSize = 23 * 4 + 13 * 2 + 8; // 126, explicitly encoded below
constexpr size_t chunkSize = 56;
constexpr uint8_t chunks = (wireSize + chunkSize - 1) / chunkSize;
constexpr float twoPi = 6.283185307f;
inline float clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
struct Half {
    float dwell = 0, inverseRamp = 1, pointPosition = 0;
};
inline Half prepareHalf(float ferocity) {
    const float f = clamp(ferocity, 0, 8) * 0.125f;
    const float d = f * kWaveMaxDwell;
    return {d * 0.5f, 1.0f / (1.0f - d), (2 * f - f * f) * (kWavePointedRows - 1)};
}
struct Intent {
    Half half[4]; // left down/up, right down/up
    float boundary = 0.5f, inverseDown = 2, inverseUp = 2;
    float skew[2] = {}, mix = 0, hz = 0;
    float centre[2] = {100, 100}, amplitude[2] = {};
    uint16_t minimum = 1000, maximum = 2000;
    int16_t trim[2] = {}, backTrim[2] = {};
    uint16_t output[7] = {1500,1500,1500,1500,1500,1500,1500};
    uint8_t kind[7] = {7,7,7,7,7,7,7}; // 0=fixed, 1/2=front, 3/4=back, 5=rudder, 6=motor, 7=unused
    uint8_t flapping = 0;
};
inline void prepare(Intent& r, float boundaryRadians, float ld, float lu, float rd, float ru,
                    float mix, float downSkew, float upSkew) {
    r.boundary = clamp(boundaryRadians / twoPi, 0.001f, 0.999f);
    r.inverseDown = 1 / r.boundary;
    r.inverseUp = 1 / (1 - r.boundary);
    r.half[0] = prepareHalf(ld); r.half[1] = prepareHalf(lu);
    r.half[2] = prepareHalf(rd); r.half[3] = prepareHalf(ru);
    r.mix = clamp(mix * 0.01f, 0, 1);
    r.skew[0] = clamp(downSkew * 0.01f, -1, 1);
    r.skew[1] = clamp(upSkew * 0.01f, -1, 1);
}
inline float evaluateHalf(const Half& h, float t, float mix) {
    const float plateau = t < h.dwell ? 1 : t > 1 - h.dwell ? -1
        : waveCosHalfLerp(clamp((t - h.dwell) * h.inverseRamp, 0, 1));
    if (mix == 0) return plateau;
    const int row = h.pointPosition >= kWavePointedRows - 1 ? kWavePointedRows - 2 : (int)h.pointPosition;
    const float k = h.pointPosition - row;
    const float p = t * kWaveCosHalfN;
    const int col = p >= kWaveCosHalfN ? kWaveCosHalfN - 1 : (int)p;
    const float f = p - col;
    const float a = kWavePointed[row][col] + (kWavePointed[row][col+1] - kWavePointed[row][col]) * f;
    const float b = kWavePointed[row+1][col] + (kWavePointed[row+1][col+1] - kWavePointed[row+1][col]) * f;
    const float pointed = h.pointPosition < 0.0001f ? waveCosHalfLerp(t) : a + (b-a)*k;
    return plateau + (pointed - plateau) * mix;
}
inline void waves(const Intent& r, float phase, float& left, float& right) {
    const bool down = phase < r.boundary;
    float t = down ? phase * r.inverseDown : (phase - r.boundary) * r.inverseUp;
    t = clamp(t, 0, 1);
    t = clamp(t + r.skew[down ? 0 : 1] * t * (1-t), 0, 1);
    const unsigned h = down ? 0 : 1;
    const float sign = down ? 1 : -1;
    left = sign * evaluateHalf(r.half[h], t, r.mix);
    right = sign * evaluateHalf(r.half[h+2], t, r.mix);
}
inline uint16_t safePulse(int32_t v) { return v < 500 ? 500 : v > 2500 ? 2500 : (uint16_t)v; }
inline void outputs(const Intent& r, float phase, uint16_t* out) {
    float l = 0, rr = 0;
    if (r.flapping) waves(r, phase, l, rr);
    const float w[2] = {l, rr};
    uint16_t wing[2];
    for (unsigned i=0; i<2; ++i) {
        const int angle = (int)clamp(r.centre[i] + r.amplitude[i] * w[i], 0, 180);
        wing[i] = safePulse(r.minimum + (uint32_t)angle * (r.maximum-r.minimum) / 180 + r.trim[i]);
    }
    for (unsigned i=0; i<7; ++i) {
        const uint8_t k = r.kind[i];
        out[i] = k == 0 || k > 4 ? r.output[i] : k <= 2 ? wing[k-1] : safePulse(wing[k-3] + r.backTrim[k-3]);
    }
}
// Explicit little-endian scalar codec (32-bit IEEE floats on both targets).
struct Codec {
    uint8_t* bytes; size_t pos = 0; bool reading;
    void u16(uint16_t& v) {
        if (reading) v = bytes[pos] | (uint16_t(bytes[pos+1]) << 8);
        else { bytes[pos] = v; bytes[pos+1] = v >> 8; }
        pos += 2;
    }
    void i16(int16_t& v) { uint16_t u = (uint16_t)v; u16(u); if (reading) v = (int16_t)u; }
    void f32(float& f) {
        static_assert(sizeof(float)==4, "Motion requires float32");
        uint32_t u; memcpy(&u, &f, 4);
        uint16_t lo=u, hi=u>>16; u16(lo); u16(hi);
        if (reading) { u = uint32_t(lo) | (uint32_t(hi)<<16); memcpy(&f,&u,4); }
    }
    void u8(uint8_t& v) { if (reading) v=bytes[pos]; else bytes[pos]=v; ++pos; }
};
inline void code(Intent& r, Codec& c) {
    for (auto& h:r.half) { c.f32(h.dwell); c.f32(h.inverseRamp); c.f32(h.pointPosition); }
    c.f32(r.boundary); c.f32(r.inverseDown); c.f32(r.inverseUp);
    for(auto& v:r.skew) c.f32(v);
    c.f32(r.mix); c.f32(r.hz);
    for(auto& v:r.centre) c.f32(v);
    for(auto& v:r.amplitude) c.f32(v);
    c.u16(r.minimum); c.u16(r.maximum);
    for(auto& v:r.trim) c.i16(v);
    for(auto& v:r.backTrim) c.i16(v);
    for(auto& v:r.output) c.u16(v);
    for(auto& v:r.kind) c.u8(v);
    c.u8(r.flapping);
}
inline bool range(float v,float lo,float hi) { return std::isfinite(v) && v>=lo && v<=hi; }
inline bool valid(const Intent& r) {
    for(const auto& h:r.half)
        if(!range(h.dwell,0,0.491f) || !range(h.inverseRamp,1,50.01f) || !range(h.pointPosition,0,15) ||
           std::fabs((1-2*h.dwell)*h.inverseRamp-1)>0.001f) return false;
    if(!range(r.boundary,0.001f,0.999f) || !range(r.inverseDown,1,1001) || !range(r.inverseUp,1,1001)) return false;
    if(std::fabs(r.boundary*r.inverseDown-1)>0.001f || std::fabs((1-r.boundary)*r.inverseUp-1)>0.001f) return false;
    if(!range(r.mix,0,1) || !range(r.hz,0,20) || r.flapping>1 || r.minimum<500 || r.maximum>2500 || r.minimum>=r.maximum) return false;
    for(unsigned i=0;i<2;++i)
        if(!range(r.skew[i],-1,1) || !range(r.centre[i],-360,540) || !range(r.amplitude[i],-440,440) || r.trim[i]<-2000 || r.trim[i]>2000 || r.backTrim[i]<-2000 || r.backTrim[i]>2000) return false;
    for(unsigned i=0;i<7;++i) if(r.kind[i]>7 || r.output[i]<500 || r.output[i]>2500) return false;
    return true;
}
inline void encode(Intent r,uint8_t* bytes) { Codec c{bytes,0,false}; code(r,c); }
inline bool decode(uint8_t* bytes,Intent& r) { Codec c{bytes,0,true}; code(r,c); return c.pos==wireSize && valid(r); }

struct Receiver {
    uint8_t bytes[wireSize] = {}, next = 0;
    uint16_t generation = 0;
    uint32_t began = 0;
    void reset() { next=0; }
    bool accept(const uint8_t* p,size_t n,uint32_t now,Intent& active) {
        if(n<4) return false;
        const uint16_t gen=p[0] | uint16_t(p[1])<<8;
        const uint8_t index=p[2];
        if(index>=chunks) return false;
        const size_t offset=index*chunkSize;
        const size_t count=wireSize-offset < chunkSize ? wireSize-offset : chunkSize;
        if(n!=count+3) { reset(); return false; }
        if(index==0) { generation=gen; next=0; began=now; }
        if(index!=next || gen!=generation || now-began>30) { reset(); return false; }
        memcpy(bytes+offset,p+3,count);
        if(++next!=chunks) return false;
        reset(); Intent candidate;
        if(!decode(bytes,candidate)) return false;
        active=candidate; return true;
    }
};
}
