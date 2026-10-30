#include "acid.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f
#define MS(x) ((int)((x) * 48))

static double rms_hat(acid_hat_t *h, int from, int to, int *pos)
{
    double sq = 0.0;
    for (; *pos < to; (*pos)++) {
        float x = acid_hat_next(h);
        if (*pos >= from) {
            sq += (double)x * x;
        }
    }
    return sqrt(sq / (to - from));
}

TEST(test_noise_range_and_mean)
{
    uint32_t st = 1;
    double sum = 0.0;
    float lo = 0.0f, hi = 0.0f;
    for (int i = 0; i < 100000; i++) {
        float x = acid_noise(&st);
        sum += x;
        lo = x < lo ? x : lo;
        hi = x > hi ? x : hi;
    }
    CHECK(lo >= -1.0f && hi <= 1.0f);
    CHECK(lo < -0.99f && hi > 0.99f);
    CHECK(fabs(sum / 100000) < 0.02);
}

TEST(test_hat_silent_until_trigger)
{
    acid_hat_t h;
    acid_hat_init(&h, SR);
    int pos = 0;
    CHECK(rms_hat(&h, 0, 1000, &pos) == 0.0);
}

TEST(test_hat_closed_short_open_long)
{
    acid_hat_t h;
    acid_hat_init(&h, SR);
    acid_hat_trigger(&h, 0);
    int pos = 0;
    double head = rms_hat(&h, 0, MS(20), &pos);
    double tail = rms_hat(&h, MS(60), MS(80), &pos);
    CHECK(head > 0.05);
    CHECK(tail < 0.1 * head);

    acid_hat_init(&h, SR);
    acid_hat_trigger(&h, 1);
    pos = 0;
    head = rms_hat(&h, 0, MS(20), &pos);
    tail = rms_hat(&h, MS(60), MS(80), &pos);
    CHECK(tail > 0.3 * head);
}

TEST(test_hat_is_bright)
{
    acid_hat_t h;
    acid_hat_init(&h, SR);
    acid_hat_trigger(&h, 1);
    float prev = acid_hat_next(&h);
    int crossings = 0;
    for (int i = 1; i < MS(10); i++) {
        float x = acid_hat_next(&h);
        crossings += prev < 0.0f && x >= 0.0f;
        prev = x;
    }
    CHECK(crossings > 30);
}

TEST(test_clap_three_bursts_and_tail)
{
    acid_clap_t c;
    acid_clap_init(&c, SR);
    float buf[MS(500)];
    for (int i = 0; i < MS(500); i++) {
        buf[i] = acid_clap_next(&c);
    }
    double silent = 0.0;
    for (int i = 0; i < MS(500); i++) {
        silent += fabsf(buf[i]);
    }
    CHECK(silent == 0.0);

    acid_clap_trigger(&c);
    for (int i = 0; i < MS(500); i++) {
        buf[i] = acid_clap_next(&c);
    }
    for (int burst = 0; burst < 2; burst++) {
        double on = 0.0, gap = 0.0;
        for (int i = MS(burst * 10); i < MS(burst * 10 + 3); i++) {
            on += (double)buf[i] * buf[i];
        }
        for (int i = MS(burst * 10 + 7); i < MS(burst * 10 + 10); i++) {
            gap += (double)buf[i] * buf[i];
        }
        CHECK(on > 3.0 * gap);
    }
    double third = 0.0, tail = 0.0;
    for (int i = MS(20); i < MS(23); i++) {
        third += (double)buf[i] * buf[i];
    }
    CHECK(third > 0.0);
    for (int i = MS(400); i < MS(500); i++) {
        tail += (double)buf[i] * buf[i];
    }
    CHECK(sqrt(tail / MS(100)) < 0.01);
}

// Хэт один голос: закрытый глушит открытый, открытый после закрытого звенит как обычный открытый.
static double hat_energy(int first_open, int second_open, int gap)
{
    acid_hat_t h;
    acid_hat_init(&h, SR);
    acid_hat_trigger(&h, first_open);
    int pos = 0;
    if (gap >= 0) {
        rms_hat(&h, 0, gap, &pos);
        acid_hat_trigger(&h, second_open);
        pos = 0;
    }
    return rms_hat(&h, MS(100), MS(200), &pos);
}

TEST(test_hat_choke_both_ways)
{
    double open_alone = hat_energy(1, 0, -1);
    CHECK(open_alone > 1e-4);
    CHECK(hat_energy(1, 0, MS(50)) < 0.05 * open_alone); // закрытый глушит открытый
    CHECK_NEAR(hat_energy(0, 1, MS(5)), open_alone, 0.05 * open_alone); // открытый после закрытого
}

int main(void)
{
    RUN(test_hat_choke_both_ways);
    RUN(test_noise_range_and_mean);
    RUN(test_hat_silent_until_trigger);
    RUN(test_hat_closed_short_open_long);
    RUN(test_hat_is_bright);
    RUN(test_clap_three_bursts_and_tail);
    TEST_MAIN_END();
}
