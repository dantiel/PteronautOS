#include <cassert>
#include <cmath>
#include <iostream>

#include "../../lib/Ornithopter/OrnithopterConfig.h"

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

    std::cout << "Throttle-frequency coupling control law passed\n";
    return 0;
}
