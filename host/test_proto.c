// Кадры UART синт <-> панель: сборка, разбор, мусор и обрывы на линии.
#include "pl_proto.h"
#include "test.h"

#include <string.h>

static pl_rx_t rx;
static uint8_t got_type;
static const uint8_t *got_pl;
static size_t got_len;
static int frames;
static uint8_t copy[PL_PAYLOAD_MAX]; // нагрузка живёт до следующего вызова: храним копию

static void keep(void)
{
    frames++;
    memcpy(copy, got_pl, got_len);
    got_pl = copy;
}

// Скормить байты, посчитать собранные кадры; последний - в got_*.
static void feed(const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (pl_rx_feed(&rx, b[i], &got_type, &got_pl, &got_len)) {
            keep();
            while (pl_rx_next(&rx, &got_type, &got_pl, &got_len)) {
                keep();
            }
        }
    }
}

static void start(void)
{
    pl_rx_init(&rx);
    frames = 0;
}

TEST(test_roundtrip)
{
    static pl_snapshot_t snap;
    memset(&snap, 0, sizeof snap);
    snap.mode = 1;
    snap.bsp_page = 6;
    snap.raw[9][15] = 127;
    snap.bsp_pos[3] = 87;
    snap.held[19] = 0x80;
    pl_knob_t k = {.page = 6, .enc = 2, .value = 30, .src = 2, .bsp_pos = 87, .held = 1};
    pl_set_t st = {.page = 8, .enc = 5, .value = 100};
    struct {
        uint8_t type;
        const void *p;
        size_t n;
    } cases[] = {{PL_SNAPSHOT, &snap, sizeof snap}, {PL_KNOB, &k, sizeof k}, {PL_SET, &st, sizeof st}, {PL_HELLO, NULL, 0}};
    CHECK(sizeof snap <= 255);
    for (size_t c = 0; c < 4; c++) {
        uint8_t buf[PL_FRAME_MAX];
        size_t n = pl_encode(cases[c].type, cases[c].p, cases[c].n, buf);
        CHECK(n == cases[c].n + 4);
        CHECK(buf[0] == PL_SYNC);
        start();
        feed(buf, n);
        CHECK(frames == 1);
        CHECK(got_type == cases[c].type);
        CHECK(got_len == cases[c].n);
        CHECK(cases[c].n == 0 || memcmp(got_pl, cases[c].p, cases[c].n) == 0);
    }
}

TEST(test_bad_crc_dropped)
{
    pl_set_t st = {1, 2, 3};
    uint8_t buf[PL_FRAME_MAX];
    size_t n = pl_encode(PL_SET, &st, sizeof st, buf);
    buf[n - 1] ^= 0x01;
    start();
    feed(buf, n);
    CHECK(frames == 0);
    buf[n - 1] ^= 0x01;
    buf[4] ^= 0x40; // битый байт полезной нагрузки
    start();
    feed(buf, n);
    CHECK(frames == 0);
}

TEST(test_garbage_then_frame)
{
    pl_set_t st = {6, 3, 77};
    uint8_t frame[PL_FRAME_MAX];
    size_t n = pl_encode(PL_SET, &st, sizeof st, frame);
    // Ложный синхробайт с большой длиной: «кадр» глотает настоящий, приёмник должен его найти.
    uint8_t junk[] = {0x00, 0xFF, PL_SYNC, PL_KNOB, 40, 0x13, 0x37};
    start();
    feed(junk, sizeof junk);
    feed(frame, n);
    uint8_t tail[40];
    memset(tail, 0x11, sizeof tail);
    feed(tail, sizeof tail);
    CHECK(frames == 1);
    CHECK(got_type == PL_SET && got_len == 3 && got_pl[2] == 77);
}

TEST(test_truncated_frame)
{
    pl_knob_t k = {6, 2, 30, 0, 87, 0};
    pl_set_t st = {8, 5, 100};
    uint8_t a[PL_FRAME_MAX], b[PL_FRAME_MAX];
    size_t na = pl_encode(PL_KNOB, &k, sizeof k, a);
    size_t nb = pl_encode(PL_SET, &st, sizeof st, b);
    start();
    feed(a, na - 3); // оборвался: плата перезагрузилась
    feed(b, nb);
    CHECK(frames == 1);
    CHECK(got_type == PL_SET && got_pl[2] == 100);
}

TEST(test_back_to_back)
{
    pl_set_t s1 = {1, 2, 3}, s2 = {4, 5, 6};
    uint8_t buf[2 * PL_FRAME_MAX];
    size_t n = pl_encode(PL_SET, &s1, sizeof s1, buf);
    n += pl_encode(PL_SET, &s2, sizeof s2, buf + n);
    start();
    feed(buf, n);
    CHECK(frames == 2);
    CHECK(got_pl[0] == 4);
}

TEST(test_swallowed_two_frames)
{
    // Ложный старт длиной в два настоящих кадра: оба должны выйти сразу, без ожидания следующего байта.
    pl_set_t s1 = {1, 2, 3}, s2 = {4, 5, 6};
    uint8_t buf[3 + 2 * PL_FRAME_MAX] = {PL_SYNC, PL_SET, 13}; // 3 + 13 + crc = ровно два кадра по 7
    size_t n = 3;
    n += pl_encode(PL_SET, &s1, sizeof s1, buf + n);
    n += pl_encode(PL_SET, &s2, sizeof s2, buf + n);
    start();
    feed(buf, n);
    CHECK(frames == 2);
    CHECK(got_pl[0] == 4);
}

TEST(test_bad_header_resyncs_now)
{
    // Мусор от отключённой панели: синхробайт с чужой длиной или неизвестным типом не должен ждать «кадр».
    pl_set_t st = {6, 3, 77};
    uint8_t frame[PL_FRAME_MAX];
    size_t n = pl_encode(PL_SET, &st, sizeof st, frame);
    uint8_t junk[][3] = {{PL_SYNC, PL_SNAPSHOT, 200}, {PL_SYNC, 0x37, 250}, {PL_SYNC, PL_HELLO, 9}, {PL_SYNC, PL_SET, 255}};
    for (size_t j = 0; j < 4; j++) {
        start();
        feed(junk[j], 3);
        feed(frame, n); // кадр сразу за мусором, больше байт не будет
        CHECK(frames == 1);
        CHECK(got_type == PL_SET && got_pl[2] == 77);
    }
}

int main(void)
{
    RUN(test_roundtrip);
    RUN(test_bad_crc_dropped);
    RUN(test_garbage_then_frame);
    RUN(test_truncated_frame);
    RUN(test_back_to_back);
    RUN(test_swallowed_two_frames);
    RUN(test_bad_header_resyncs_now);
    TEST_MAIN_END();
}
