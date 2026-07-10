#pragma once
/*
  Ornithopter Layer for ExpressLRS PWM Receiver
  Inserted between CRSF channel decode and PWM servo output.
*/

#include <cstdint>
#include "OrnithopterConfig.h"
#include "OrnithopterWaveform.h"

// ExpressLRS globals we depend on (declared in crsf_protocol.h / rx_main.cpp)
extern uint32_t ChannelData[];

class Ornithopter {
public:
    bool enabled;
    bool linkUp;

    // CRSF raw channel values (172-1811 range)
    uint16_t voiceAileron;
    uint16_t voiceElevator;
    uint16_t voiceThrottle;
    uint16_t voiceRudder;
    uint16_t voiceArm;
    uint16_t voiceRudderFerocity;
    uint16_t voiceStrokeFerocity;
    uint16_t voiceReturnFerocity;
    uint16_t voiceCadence;
    uint16_t voiceAltitudeHold;

    // Computed servo outputs (988-2012 µs)
    uint16_t servoLeftUs;
    uint16_t servoRightUs;
    uint16_t servoRudderUs;

#ifdef ZEPHYRUS_ENABLED
    float gyroRudderCorrection;
#endif

    Ornithopter();
    bool update();
    void onLinkUp();
    void onLinkDown();
    void enterFailsafe();

private:
    FlappingOscillator _osc;
    uint32_t _lastUpdateUs;

    float _crsfToFloat(uint16_t raw, float outMin, float outMax);
    float _crsfToNorm(uint16_t raw);
    void  _readChannels();
    void  _computeMixer();
    static uint16_t _clampServo(uint16_t us);
};

extern Ornithopter ornithopter;