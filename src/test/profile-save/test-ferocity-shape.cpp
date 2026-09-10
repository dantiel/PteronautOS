#include <cassert>
#include <cmath>
#include <iostream>

#include "../../lib/Ornithopter/OrnithopterWaveform.h"

static constexpr float kPi = 3.14159265358979f;

static void expectNear(float actual, float expected, float tolerance = 0.0001f)
{
    assert(std::fabs(actual - expected) < tolerance);
}

int main()
{
    // Zero ferocity remains the original smooth cosine for every shape mix.
    for (float mix : {0.0f, 25.0f, 50.0f, 100.0f}) {
        for (int step = 0; step <= 32; ++step) {
            const float phase = 2.0f * kPi * step / 32.0f;
            expectNear(FlappingOscillator::shapeWave(phase, 0.0f, 0.0f, -1.0f, mix),
                       std::cos(phase));
        }
    }

    // At maximum ferocity, legacy mode is a finite-transition plateau while
    // pyramidal mode travels continuously through the half-stroke.
    const float quarterStroke = 0.25f * kPi;
    const float plateau = FlappingOscillator::shapeWave(quarterStroke, 8.0f, 8.0f, -1.0f, 0.0f);
    const float pyramidal = FlappingOscillator::shapeWave(quarterStroke, 8.0f, 8.0f, -1.0f, 100.0f);
    assert(plateau > 0.99f);
    assert(pyramidal > 0.35f && pyramidal < 0.65f);

    // The control is a true continuous mix between both shape families.
    const float mixed = FlappingOscillator::shapeWave(quarterStroke, 8.0f, 8.0f, -1.0f, 50.0f);
    expectNear(mixed, 0.5f * (plateau + pyramidal));

    // Uneven ferocity anticipates the stronger downstroke: 7/1 ends the
    // downstroke after 1/8 cycle, while 1/7 leaves it in the elongated smooth
    // downstroke at the same phase.
    const float shortHalfMid = 0.125f * kPi;
    const float anticipated = FlappingOscillator::shapeWave(shortHalfMid, 7.0f, 1.0f, -1.0f, 100.0f);
    const float elongated = FlappingOscillator::shapeWave(shortHalfMid, 1.0f, 7.0f, -1.0f, 100.0f);
    assert(std::fabs(anticipated) < 0.05f);
    assert(elongated > 0.9f);

    // Both families join continuously at the shared reversal boundary.
    for (float mix : {0.0f, 50.0f, 100.0f}) {
        const float before = FlappingOscillator::shapeWave(kPi - 0.00001f, 8.0f, 8.0f, kPi, mix);
        const float after = FlappingOscillator::shapeWave(kPi + 0.00001f, 8.0f, 8.0f, kPi, mix);
        assert(before < -0.99f);
        assert(after < -0.99f);
        assert(std::fabs(before - after) < 0.001f);
    }

    // Centre-skew: +100 front-loads the downstroke so the wing passes its
    // mid-stroke (wave zero-crossing) BEFORE the symmetric t=0.5; −100 delays
    // it past t=0.5. At zero ferocity the half is a pure cosine, whose centre
    // is exactly the sign change.
    const float downStart = 0.0f;   // theta=0 → downstroke start
    const float downMid  = 0.5f * kPi;  // theta=π/2 → symmetric centre (cos=0)
    // Symmetric: value at downMid is ~0 (cosine zero-crossing).
    expectNear(FlappingOscillator::shapeWave(downMid, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f), 0.0f, 0.02f);
    // +100 skew (front-load): at the same phase the wave has already passed
    // the centre and is negative (descending), i.e. centre arrived early.
    const float frontLoaded = FlappingOscillator::shapeWave(downMid, 0.0f, 0.0f, -1.0f, 0.0f, 100.0f, 0.0f);
    assert(frontLoaded < -0.05f);
    // −100 skew (late thrust): centre arrives later, wave still positive.
    const float lateThrust = FlappingOscillator::shapeWave(downMid, 0.0f, 0.0f, -1.0f, 0.0f, -100.0f, 0.0f);
    assert(lateThrust > 0.05f);
    // Endpoints stay pinned under full skew — no position jump at reversal.
    expectNear(FlappingOscillator::shapeWave(0.0f, 4.0f, 4.0f, -1.0f, 0.0f, 100.0f, -100.0f), 1.0f, 0.001f);
    // returnSkew acts on the upstroke (mirrored half). At mid-upstroke
    // (t=0.5) the symmetric value is 0; +100 front-load has already crossed to
    // positive (advanced), −100 lags behind at negative.
    const float upMid   = kPi + 0.5f * kPi;   // theta = 3π/2, upstroke centre
    const float upFront = FlappingOscillator::shapeWave(upMid, 0.0f, 0.0f, kPi, 0.0f, 0.0f, 100.0f);
    const float upBack  = FlappingOscillator::shapeWave(upMid, 0.0f, 0.0f, kPi, 0.0f, 0.0f, -100.0f);
    assert(upFront > 0.5f);
    assert(upBack < -0.5f);

    std::cout << "Ferocity plateau-to-pyramidal mixing + centre-skew passed\n";
    return 0;
}