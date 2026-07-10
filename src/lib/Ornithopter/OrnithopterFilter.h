#pragma once
/*
  Ornithopter PWM Filter — Drop-in shim for devServoOutput.cpp

  Usage in devServoOutput.cpp:
    #include "Ornithopter/OrnithopterFilter.h"

    In servoNewChannelsAvailable():  call ornithopterNewChannels();
    In servoCalcAllChannels():       write(ch, ornithopterFilterChannel(ch, us));
    On link up/down events:          ornithopterLinkUp() / ornithopterLinkDown()
*/

#include <cstdint>

#ifdef ORNITHOPTER_MODE
#include "Ornithopter.h"

static inline void ornithopterUpdate() {
    ornithopter.update();
}

static inline uint16_t orniFilterChannel(uint8_t ch, uint16_t us) {
    if (ch == ORNITHOPTER_SERVO_LEFT)   return ornithopter.servoLeftUs;
    if (ch == ORNITHOPTER_SERVO_RIGHT)  return ornithopter.servoRightUs;
    if (ch == ORNITHOPTER_SERVO_RUDDER) return ornithopter.servoRudderUs;
    return us;
}

static inline void ornithopterOnLinkUp()   { ornithopter.onLinkUp(); }
static inline void ornithopterOnLinkDown() { ornithopter.onLinkDown(); }

#else
static inline void ornithopterUpdate() {}
static inline uint16_t orniFilterChannel(uint8_t, uint16_t us) { return us; }
static inline void ornithopterOnLinkUp() {}
static inline void ornithopterOnLinkDown() {}
#endif