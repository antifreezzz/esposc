#include "acid.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f
#define MS(x) ((int)((x) * 48))

static acid_snare_t sn;

static double rms_range(int from, int to, int *pos)
{
    double sq = 0.0;
    for (; *pos < to; (*pos)++) {
        float x = acid_snare_next(&sn);
        if (*pos >= from) {
            sq += (double)x * x;
        }
    }
    return sqrt(sq / (to - from));
}

// Переходы через ноль за первые ms миллисекунд после удара.
static int crossings(int ms)
{
    acid_snare_trigger(&sn);
    int n = 0;
    float prev = 0.0f;
    for (int i = 0; i < MS(ms); i++) {
        float x = acid_snare_next(&sn);
        n += (prev < 0.0f) != (x < 0.0f);
        prev = x;
    }
    return n;
}

// Корпус малого без libm и деления: как прежняя формула (шум отключён snappy = 0) с точностью ниже 16 бит.
TEST(test_snare_body_matches_reference)
{
    acid_snare_t sn;
    acid_snare_init(&sn, 48000.0f);
    sn.snappy = 0.0f;
    acid_snare_trigger(&sn);
    float phase = 0.0f, pitch = 1.0f, body = 1.0f, worst = 0.0f;
    for (int i = 0; i < 24000; i++) {
        float ref = sinf(2.0f * 3.14159265f * phase) * body;
        phase += sn.tune_hz * (1.0f + 0.5f * pitch) / 48000.0f;
        phase -= phase >= 1.0f ? 1.0f : 0.0f;
        pitch *= sn.pitch_coef;
        body *= sn.body_coef;
        float y = acid_snare_next(&sn);
        if (sn.body_env == 0.0f) {
            break;
        }
        worst = fmaxf(worst, fabsf(y - ref));
    }
    CHECK(worst < 2e-5f);
}

TEST(test_snare_silent_until_trigger)
{
    acid_snare_init(&sn, SR);
    int pos = 0;
    CHECK(rms_range(0, 1000, &pos) == 0.0);
}

TEST(test_snare_decays)
{
    acid_snare_init(&sn, SR);
    acid_snare_trigger(&sn);
    int pos = 0;
    double head = rms_range(0, MS(20), &pos);
    double tail = rms_range(MS(400), MS(420), &pos);
    CHECK(head > 0.05);
    CHECK(tail < 0.02 * head);
}

TEST(test_snare_decay_param)
{
    acid_snare_init(&sn, SR);
    acid_snare_set(&sn, 180.0f, 0.6f, 0.05f);
    acid_snare_trigger(&sn);
    int pos = 0;
    double short_tail = rms_range(MS(150), MS(200), &pos);
    acid_snare_init(&sn, SR);
    acid_snare_set(&sn, 180.0f, 0.6f, 0.4f);
    acid_snare_trigger(&sn);
    pos = 0;
    double long_tail = rms_range(MS(150), MS(200), &pos);
    CHECK(long_tail > 5.0 * short_tail);
}

TEST(test_snare_snappy_adds_noise)
{
    acid_snare_init(&sn, SR);
    acid_snare_set(&sn, 180.0f, 0.0f, 0.15f);
    int tonal = crossings(50);
    acid_snare_init(&sn, SR);
    acid_snare_set(&sn, 180.0f, 1.0f, 0.15f);
    int noisy = crossings(50);
    CHECK(tonal < 40);  // корпус ~180 Гц: ~20 переходов за 50 мс
    CHECK(noisy > 200); // пружины: шум
}

TEST(test_snare_tune)
{
    acid_snare_init(&sn, SR);
    acid_snare_set(&sn, 150.0f, 0.0f, 0.15f);
    int low = crossings(50);
    acid_snare_init(&sn, SR);
    acid_snare_set(&sn, 300.0f, 0.0f, 0.15f);
    int high = crossings(50);
    CHECK(high > low * 16 / 10);
}

TEST(test_drum_setters)
{
    // Бочка: длинный decay - больше энергии через 300 мс.
    acid_kick_t k;
    acid_kick_init(&k, SR);
    acid_kick_set(&k, 50.0f, 100.0f, 0.03f, 0.4f);
    acid_kick_trigger(&k);
    for (int i = 0; i < MS(300); i++) {
        acid_kick_next(&k);
    }
    CHECK(k.amp_env > 0.3f);

    // Хэт: длинный закрытый decay.
    acid_hat_t h;
    acid_hat_init(&h, SR);
    acid_hat_set(&h, 7000.0f, 0.08f, 0.2f);
    acid_hat_trigger(&h, 0);
    for (int i = 0; i < MS(80); i++) {
        acid_hat_next(&h);
    }
    CHECK(h.env > 0.3f);

    // Клэп: короткий хвост гаснет быстрее.
    acid_clap_t c;
    acid_clap_init(&c, SR);
    acid_clap_set(&c, 1200.0f, 0.03f);
    acid_clap_trigger(&c);
    for (int i = 0; i < MS(450); i++) { // хвост 30 мс до порога тишины ~350 мс
        acid_clap_next(&c);
    }
    CHECK(c.t == -1);
}

int main(void)
{
    RUN(test_snare_body_matches_reference);
    RUN(test_snare_silent_until_trigger);
    RUN(test_snare_decays);
    RUN(test_snare_decay_param);
    RUN(test_snare_snappy_adds_noise);
    RUN(test_snare_tune);
    RUN(test_drum_setters);
    TEST_MAIN_END();
}
