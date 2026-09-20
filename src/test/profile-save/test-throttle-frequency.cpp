#include <cassert>
#include <cmath>
#include <iostream>

#include "../../lib/Ornithopter/OrnithopterConfig.h"
#include "../../lib/Ornithopter/OrnithopterWaveform.h"

static void expectNear(float actual, float expected)
{
    assert(std::fabs(actual - expected) < 0.00001f);
}

int main()
{
    // Endpoints: preserve today's independent CH6 mode at 0%, and follow
    // throttle exactly at 100%.
    expectNear(orniThrottleFrequencyCommand(0.8f, 0.2f, 0.0f), 0.8f);
    expectNear(orniThrottleFrequencyCommand(0.8f, 0.2f, 100.0f), 0.2f);

    // The mix itself is linear and continuous.
    expectNear(orniThrottleFrequencyCommand(0.8f, 0.2f, 25.0f), 0.65f);
    expectNear(orniThrottleFrequencyCommand(0.8f, 0.2f, 50.0f), 0.5f);
    expectNear(orniThrottleFrequencyCommand(0.8f, 0.2f, 75.0f), 0.35f);

    // Invalid external values cannot command outside the configured
    // frequency window or exceed the meaningful 100% coupling endpoint.
    expectNear(orniThrottleFrequencyCommand(-1.0f, 2.0f, 150.0f), 1.0f);
    expectNear(orniThrottleFrequencyCommand(2.0f, -1.0f, -50.0f), 1.0f);

    // At full coupling, increasing throttle monotonically increases the
    // frequency command, independent of CH6.
    float previous = -1.0f;
    for (int step = 0; step <= 100; ++step) {
        const float throttle = step * 0.01f;
        const float command = orniThrottleFrequencyCommand(0.73f, throttle, 100.0f);
        assert(command >= previous);
        expectNear(command, throttle);
        previous = command;
    }

    // Throttle → thrust-shape coupling: one aggression scalar α = expo(throttle)·mix
    // drives dwell AND centre in lockstep. Idle = neutral (no shift), full
    // throttle = full front-load (+100 centre) + full dwell boost (+8 ferocity).
    expectNear(orniThrottleThrustShape(1.0f, 100.0f, 0.0f).centreShift, 100.0f);
    expectNear(orniThrottleThrustShape(1.0f, 100.0f, 0.0f).dwellBoost, 8.0f);
    expectNear(orniThrottleThrustShape(0.0f, 100.0f, 0.0f).centreShift, 0.0f);
    expectNear(orniThrottleThrustShape(0.5f, 100.0f, 0.0f).centreShift, 50.0f);
    // Coupling scales linearly; 0% never shifts.
    expectNear(orniThrottleThrustShape(1.0f, 50.0f, 0.0f).centreShift, 50.0f);
    expectNear(orniThrottleThrustShape(0.75f, 0.0f, 0.0f).centreShift, 0.0f);
    // Out-of-range inputs clamp without exceeding the skew envelope.
    expectNear(orniThrottleThrustShape(2.0f, 100.0f, 0.0f).centreShift, 100.0f);
    expectNear(orniThrottleThrustShape(-1.0f, 100.0f, 0.0f).centreShift, 0.0f);
    expectNear(orniThrottleThrustShape(1.0f, 150.0f, 0.0f).centreShift, 100.0f);

    // Aileron → differential flap amplitude (roll): centred stick never
    // shifts; full deflection returns the full signed fraction (±mix) that
    // enlarges one stroke and collapses the other. Coupling scales linearly
    // and clamps to ±1.
    expectNear(orniAileronRollShift(0.0f, 100.0f), 0.0f);
    expectNear(orniAileronRollShift(1.0f, 100.0f), 1.0f);
    expectNear(orniAileronRollShift(-1.0f, 100.0f), -1.0f);
    expectNear(orniAileronRollShift(0.5f, 100.0f), 0.5f);
    expectNear(orniAileronRollShift(1.0f, 50.0f), 0.5f);
    expectNear(orniAileronRollShift(1.0f, 0.0f), 0.0f);
    expectNear(orniAileronRollShift(2.0f, 100.0f), 1.0f);
    expectNear(orniAileronRollShift(-2.0f, 100.0f), -1.0f);

    // Throttle-rate → transient skew boost/brake (slew): positive
    // throttle slew front-loads the downstroke (boost), negative slew
    // front-loads the upstroke (brake). A full stick slam (~5/s) at 100%
    // mix hits ±50; the term clamps to the skew envelope and is off at 0%.
    expectNear(orniThrottleSkewRateShift(5.0f, 100.0f), 50.0f);
    expectNear(orniThrottleSkewRateShift(-5.0f, 100.0f), -50.0f);
    expectNear(orniThrottleSkewRateShift(0.0f, 100.0f), 0.0f);
    expectNear(orniThrottleSkewRateShift(5.0f, 50.0f), 25.0f);
    expectNear(orniThrottleSkewRateShift(5.0f, 0.0f), 0.0f);
    expectNear(orniThrottleSkewRateShift(50.0f, 100.0f), 100.0f);
    expectNear(orniThrottleSkewRateShift(-50.0f, 100.0f), -100.0f);
    expectNear(orniThrottleSkewRateShift(5.0f, 150.0f), 50.0f);
    expectNear(orniAileronRollRateShift(5.0f, 100.0f), 0.5f);
    expectNear(orniAileronRollRateShift(-5.0f, 100.0f), -0.5f);
    expectNear(orniAileronRollRateShift(0.0f, 100.0f), 0.0f);
    expectNear(orniAileronRollRateShift(5.0f, 50.0f), 0.25f);
    expectNear(orniAileronRollRateShift(5.0f, 0.0f), 0.0f);
    expectNear(orniAileronRollRateShift(50.0f, 100.0f), 1.0f);
    expectNear(orniAileronRollRateShift(-50.0f, 100.0f), -1.0f);

    // Roll torque = amplitude differential, NOT centre-skew. Full aileron at
    // 100% mix doubles the LEFT stroke amplitude and collapses the RIGHT; the
    // mirror-image fractions sum to zero so the differential is symmetric
    // around the throttle-set amplitude.
    {
        const float amp = 40.0f;
        const float rollDiff = orniAileronRollShift(1.0f, 100.0f);
        const float ampL = amp * (1.0f + rollDiff);
        const float ampR = amp * (1.0f - rollDiff);
        expectNear(ampL, 80.0f);
        expectNear(ampR, 0.0f);
    }

    std::cout << "Waveform coupling control laws passed (frequency, thrust-shape, aileron-roll, skew-rate, aileron-roll-rate)\n";
    return 0;
}