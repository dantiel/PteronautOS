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

// CRSF channel indices for stick override
enum StickCh : uint8_t {
    STK_AILERON          = 0,
    STK_ELEVATOR         = 1,
    STK_THROTTLE         = 2,
    STK_RUDDER           = 3,
    STK_ARM              = 4,
    STK_FREQ             = 5,   // flap frequency modulator
    STK_PROFILE          = 6,   // flight profile selector (multi-position)
    STK_COUNT            = 7
};

class Ornithopter {
public:
    bool enabled;
    bool linkUp;
    bool stickOverride;   // when true, _readChannels uses stickChannels instead of CRSF

    // CRSF raw channel values (172–1811 range)
    uint16_t voiceAileron;
    uint16_t voiceElevator;
    uint16_t voiceThrottle;
    uint16_t voiceRudder;
    uint16_t voiceArm;
    uint16_t voiceFreq;      // flap frequency modulator channel
    uint16_t voiceProfile;   // flight profile selector channel

    // Unified servo output — indexed by ServoFunc tag
    // _f[SF_LEFT_WING], _f[SF_RIGHT_WING], _f[SF_RUDDER],
    // _f[SF_MOTOR], _f[SF_VTAIL_LEFT], _f[SF_VTAIL_RIGHT],
    // _f[SF_ELEVATOR], _f[SF_BACK_LEFT_WING], _f[SF_BACK_RIGHT_WING]
    // Read via funcValue(func)
    uint16_t funcValue(uint8_t func) const { return _f[func]; }

    // ── Virtual stick injection ────────────────────────────────────
    uint16_t stickChannels[STK_COUNT]; // override values (172–1811 range)
    void setStickChannel(uint8_t ch, uint16_t val) { stickChannels[ch] = val; }
    void setStickOverride(bool on) { stickOverride = on; }

    // ── Flight profiles (multi-position channel) ─────────────────────
    FlightProfileParams flightProfiles[FLIGHT_PROFILE_COUNT];
    uint8_t activeFlightProfile;   // 0..2 (selected by STK_PROFILE channel)
    void applyFlightProfile(uint8_t idx);
        void setFlightProfileParams(uint8_t idx, float sf, float rf, int8_t glide, int8_t flapAng, float ail, float elev, float rudRng, float rudAmpDiff, float elevFerMix, float thrFerMix);

    // ── Runtime waveform/mixer params (init from OrnithopterConfig.h defaults) ──
    float   strokeFerocity;       // 0–100, waveform aggression
    float   returnFerocity;       // 0–100, upstroke speed
    int8_t  glideAngleDeg;        // -15…+15, static glide wing angle
        int8_t  flappingAngleDeg;     // -15…+15, flapping stroke centre offset (deg)
    float   aileronScale;         // 0–100, roll authority
    float   elevatorScale;        // 0–100, pitch authority
    uint16_t servoSpeed;          // ms per 60° stroke (25–250), kernel-level servo rate
    uint16_t flapBaseFreq;        // deci-Hz (10–200), CH6 frequency window ceiling
    uint16_t servoMinUs;          // µs, wing servo 0° pulse (500–1490)
    uint16_t servoMaxUs;          // µs, wing servo 180° pulse (1510–2500)
    int16_t  servoTrimUs[SF_COUNT]; // µs, per-servo correction (trim), signed
    float   rudderYawWeight;      // 0–100, rudder mix yaw component
    float   rudderRollWeight;     // 0–100, rudder mix roll component
    float   rudderFerocityRange;  // 0–100, amplitude of rudder-ferocity channel
    float   rudderAmplitudeDifferential; // 0–100, rudder → L/R differential flap amplitude
    float   elevatorFerocityMix;  // 0–100, extra ferocity per |elevator| deflection
    float   throttleFerocityMix;  // 0–100, throttle→ferocity coupling (dwell)
    float   elevonScale;          // 0–100, elevon mix authority (gearbox)
    uint16_t motorMinUs;          // µs, motor idle pulse (900–1200)
    uint16_t motorMaxUs;          // µs, motor full pulse (1800–2100)
        bool    glideMode;            // true = gearbox idle glide
        bool    benchMode;            // true = WiFi/panel bench mode (servos hold glide + trim)
    uint8_t hallSensorPin;        // GPIO pin for hall sensor (12–15)
    uint8_t ratchetThrottlePct;   // 0–100, detent step size
    uint16_t ratchetTimeoutMs;    // ms before ratchet releases
    char modelName[33];           // user-facing model identifier (WebUI + backup)

#ifdef ZEPHYRUS_ENABLED
    float gyroRudderCorrection;
    float gyroAileronCorrection;   // µs offset for roll PID (gearbox only)
    float gyroElevatorCorrection;  // µs offset for pitch PID (gearbox only)

    // Raw pitch PID terms from Zephyrus (Nigredo — waveform modulation)
    float gyroPitchPTerm;          // Raw P term
    float gyroPitchITerm;          // Raw I term
    float gyroPitchDTerm;          // Raw D term
    float gyroPitchErrorRate;      // Pitch error derivative °/s (for SSFF)

    // Ondas modulation gains (runtime — initialized from OrnithopterConfig)
    float cadenceGain;             // P→Phase Advance (was ondasGain)
    float ferocityDGain;           // D→Dwell ratio PD-blend (was ondasGain2)
    float balanceGain;             // I→Asymmetry bias (was ondasGain3)
    float ferocityPGain;           // P→Dwell ratio PD-blend
    float ferocityRollGain;        // Roll-axis ferocity (deferred)
    float ferocityYawGain;         // Yaw-axis ferocity (deferred)
    float warpGain;                // Dual-waveform blend (deferred)
    float warpYawGain;             // Yaw-axis warp (deferred)
    float anchorGain;              // k₂ damping delta (0=k₂=10 tight, 100=k₂=20)
    float resonanceGain;           // Phase-locked resonance gain (0=disabled)
    float ssffGain;                // Stroke-synchronous feed-forward
    float aeroGlideCoeff;          // Aeroelastic glide gain modulation
    float aeroFlapCoeff;           // Aeroelastic flap gain modulation
    float aeroGainScale;           // Computed aeroelastic gain (glide/flap based)

    // Resonance state
    float _resonanceAccum;          // Phase-locked accumulator (τ=0.15s leaky)

    // SSFF state (private to _computeServoMixer, persisted across calls)
    float _prevFlappingSin;
    float _ssffAccumError;
    int   _ssffAccumCount;
    float _ssffFerocityUpBias;
    float _ssffFerocityDownBias;
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
    static uint16_t _clampServo(int32_t us);
};

extern Ornithopter ornithopter;