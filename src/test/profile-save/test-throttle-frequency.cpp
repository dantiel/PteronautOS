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

    // Throttle → skew coupling: asymmetric thrust-vector steering.
    // Full throttle front-loads the downstroke (+), idle front-loads the
    // upstroke (−). Zero at mid-throttle, ±100 at the extremes at full mix.
    expectNear(orniThrottleSkewShift(1.0f, 100.0f), 100.0f);
    expectNear(orniThrottleSkewShift(0.0f, 100.0f), -100.0f);
    expectNear(orniThrottleSkewShift(0.5f, 100.0f), 0.0f);
    // Coupling scales linearly; 0% never shifts.
    expectNear(orniThrottleSkewShift(1.0f, 50.0f), 50.0f);
    expectNear(orniThrottleSkewShift(0.0f, 50.0f), -50.0f);
    expectNear(orniThrottleSkewShift(0.75f, 0.0f), 0.0f);
    // Out-of-range inputs clamp without exceeding the skew envelope.
    expectNear(orniThrottleSkewShift(2.0f, 100.0f), 100.0f);
    expectNear(orniThrottleSkewShift(-1.0f, 100.0f), -100.0f);
    expectNear(orniThrottleSkewShift(1.0f, 150.0f), 100.0f);

    // Aileron → differential skew coupling (roll steering): centred stick
    // never shifts; full deflection front-loads one wing and late-loads the
    // other (mirror-image shift). Coupling scales linearly and clamps.
    expectNear(orniAileronSkewShift(0.0f, 100.0f), 0.0f);
    expectNear(orniAileronSkewShift(1.0f, 100.0f), 100.0f);
    expectNear(orniAileronSkewShift(-1.0f, 100.0f), -100.0f);
    expectNear(orniAileronSkewShift(0.5f, 100.0f), 50.0f);
    expectNear(orniAileronSkewShift(1.0f, 50.0f), 50.0f);
    expectNear(orniAileronSkewShift(1.0f, 0.0f), 0.0f);
    expectNear(orniAileronSkewShift(2.0f, 100.0f), 100.0f);
    expectNear(orniAileronSkewShift(-2.0f, 100.0f), -100.0f);

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

    // Mirror-image aileron skew makes the two wings diverge mid-downstroke:
    // the LEFT wing (skew +50) and RIGHT wing (skew −50) must not produce
    // the same pulse — that divergence IS the roll torque.
    {
        const float theta = 1.2f;  // inside the downstroke half
        float pulseL = FlappingOscillator::shapeWave(theta, 0.0f, 0.0f, 3.14159265f, 0.0f, 50.0f, 0.0f);
        float pulseR = FlappingOscillator::shapeWave(theta, 0.0f, 0.0f, 3.14159265f, 0.0f, -50.0f, 0.0f);
        assert(std::fabs(pulseL - pulseR) > 0.0001f);
    }

    std::cout << "Waveform coupling control laws passed (frequency, skew, aileron-skew, skew-rate)\n";
    return 0;
}