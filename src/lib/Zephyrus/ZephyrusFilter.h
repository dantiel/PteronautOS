#pragma once
/*
  Zephyrus Gyro Filter — Drop-in bridge shim for devServoOutput.cpp
  Mirrors OrnithopterFilter.h pattern exactly.

  Usage in devServoOutput.cpp:
    #include "../Zephyrus/ZephyrusFilter.h"

    In servosUpdate():   call zephyrusUpdate() before ornithopterUpdate()
    In event() onLinkUp: call zephyrusOnLinkUp()
    In failsafe:         call zephyrusOnLinkDown()
*/

#include <cstdint>

#ifdef ZEPHYRUS_ENABLED
#include "Zephyrus.h"
#include "../Ornithopter/Ornithopter.h"

extern Zephyrus zephyrus;

static inline void zephyrusBegin()    { zephyrus.begin(); }
static inline void zephyrusUpdate() {
    static uint32_t lastUs = 0;
    uint32_t now = micros();
    if (now - lastUs < 4000) return;  // 250Hz max — more than enough for mechanical rudder
    lastUs = now;
    zephyrus.update(now);
    ornithopter.gyroRudderCorrection = zephyrus.rudderCorrection;
#ifdef ORNITHOPTER_GEARBOX
    ornithopter.gyroAileronCorrection  = zephyrus.rollCorrection * ZEPHYR_GEARBOX_ROLL_GAIN;
    ornithopter.gyroElevatorCorrection = zephyrus.pitchCorrection * ZEPHYR_GEARBOX_PITCH_GAIN;
#endif
}

static inline void zephyrusOnLinkUp()   { zephyrus.onLinkUp(); }
static inline void zephyrusOnLinkDown() { zephyrus.onLinkDown(); }

#else
static inline void zephyrusBegin() {}
static inline void zephyrusUpdate() {}
static inline void zephyrusOnLinkUp() {}
static inline void zephyrusOnLinkDown() {}
#endif