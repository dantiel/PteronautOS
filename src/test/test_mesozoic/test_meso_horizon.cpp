#include <cstdint>
#include <cmath>
#include <unity.h>
#include "Mesozoic.h"
#include "MesozoicAttitude.h"

// Unity runner stubs (the native hand-built harness needs them).
void setUp(void)    {}
void tearDown(void) {}

// Mesozoic HORIZON T3 — native probe for the attitude outer loop closing the
// cascade. Verifies (1) the rate-setpoint mapping + slew clamp, (2) the inner
// rate-error sign convention (setpoint − rate) against the fixed-point mesoPid,
// (3) closed-loop level hold against an unstable pitch pole, and (4) that a
// bare rate damper DIVERGES on the same pole — the structural proof that the
// attitude term is not a tuning nicety but a requirement.

static void test_rate_setpoint_mapping(void) {
    // 10° error at Kp=2.5 → 25°/s; slew-clamped to ±45°/s.
    TEST_ASSERT_FLOAT_WITHIN(0.001f,  25.0f, mesoAttitudeRateSetpoint( 10.0f, 2.5f, 45.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -25.0f, mesoAttitudeRateSetpoint(-10.0f, 2.5f, 45.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f,  45.0f, mesoAttitudeRateSetpoint( 100.0f, 2.5f, 45.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -45.0f, mesoAttitudeRateSetpoint(-100.0f, 2.5f, 45.0f));
}

static void test_horizon_blend(void) {
    // 10° attitude error, Kp=2.5 → level term 25°/s. Stick centre = pure level
    // hold; full deflection = pure rate command; mid-stick = the blend.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, mesoHorizonRateSetpoint(10.0f,  0.0f, 2.5f, 45.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 45.0f, mesoHorizonRateSetpoint(10.0f,  1.0f, 2.5f, 45.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f,-45.0f, mesoHorizonRateSetpoint(10.0f, -1.0f, 2.5f, 45.0f));
    // mid-stick: 25·(1−0.5) + 0.5·45 = 12.5 + 22.5 = 35.0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 35.0f, mesoHorizonRateSetpoint(10.0f,  0.5f, 2.5f, 45.0f));
}

static void test_inner_rate_error_sign(void) {
    // Positive attitude error (nose-down body) demands a positive rate setpoint
    // → positive mesoPid correction (nose-up moment), in °/s proportional to
    // the rate error. kp=1.0 Q12, so output LSB == error LSB == 25°/s.
    const float lsb = 131.0f;          // MPU6050 ±250 dps
    const int16_t kp = 4096;           // 1.0 in Q12
    int16_t err = (int16_t)(25.0f * lsb);   // 3275 LSB
    MesoPid p; mesoInit(p);
    int16_t c = mesoPid(p, err, kp, 0, 0, 0, 4);
    TEST_ASSERT_TRUE(c > 0);
    TEST_ASSERT_FLOAT_WITHIN(6.0f, 25.0f, (float)c / lsb);
}

// Closed-loop pitch sim on an unstable pitch pole (rateDot = a·pitch + control).
// outerLoop=true closes the cascade (rateSp = −Kp_att·pitch); false leaves a
// bare rate damper (rateSp = 0). Returns the final pitch after the run.
static float simPitch(bool outerLoop) {
    const float dt = 0.004f;
    const float a = 0.8f;        // destabilizing pitch moment (per s per deg)
    const float m = 3.0f;        // inner rate gain
    const float kpAtt = 2.5f;    // outer attitude gain
    const float maxRate = 45.0f;
    float pitch = outerLoop ? 15.0f : 1.0f;   // initial perturbation
    float rate = 0.0f;
    for (int t = 0; t < 20000; ++t) {
        float rateSp = outerLoop ? mesoAttitudeRateSetpoint(0.0f - pitch, kpAtt, maxRate) : 0.0f;
        float rateDot = a * pitch + m * (rateSp - rate);   // unstable moment + control
        rate  += rateDot * dt;
        pitch += rate * dt;
    }
    return pitch;
}

static void test_cascade_holds_level(void) {
    // The cascade must settle and hold near level (bounded, small) on the
    // unstable pole — the whole point of HORIZON.
    TEST_ASSERT_FLOAT_WITHIN(3.0f, 0.0f, simPitch(true));
}

static void test_rate_damper_alone_diverges(void) {
    // Without the outer attitude loop the unstable pole wins and pitch runs
    // away — proof the attitude term is structurally required, not optional.
    TEST_ASSERT_TRUE(fabsf(simPitch(false)) > 100.0f);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_rate_setpoint_mapping);
    RUN_TEST(test_horizon_blend);
    RUN_TEST(test_inner_rate_error_sign);
    RUN_TEST(test_cascade_holds_level);
    RUN_TEST(test_rate_damper_alone_diverges);
    UNITY_END();
    return 0;
}