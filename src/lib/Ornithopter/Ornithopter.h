#pragma once
/*
  Ornithopter Layer for ExpressLRS PWM Receiver
  Inserted between CRSF channel decode and PWM servo output.

  Output: _f[] array indexed by ServoFunc tags.
  Profile determines which PWM index maps to which function.
*/

#include <cstdint>
#include "OrnithopterConfig.h"
#include "OrnithopterWaveform.h"

#if defined(ZEPHYRUS_ENABLED)
#include "../Zephyrus/ZephyrusConfig.h"   // for ZEPHYR_GEARBOX_CLAMP_US (only used in gearbox kernel)
#endif

extern uint32_t ChannelData[];

class Ornithopter {
public:
    bool enabled;
    bool linkUp;

    // CRSF raw channel values (172–1811 range)
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

    // Unified servo output — indexed by ServoFunc tag
    // _f[SF_LEFT_WING], _f[SF_RIGHT_WING], _f[SF_RUDDER],
    // _f[SF_MOTOR], _f[SF_VTAIL_LEFT], _f[SF_VTAIL_RIGHT],
    // _f[SF_ELEVATOR], _f[SF_BACK_LEFT_WING], _f[SF_BACK_RIGHT_WING]
    // Read via funcValue(func)
    uint16_t funcValue(uint8_t func) const { return _f[func]; }

#ifdef ZEPHYRUS_ENABLED
    float gyroRudderCorrection;
    float gyroAileronCorrection;   // µs offset for roll PID (gearbox only)
    float gyroElevatorCorrection;  // µs offset for pitch PID (gearbox only)
#endif

    Ornithopter();
    bool update();
    void onLinkUp();
    void onLinkDown();
    void enterFailsafe();

private:
    FlappingOscillator _osc;
    uint32_t _lastUpdateUs;
    uint16_t _f[SF_COUNT];  // servo output indexed by ServoFunc

    float _crsfToFloat(uint16_t raw, float outMin, float outMax);
    float _crsfToNorm(uint16_t raw);
    void  _readChannels();
    void  _computeServoMixer();    // waveform kernel
    void  _computeGearboxMixer();  // gearbox kernel
    static uint16_t _clampServo(uint16_t us);
};

extern Ornithopter ornithopter;