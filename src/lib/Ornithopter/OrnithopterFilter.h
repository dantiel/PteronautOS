#pragma once
/*
  Ornithopter PWM Filter — drop-in shim for devServoOutput.cpp

  Maps PWM output index → servo function → ornithopter._f[func].
  Profile descriptor in OrnithopterConfig.h defines the mapping.
*/

#include <cstdint>

#ifdef ORNITHOPTER_MODE
#include "Ornithopter.h"

static inline void ornithopterUpdate() {
    ornithopter.update();
}

static inline uint16_t orniFilterChannel(uint8_t ch, uint16_t us) {
    uint8_t func = PROFILE.funcMap[ch];
    if (func != SF_NONE) return ornithopter.funcValue(func);
    return us;
}

// Write zeros to all ornithopter-managed PWM indices on init
static inline void orniInitWrite(void (*write)(uint8_t, uint16_t)) {
    for (uint8_t ch = 0; ch < 7; ++ch) {
        if (PROFILE.funcMap[ch] != SF_NONE) write(ch, 0);
    }
}

static inline void ornithopterOnLinkUp()   { ornithopter.onLinkUp(); }
static inline void ornithopterOnLinkDown() { ornithopter.onLinkDown(); }

#else
static inline void ornithopterUpdate() {}
static inline uint16_t orniFilterChannel(uint8_t, uint16_t us) { return us; }
static inline void orniInitWrite(void (*)(uint8_t, uint16_t)) {}
static inline void ornithopterOnLinkUp() {}
static inline void ornithopterOnLinkDown() {}
#endif
