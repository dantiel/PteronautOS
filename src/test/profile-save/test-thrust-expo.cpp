#include <cassert>
#include <cmath>
#include <iostream>

#include "../../lib/Ornithopter/OrnithopterConfig.h"

static void expectNear(float actual, float expected, float tolerance = 0.00001f)
{
    assert(std::fabs(actual - expected) < tolerance);
}

int main()
{
    // Expo 0 is exactly the pre-existing linear blend, for every throttle.
    for (float x : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f})
        expectNear(orniThrottleThrustExpo(x, 0.0f), x);

    // Both endpoints stay pinned regardless of the curve, so the mix still
    // sets the authority ceiling and idle stays neutral.
    for (float expo : {-100.0f, -50.0f, 0.0f, 50.0f, 100.0f}) {
        expectNear(orniThrottleThrustExpo(0.0f, expo), 0.0f);
        expectNear(orniThrottleThrustExpo(1.0f, expo), 1.0f);
        expectNear(orniThrottleThrustExpo(-0.5f, expo), 0.0f);   // input clamp
        expectNear(orniThrottleThrustExpo(1.5f, expo), 1.0f);    // input clamp
    }

    // Soft (+) arrives late, direct (−) arrives early. At half stick the
    // quadratic blend is exact: 0.25 soft, 0.75 direct.
    expectNear(orniThrottleThrustExpo(0.5f,  100.0f), 0.25f);
    expectNear(orniThrottleThrustExpo(0.5f, -100.0f), 0.75f);
    assert(orniThrottleThrustExpo(0.5f, 50.0f) < 0.5f);
    assert(orniThrottleThrustExpo(0.5f, -50.0f) > 0.5f);

    // The curve stays monotone in throttle for every expo, and the expo itself
    // is clamped to ±100.
    for (float expo : {-250.0f, -100.0f, -33.0f, 0.0f, 33.0f, 100.0f, 250.0f}) {
        float previous = orniThrottleThrustExpo(0.0f, expo);
        for (int step = 1; step <= 100; ++step) {
            const float current = orniThrottleThrustExpo(step / 100.0f, expo);
            assert(current >= previous);
            assert(current >= 0.0f && current <= 1.0f);
            previous = current;
        }
    }
    for (float x : {0.2f, 0.5f, 0.8f}) {
        expectNear(orniThrottleThrustExpo(x, 150.0f), orniThrottleThrustExpo(x, 100.0f));
        expectNear(orniThrottleThrustExpo(x, -150.0f), orniThrottleThrustExpo(x, -100.0f));
    }

    // Thrust shape: mix sets the ceiling, the expo only curves the approach.
    const OrniThrustShape off = orniThrottleThrustShape(1.0f, 0.0f, 0.0f);
    expectNear(off.dwellBoost, 0.0f);
    expectNear(off.centreShift, 0.0f);

    const OrniThrustShape fullSoft = orniThrottleThrustShape(1.0f, 100.0f, 100.0f);
    const OrniThrustShape fullHard = orniThrottleThrustShape(1.0f, 100.0f, -100.0f);
    expectNear(fullSoft.dwellBoost, 8.0f);
    expectNear(fullSoft.centreShift, 100.0f);
    expectNear(fullHard.dwellBoost, 8.0f);
    expectNear(fullHard.centreShift, 100.0f);

    const OrniThrustShape linear = orniThrottleThrustShape(0.5f, 100.0f, 0.0f);
    const OrniThrustShape soft   = orniThrottleThrustShape(0.5f, 100.0f, 100.0f);
    const OrniThrustShape direct = orniThrottleThrustShape(0.5f, 100.0f, -100.0f);
    expectNear(linear.dwellBoost, 4.0f);
    expectNear(linear.centreShift, 50.0f);
    expectNear(soft.dwellBoost, 2.0f);
    expectNear(soft.centreShift, 25.0f);
    expectNear(direct.dwellBoost, 6.0f);
    expectNear(direct.centreShift, 75.0f);
    // Both projections ride the same α — their overlap is never double-counted.
    for (float throttle : {0.0f, 0.3f, 0.6f, 1.0f}) {
        for (float expo : {-100.0f, 0.0f, 100.0f}) {
            const OrniThrustShape s = orniThrottleThrustShape(throttle, 100.0f, expo);
            expectNear(s.dwellBoost / 8.0f, s.centreShift / 100.0f);
        }
    }

    std::cout << "Throttle→thrust expo curve + overlap law passed\n";
    return 0;
}
