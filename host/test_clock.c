#include "acid_clock.h"
#include "acid_noise.h"
#include "test.h"

#define SR 48000.0f

// n тиков с шагом step и джиттером +-jit сэмплов, начиная с *t.
static void ticks(acid_clock_t *c, uint32_t *t, int n, uint32_t step, int jit, uint32_t *rnd)
{
    for (int i = 0; i < n; i++) {
        *t += step;
        int j = jit ? (int)(acid_rand(rnd) % (uint32_t)(2 * jit + 1)) - jit : 0;
        acid_clock_tick(c, (uint32_t)((int)*t + j));
    }
}

TEST(test_clock_default_120)
{
    acid_clock_t c;
    acid_clock_init(&c, SR);
    CHECK_NEAR(acid_clock_spb(&c), 24000.0f, 0.5f);
}

TEST(test_clock_tracks_tempo)
{
    acid_clock_t c;
    acid_clock_init(&c, SR);
    uint32_t t = 0, r = 1;
    ticks(&c, &t, 48, 1000, 0, &r); // 1000 сэмплов на тик = 24000 на четверть = 120 BPM
    CHECK_NEAR(acid_clock_spb(&c), 24000.0f, 1.0f);
    ticks(&c, &t, 48, 1200, 0, &r); // 100 BPM
    CHECK_NEAR(acid_clock_spb(&c), 28800.0f, 1.0f);
}

TEST(test_clock_jitter_stable)
{
    acid_clock_t c;
    acid_clock_init(&c, SR);
    uint32_t t = 0, r = 7;
    ticks(&c, &t, 48, 1100, 0, &r);
    float base = acid_clock_spb(&c);
    int stable = 1;
    for (int i = 0; i < 200; i++) {
        ticks(&c, &t, 1, 1100, 240, &r); // джиттер +-5 мс
        stable &= acid_clock_spb(&c) == base;
    }
    CHECK(stable);
    CHECK_NEAR(base, 26400.0f, 264.0f);
}

TEST(test_clock_clamps_range)
{
    acid_clock_t c;
    acid_clock_init(&c, SR);
    uint32_t t = 0, r = 1;
    ticks(&c, &t, 48, 400, 0, &r); // 300 BPM
    CHECK_NEAR(acid_clock_spb(&c), SR * 60.0f / 200.0f, 1.0f);
    ticks(&c, &t, 48, 3000, 0, &r); // 40 BPM
    CHECK_NEAR(acid_clock_spb(&c), SR * 60.0f / 60.0f, 1.0f);
}

TEST(test_clock_keeps_tempo_without_ticks)
{
    acid_clock_t c;
    acid_clock_init(&c, SR);
    uint32_t t = 0, r = 1;
    ticks(&c, &t, 48, 1200, 0, &r);
    t += 5 * 48000; // клок пропал на 5 с
    acid_clock_tick(&c, t);
    CHECK_NEAR(acid_clock_spb(&c), 28800.0f, 1.0f);
    ticks(&c, &t, 3, 1200, 0, &r);
    CHECK_NEAR(acid_clock_spb(&c), 28800.0f, 1.0f);
}

int main(void)
{
    RUN(test_clock_default_120);
    RUN(test_clock_tracks_tempo);
    RUN(test_clock_jitter_stable);
    RUN(test_clock_clamps_range);
    RUN(test_clock_keeps_tempo_without_ticks);
    TEST_MAIN_END();
}
