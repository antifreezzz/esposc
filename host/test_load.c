// Загрузка ядер для панели: проценты бюджета, зажим, кадр.
#include "pl_load.h"
#include "pl_proto.h"
#include "test.h"

#include <string.h>

TEST(test_load_percent)
{
    pl_load_acc_t a = {.c1_sum_us = 3000, .c1_worst_us = 1800, .blocks = 2,
                       .c0_sum_us = 1000, .c0_worst_us = 600, .chunks = 3, .underruns = 2};
    pl_load_t o;
    pl_load_pack(&o, &a, 1);
    CHECK(o.c1_mean == 75);
    CHECK(o.c1_worst == 90);
    CHECK(o.c0_mean == 50);
    CHECK(o.c0_worst == 90);
    CHECK(o.underruns == 2);
    CHECK(o.guard == 1);
}

TEST(test_load_clamps)
{
    pl_load_acc_t a = {.c1_sum_us = 10000, .c1_worst_us = 10000, .blocks = 1,
                       .c0_sum_us = 10000, .c0_worst_us = 10000, .chunks = 1, .underruns = 300};
    pl_load_t o;
    pl_load_pack(&o, &a, 2);
    CHECK(o.c1_mean == 255);
    CHECK(o.c1_worst == 255);
    CHECK(o.c0_worst == 255);
    CHECK(o.underruns == 255);
}

TEST(test_load_empty_period)
{
    pl_load_acc_t a = {0};
    pl_load_t o;
    memset(&o, 0x55, sizeof o);
    pl_load_pack(&o, &a, 0);
    CHECK(o.c1_mean == 0 && o.c1_worst == 0 && o.c0_mean == 0 && o.c0_worst == 0 && o.underruns == 0);
}

TEST(test_load_frame_roundtrip)
{
    pl_load_t l = {75, 90, 50, 90, 3, 1};
    uint8_t f[PL_FRAME_MAX];
    size_t n = pl_encode(PL_LOAD, &l, sizeof l, f);
    pl_rx_t rx;
    pl_rx_init(&rx);
    uint8_t type = 0;
    const uint8_t *p = NULL;
    size_t len = 0;
    int got = 0;
    for (size_t i = 0; i < n; i++) {
        got |= pl_rx_feed(&rx, f[i], &type, &p, &len);
    }
    CHECK(got);
    CHECK(type == PL_LOAD);
    CHECK(len == sizeof l);
    CHECK(memcmp(p, &l, sizeof l) == 0);
}

int main(void)
{
    RUN(test_load_percent);
    RUN(test_load_clamps);
    RUN(test_load_empty_period);
    RUN(test_load_frame_roundtrip);
    TEST_MAIN_END();
}
