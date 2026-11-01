#include "acid.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f

static int rising_zero_crossings(float (*gen)(acid_osc_t *), float hz, int n)
{
    acid_osc_t o = {0};
    acid_osc_set_freq(&o, hz, SR);
    float prev = gen(&o);
    int count = 0;
    for (int i = 1; i < n; i++) {
        float x = gen(&o);
        if (prev < 0.0f && x >= 0.0f) {
            count++;
        }
        prev = x;
    }
    return count;
}

TEST(test_midi_to_hz)
{
    CHECK_NEAR(acid_midi_to_hz(69), 440.0f, 0.01f);
    CHECK_NEAR(acid_midi_to_hz(57), 220.0f, 0.01f);
    CHECK_NEAR(acid_midi_to_hz(81), 880.0f, 0.02f);
}

TEST(test_saw_frequency)
{
    CHECK_NEAR(rising_zero_crossings(acid_osc_saw, 110.0f, 48000), 110, 1);
}

TEST(test_square_frequency)
{
    CHECK_NEAR(rising_zero_crossings(acid_osc_square, 110.0f, 48000), 110, 1);
}

TEST(test_osc_range)
{
    float (*gens[2])(acid_osc_t *) = {acid_osc_saw, acid_osc_square};
    for (int g = 0; g < 2; g++) {
        acid_osc_t o = {0};
        acid_osc_set_freq(&o, 2000.0f, SR);
        float lo = 0.0f, hi = 0.0f;
        for (int i = 0; i < 480000; i++) {
            float x = gens[g](&o);
            lo = x < lo ? x : lo;
            hi = x > hi ? x : hi;
        }
        CHECK(lo >= -1.05f);
        CHECK(hi <= 1.05f);
        CHECK(lo < -0.9f); // не тишина
        CHECK(hi > 0.9f);
    }
}

// Прежний PolyBLEP с делением на dt: эталон для быстрого пути.
static float ref_blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static float ref_next(float *ph, float inc, int square)
{
    float p = *ph, y;
    if (square) {
        y = p < 0.5f ? 1.0f : -1.0f;
        y += ref_blep(p, inc);
        float t2 = p + 0.5f;
        t2 -= t2 >= 1.0f ? 1.0f : 0.0f;
        y -= ref_blep(t2, inc);
    } else {
        y = 2.0f * p - 1.0f - ref_blep(p, inc);
    }
    p += inc;
    *ph = p >= 1.0f ? p - 1.0f : p;
    return y;
}

TEST(test_blep_matches_division)
{
    static const float hz[] = {55.0f, 440.0f, 5000.0f};
    float worst = 0.0f;
    for (int sq = 0; sq < 2; sq++) {
        for (int h = 0; h < 3; h++) {
            acid_osc_t o = {0};
            acid_osc_set_freq(&o, hz[h], SR);
            float ph = 0.0f;
            for (int i = 0; i < 48000; i++) {
                float y = sq ? acid_osc_square(&o) : acid_osc_saw(&o);
                float d = fabsf(y - ref_next(&ph, o.inc, sq));
                worst = d > worst ? d : worst;
            }
        }
    }
    CHECK(worst < 1e-5f);
}

TEST(test_osc_idt_is_reciprocal)
{
    acid_osc_t o = {0};
    acid_osc_set_freq(&o, 440.0f, SR);
    CHECK_NEAR(o.idt * o.inc, 1.0f, 1e-6f);
}

int main(void)
{
    RUN(test_midi_to_hz);
    RUN(test_saw_frequency);
    RUN(test_square_frequency);
    RUN(test_osc_range);
    RUN(test_blep_matches_division);
    RUN(test_osc_idt_is_reciprocal);
    TEST_MAIN_END();
}
