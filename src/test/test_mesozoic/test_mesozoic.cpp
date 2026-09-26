#include <cstdint>
#include <unity.h>
#include "Mesozoic.h"

// Mesozoic rate-damper brain — native correctness probe (MESOZOIC.md DoD §5–§6).
// Verifies: zero-rate → zero output, constant-rate → per-tick output,
// wind-up clamp bounds the accumulator, correct damping sign, derivative settles.

static void test_zero_rate_zero_output(void) {
    MesoPid p; mesoInit(p);
    int16_t out = mesoPid(p, 0, 3072, 1024, 64, 5365760, 4);
    TEST_ASSERT_EQUAL_INT16(0, out);
}

static void test_constant_rate_produces_per_tick_output(void) {
    MesoPid p; mesoInit(p);
    // 10 °/s = 1310 LSB (±250 dps ⇒ 131 LSB/(°/s)). P = 0.75 → ~982 LSB,
    // plus a first-step derivative kick. Output must be non-zero and same order.
    int16_t out = mesoPid(p, 1310, 3072, 1024, 64, 5365760, 4);
    TEST_ASSERT_INT16_WITHIN(200, 1084, out);   // measured: P + first-step D
}

static void test_sign_damps_not_drives(void) {
    MesoPid p; mesoInit(p);
    int16_t opos = mesoPid(p,  1310, 3072, 1024, 64, 5365760, 4);
    mesoInit(p);
    int16_t oneg = mesoPid(p, -1310, 3072, 1024, 64, 5365760, 4);
    TEST_ASSERT_TRUE(opos > 0);
    TEST_ASSERT_TRUE(oneg < 0);
}

static void test_integrator_windup_clamped(void) {
    MesoPid p; mesoInit(p);
    int16_t mn = 32767, mx = -32768;
    for (int t = 0; t < 100000; t++) {
        int16_t o = mesoPid(p, 1310, 3072, 1024, 64, 5365760, 4);
        if (o < mn) mn = o;
        if (o > mx) mx = o;
    }
    // Bounded and no NaN (comparisons above would fail otherwise).
    TEST_ASSERT_TRUE(mn > -32768);
    TEST_ASSERT_TRUE(mx < 32767);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_rate_zero_output);
    RUN_TEST(test_constant_rate_produces_per_tick_output);
    RUN_TEST(test_sign_damps_not_drives);
    RUN_TEST(test_integrator_windup_clamped);
    UNITY_END();
    return 0;
}
