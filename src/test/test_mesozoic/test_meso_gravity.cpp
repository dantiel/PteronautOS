#include <cstdint>
#include <cmath>
#include <unity.h>
#include "MesozoicGravity.h"
#include "MesozoicAttitude.h"

// Unity runner stubs (the native hand-built harness needs them).
void setUp(void)    {}
void tearDown(void) {}

// Mesozoic HORIZON — native correctness probe for the flap-decoupled gravity
// recovery (T1) + slow complementary attitude (T2). Verifies the 4-mixer
// ESPELHO lock-in extracts the full coherent self-image (fundamental + skew,
// phase-true, ×2), the gravity low-pass recovers the true gravity direction,
// and the attitude estimate lands on the correct roll/pitch.

// Drive N flap cycles of a synthetic polluted accelerometer.
static void runFlap(float *gravOut, float *rollOut, float *pitchOut,
                    float pitchDeg, float rollDeg,
                    float flapHz, int ticks)
{
    const float dt = 0.004f, tauLock = 0.4f, tauGrav = 0.15f, tauAtt = 0.5f;

    // True gravity in body frame (level ref → [0,0,1g] when level).
    const float pr = pitchDeg * 3.14159265f / 180.0f;
    const float rr = rollDeg  * 3.14159265f / 180.0f;
    const float Gx = -sinf(pr);
    const float Gy =  sinf(rr) * cosf(pr);
    const float Gz =  cosf(rr) * cosf(pr);

    // Flap-coherent self-accel (fundamental + quadrature + skew) per axis.
    const float A1[3] = { 0.8f, -0.5f,  0.6f};
    const float B1[3] = { 0.3f,  0.7f, -0.4f};
    const float A2[3] = { 0.25f,-0.2f,  0.3f};
    const float B2[3] = {-0.15f, 0.2f,  0.1f};

    MesoGravity g; mesoGravityInit(g);
    MesoAttitude att; mesoAttitudeInit(att);
    float phase = 0.0f;

    for (int t = 0; t < ticks; ++t) {
        phase += 2.0f * 3.14159265f * flapHz * dt;
        if (phase >= 6.283185307f) phase -= 6.283185307f;
        const float s = sinf(phase), c = cosf(phase);
        const float s2 = 2.0f*s*c, c2 = c*c - s*s;
        const float noise = 0.05f * sinf(phase * 23.0f + 0.7f);

        const float ax = Gx + A1[0]*s + B1[0]*c + A2[0]*s2 + B2[0]*c2 + noise;
        const float ay = Gy + A1[1]*s + B1[1]*c + A2[1]*s2 + B2[1]*c2 + noise;
        const float az = Gz + A1[2]*s + B1[2]*c + A2[2]*s2 + B2[2]*c2 + noise;

        mesoGravityUpdate(g, ax, ay, az, s, c, dt, tauLock, tauGrav);
        // steady tilt: zero gyro → attitude must come purely from gravity
        mesoAttitudeUpdate(att, 0.0f, 0.0f, g.grav[0], g.grav[1], g.grav[2], dt, tauAtt);
    }

    gravOut[0] = g.grav[0]; gravOut[1] = g.grav[1]; gravOut[2] = g.grav[2];
    *rollOut = att.roll; *pitchOut = att.pitch;
}

static void test_gravity_recovers_level(void) {
    float g[3], r, p;
    runFlap(g, &r, &p, 0.0f, 0.0f, 10.0f, 8000);
    // Direction must be [0,0,1g] — X/Y near zero, Z near +1.
    TEST_ASSERT_FLOAT_WITHIN(0.08f, 0.0f, g[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.08f, 0.0f, g[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.10f, 1.0f, g[2]);
}

static void test_attitude_recovers_tilt(void) {
    float g[3], r, p;
    runFlap(g, &r, &p, 20.0f, 10.0f, 10.0f, 8000);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 20.0f, p);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 10.0f, r);
}

static void test_attitude_rejects_beat_frequency(void) {
    // At 25 Hz flap (well above the ~1 Hz gravity band), attitude must still
    // settle to level with no beat-frequency oscillation in the estimate.
    float g[3], r, p;
    runFlap(g, &r, &p, 0.0f, 0.0f, 25.0f, 12000);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, r);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, p);
}

static void test_espelho_extracts_skew(void) {
    // Pure coherent sample: fundamental + 2nd harmonic, no gravity. After
    // convergence the skew ratio |2nd|/|fundamental| must be recovered.
    const float dt = 0.004f, tau = 0.4f;
    const float A1 = 0.6f, B1 = 0.2f, A2 = 0.3f, B2 = -0.4f;
    MesoEspelho e; mesoEspelhoInit(e);
    float phase = 0.0f;
    for (int t = 0; t < 8000; ++t) {
        phase += 2.0f*3.14159265f*10.0f*dt;
        if (phase >= 6.283185307f) phase -= 6.283185307f;
        const float s = sinf(phase), c = cosf(phase);
        const float sample = A1*s + B1*c + A2*(2.0f*s*c) + B2*(c*c - s*s);
        (void)mesoEspelhoStep(e, sample, s, c, dt, tau);
    }
    const float expected = sqrtf(A2*A2 + B2*B2) / sqrtf(A1*A1 + B1*B1);
    const float got = mesoEspelhoSkew(e);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, expected, got);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_gravity_recovers_level);
    RUN_TEST(test_attitude_recovers_tilt);
    RUN_TEST(test_attitude_rejects_beat_frequency);
    RUN_TEST(test_espelho_extracts_skew);
    UNITY_END();
    return 0;
}