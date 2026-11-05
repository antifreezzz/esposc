// Таблица чашки панель -> синт: 4 кадра pl_proto по 64 точки, собирается только целиком одного номера.
#include "pl_proto.h"
#include "test.h"

#include <string.h>

static int16_t T[256];
static int8_t M[6] = {1, -2, 3, -4, 5, -6};
static uint8_t out[4][PL_PETRI_PART_MAX];
static size_t len[4];
static pl_petri_rx_t r;

static void fill(int base)
{
    for (int i = 0; i < 256; i++) {
        T[i] = (int16_t)(base + i * 100 - 12800);
    }
}

// Части через настоящий кодек кадров: как по линии.
static int feed(int part)
{
    uint8_t f[PL_FRAME_MAX];
    size_t n = pl_encode((uint8_t)(PL_PETRI_0 + part), out[part], len[part], f);
    static pl_rx_t rx;
    pl_rx_init(&rx);
    uint8_t type;
    const uint8_t *p;
    size_t pl;
    int got = 0;
    for (size_t i = 0; i < n; i++) {
        if (pl_rx_feed(&rx, f[i], &type, &p, &pl)) {
            got = pl_petri_collect(&r, type, p, pl);
        }
    }
    return got;
}

TEST(split_collect_roundtrip)
{
    fill(0);
    pl_petri_split(T, M, 7, 3, out, len);
    CHECK(len[0] == 129 && len[1] == 129 && len[2] == 129 && len[3] == 137);
    memset(&r, 0, sizeof r);
    CHECK(feed(0) == 0 && feed(1) == 0 && feed(2) == 0);
    CHECK(feed(3) == 1);
    CHECK(memcmp(r.tab, T, sizeof T) == 0);
    CHECK(memcmp(r.mods, M, sizeof M) == 0);
    CHECK(r.jumps == 7 && r.seq == 3);
}

TEST(mixed_seq_rejected)
{
    memset(&r, 0, sizeof r);
    fill(0);
    pl_petri_split(T, M, 1, 10, out, len);
    feed(0);
    feed(1);
    fill(5);
    pl_petri_split(T, M, 1, 11, out, len); // следующий номер: прежние части не в счёт
    CHECK(feed(2) == 0);
    CHECK(feed(3) == 0);
    CHECK(feed(0) == 0);
    CHECK(feed(1) == 1); // все 4 части номера 11, порядок не важен
    CHECK(r.seq == 11 && r.tab[0] == T[0]);
}

TEST(lost_part_waits)
{
    memset(&r, 0, sizeof r);
    fill(0);
    pl_petri_split(T, M, 1, 20, out, len);
    feed(0);
    feed(1);
    CHECK(feed(3) == 0); // без части 2
    pl_petri_split(T, M, 1, 21, out, len);
    CHECK(feed(0) + feed(1) + feed(2) + feed(3) == 1);
}

TEST(frame_lengths_fixed)
{
    static pl_rx_t rx;
    pl_rx_init(&rx);
    uint8_t junk[] = {PL_SYNC, PL_PETRI_1, 128}; // чужая длина
    uint8_t type;
    const uint8_t *p;
    size_t pl;
    int got = 0;
    for (size_t i = 0; i < sizeof junk; i++) {
        got += pl_rx_feed(&rx, junk[i], &type, &p, &pl);
    }
    CHECK(got == 0);
    CHECK(rx.n == 0); // заголовок отброшен сразу
}

int main(void)
{
    RUN(split_collect_roundtrip);
    RUN(mixed_seq_rejected);
    RUN(lost_part_waits);
    RUN(frame_lengths_fixed);
    TEST_MAIN_END();
}
