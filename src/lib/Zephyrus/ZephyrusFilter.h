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
    zephyrus.update(micros());
    ornithopter.gyroRudderCorrection = zephyrus.rudderCorrection;
}

static inline void zephyrusOnLinkUp()   { zephyrus.onLinkUp(); }
static inline void zephyrusOnLinkDown() { zephyrus.onLinkDown(); }

#else
static inline void zephyrusBegin() {}
static inline void zephyrusUpdate() {}
static inline void zephyrusOnLinkUp() {}
static inline void zephyrusOnLinkDown() {}
#endif