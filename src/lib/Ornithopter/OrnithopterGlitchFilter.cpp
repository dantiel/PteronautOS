#include "Ornithopter.h"

// ─────────────────────────────────────────────────────────────────────
//  GLITCH FILTER — CRSF channel glitch filter
// ─────────────────────────────────────────────────────────────────────
// Rejects single-frame channel corruptions (slow-decode / RF glitch) that would
// otherwise slam a wing servo. Per-channel delta + 2-frame persistence + collective
// ghost detection (≥N channels anomalous = whole frame rejected). Ported from
// Gralha Azul's verificarCanal, adapted to PteronautOS's decoupled mixer/CRSF
// cadence (the mixer ticks at 333Hz while CRSF arrives at 150–500Hz).
// Baselines on the first GENUINE frame after link-up (pre-link frames pass through
// unfiltered) and self-heals after 20 consecutive collective rejections.

void Ornithopter::_resetGlitchFilter()
{
    _glitchInitialized = false;
    _glitchGhostStreak = 0;
    for (uint8_t i = 0; i < STK_COUNT; i++) {
        _glitchRef[i]       = 992;
        _glitchCandidate[i] = 992;
        _glitchPersist[i]   = 0;
        _glitchLastRaw[i]   = 0xFFFF;  // sentinel — no CRSF frame can match this
    }
}

void Ornithopter::_applyGlitchFilter() {
    const uint16_t raw[STK_COUNT] = {
        (uint16_t)ChannelData[ORNI_CH_AILERON],
        (uint16_t)ChannelData[ORNI_CH_ELEVATOR],
        (uint16_t)ChannelData[ORNI_CH_THROTTLE],
        (uint16_t)ChannelData[ORNI_CH_RUDDER],
        (uint16_t)ChannelData[ORNI_CH_ARM],
        (uint16_t)ChannelData[ORNI_CH_FREQ],
        (uint16_t)ChannelData[ORNI_CH_PROFILE],
    };
    uint16_t* out[STK_COUNT] = {
        &voiceAileron, &voiceElevator, &voiceThrottle, &voiceRudder,
        &voiceArm, &voiceFreq, &voiceProfile,
    };
    // Arm + profile are switched channels that legitimately jump full-range
    // (172↔1811); everything else is a stick/pot that moves smoothly.
    const uint16_t deltaMaxCanal[STK_COUNT] = {
        glitchDeltaMax, glitchDeltaMax, glitchDeltaMax, glitchDeltaMax,
        glitchDeltaArm, glitchDeltaMax, glitchDeltaArm,
    };

    // Track frame freshness continuously — even while disabled or pre-link —
    // so the baseline is taken on the first GENUINE frame after link-up,
    // not on stale failsafe/held channel data (the old code baselined on the
    // first mixer tick and could lock the refs to garbage forever).
    bool anyChanged = false;
    for (uint8_t i = 0; i < STK_COUNT; i++) {
        if (raw[i] != _glitchLastRaw[i]) anyChanged = true;
        _glitchLastRaw[i] = raw[i];
    }
    // Arm-debounce clock: advances on DISTINCT CRSF frames (not mixer ticks),
    // so a low voiceArm held across rejected frames counts once per real frame.
    if (anyChanged) _glitchFrameTick++;

    if (!glitchActive) {
        for (uint8_t i = 0; i < STK_COUNT; i++) *out[i] = raw[i];
        _glitchInitialized = false;   // re-baseline on re-enable
        return;
    }

    // No link yet — raw passthrough (the failsafe already centres the servos).
    // Stay un-baselined so the first real frame after link-up sets the refs.
    if (!linkUp) {
        for (uint8_t i = 0; i < STK_COUNT; i++) *out[i] = raw[i];
        _glitchInitialized = false;
        return;
    }

    if (!_glitchInitialized) {
        if (!anyChanged) {
            // Stale held frame — wait for the first real frame after link-up.
            for (uint8_t i = 0; i < STK_COUNT; i++) *out[i] = raw[i];
            return;
        }
        // First genuine frame after link-up — accept unconditionally
        // (Gralha Azul's rule: the first frame initializes the refs).
        _glitchInitialized = true;
        for (uint8_t i = 0; i < STK_COUNT; i++) {
            _glitchRef[i]       = raw[i];
            _glitchCandidate[i] = raw[i];
            _glitchPersist[i]   = 0;
            *out[i]             = raw[i];
        }
        return;
    }

    // Same CRSF frame re-read (333Hz mixer vs 150Hz link) — voice* already
    // holds the accepted values; nothing to do.
    if (!anyChanged) return;

    // Pass 1: count channels exceeding their per-frame delta (simultaneity).
    uint8_t anomalous = 0;
    for (uint8_t i = 0; i < STK_COUNT; i++) {
        uint16_t d = (raw[i] > _glitchRef[i]) ? (uint16_t)(raw[i] - _glitchRef[i])
                                                : (uint16_t)(_glitchRef[i] - raw[i]);
        if (d > deltaMaxCanal[i]) anomalous++;
    }

    if (anomalous >= glitchSimultThreshold) {
        // Collective ghost — reject the whole frame, keep last accepted values.
        // Self-heal: RF noise cannot persist for many frames — if it does, the
        // world has changed (failsafe recovery, profile switch) or the refs are
        // stale, so force a re-baseline instead of freezing the servos forever.
        for (uint8_t i = 0; i < STK_COUNT; i++) _glitchPersist[i] = 0;
        if (++_glitchGhostStreak >= GLITCH_GHOST_REBASELINE_FRAMES) {
            _glitchGhostStreak = 0;
            _glitchInitialized = false;   // next fresh frame re-baselines
        }
        return;
    }
    _glitchGhostStreak = 0;

    // Pass 2: per-channel delta + 2-frame persistence confirmation.
    for (uint8_t i = 0; i < STK_COUNT; i++) {
        uint16_t d = (raw[i] > _glitchRef[i]) ? (uint16_t)(raw[i] - _glitchRef[i])
                                                : (uint16_t)(_glitchRef[i] - raw[i]);
        if (d <= deltaMaxCanal[i]) {
            *out[i]              = raw[i];
            _glitchRef[i]        = raw[i];
            _glitchPersist[i]    = 0;
        } else if (_glitchPersist[i] == 0) {
            _glitchCandidate[i]  = raw[i];
            _glitchPersist[i]    = 1;   // frozen this frame — await confirmation
        } else {
            uint16_t dc = (raw[i] > _glitchCandidate[i]) ? (uint16_t)(raw[i] - _glitchCandidate[i])
                                                           : (uint16_t)(_glitchCandidate[i] - raw[i]);
            if (dc <= glitchDeltaPersist) {
                *out[i]              = raw[i];   // confirmed (2 consecutive frames)
                _glitchRef[i]        = raw[i];
                _glitchPersist[i]    = 0;
            } else {
                _glitchCandidate[i]  = raw[i];   // inconsistent — new candidate, still frozen
                _glitchPersist[i]    = 1;
            }
        }
    }
}
