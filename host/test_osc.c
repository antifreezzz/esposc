#include "acid.h"
#include "test.h"

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

int main(void)
{
    RUN(test_midi_to_hz);
    RUN(test_saw_frequency);
    RUN(test_square_frequency);
    RUN(test_osc_range);
    TEST_MAIN_END();
}
