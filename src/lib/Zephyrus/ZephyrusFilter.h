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
    // Bridge raw PID correction outputs for the 2-wing Mesozoic stabilizer
    // (flapping wings) — unlike gyroAileron/ElevatorCorrection these are NOT
    // gearbox-only; the waveform kernel consumes them directly.
    ornithopter.gyroRollCorrection  = zephyrus.rollCorrection;
    ornithopter.gyroYawCorrection   = zephyrus.yawCorrection;
    ornithopter.gyroPitchCorrection = zephyrus.pitchCorrection;
    // Bridge raw pitch PID terms for waveform modulation (Nigredo)
    ornithopter.gyroPitchPTerm     = zephyrus.pitchPTerm;
    ornithopter.gyroPitchITerm     = zephyrus.pitchITerm;
    ornithopter.gyroPitchDTerm     = zephyrus.pitchDTerm;
    ornithopter.gyroPitchErrorRate = zephyrus.pitchErrorRate;
#ifdef ORNITHOPTER_GEARBOX
    ornithopter.gyroAileronCorrection  = zephyrus.rollCorrection * ZEPHYR_GEARBOX_ROLL_GAIN;
    ornithopter.gyroElevatorCorrection = zephyrus.pitchCorrection * ZEPHYR_GEARBOX_PITCH_GAIN * ornithopter.aeroGainScale;
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