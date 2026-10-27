#include "acid.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f

static int rising_crossings(acid_kick_t *k, int n)
{
    float prev = acid_kick_next(k);
    int c = 0;
    for (int i = 1; i < n; i++) {
        float x = acid_kick_next(k);
        c += prev < 0.0f && x >= 0.0f;
        prev = x;
    }
    return c;
}

TEST(test_kick_silent_until_trigger)
{
    acid_kick_t k;
    acid_kick_init(&k, SR);
    float worst = 0.0f;
    for (int i = 0; i < 1000; i++) {
        float a = fabsf(acid_kick_next(&k));
        worst = a > worst ? a : worst;
    }
    CHECK(worst == 0.0f);
}

TEST(test_kick_decays)
{
    acid_kick_t k;
    acid_kick_init(&k, SR);
    acid_kick_trigger(&k);
    float peak = 0.0f;
    double tail_sq = 0.0;
    for (int i = 0; i < 24000; i++) {
        float x = acid_kick_next(&k);
        if (i < 480) {
            peak = fabsf(x) > peak ? fabsf(x) : peak;
        }
        if (i >= 19200) {
            tail_sq += (double)x * x;
        }
    }
    CHECK(peak > 0.5f);
    CHECK(sqrt(tail_sq / 4800) < 0.01);
}

TEST(test_kick_pitch_drops)
{
    acid_kick_t k;
    acid_kick_init(&k, SR);
    acid_kick_trigger(&k);
    int early = rising_crossings(&k, 960);   // 0-20 мс
    for (int i = 0; i < 3840; i++) {
        acid_kick_next(&k);                  // до 100 мс
    }
    int late = rising_crossings(&k, 960);    // 100-120 мс
    CHECK(early > late);
    CHECK(late >= 0);
}

TEST(test_synth_kick_on_beats)
{
    static acid_step_t rests[16];
    for (int i = 0; i < 16; i++) {
        rests[i].note = 0;
        rests[i].flags = ACID_REST;
    }
    static acid_synth_t s;
    acid_synth_init(&s, SR);
    acid_synth_set_pattern(&s, rests, 16);
    float x;
    double on_beat[16] = {0};
    for (int n = 0; n < (int)(16 * s.seq.samples_per_step); n++) {
        acid_synth_render(&s, &x, 1);
        if (s.seq.pos < 480.0f) {           // первые 10 мс шага
            on_beat[s.seq.index] += (double)x * x;
        }
    }
    // На доле удар, между долями только хвост предыдущего.
    for (int i = 0; i < 16; i++) {
        if (i % 4 == 0) {
            CHECK(on_beat[i] > 1.0);
        } else {
            CHECK(on_beat[i] * 5.0 < on_beat[i - i % 4]);
        }
    }
}

int main(void)
{
    RUN(test_kick_silent_until_trigger);
    RUN(test_kick_decays);
    RUN(test_kick_pitch_drops);
    RUN(test_synth_kick_on_beats);
    TEST_MAIN_END();
}
