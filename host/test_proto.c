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
    snap.raw[PL_PAGE_PETRI][4] = 6;
    pl_knob_t k = {.page = 6, .enc = 2, .value = 30, .src = 2, .bsp_pos = 87, .held = 1};
    pl_set_t st = {.page = 8, .enc = 5, .value = 100};
    uint8_t page = 3;
    struct {
        uint8_t type;
        const void *p;
        size_t n;
    } cases[] = {{PL_SNAPSHOT, &snap, sizeof snap}, {PL_KNOB, &k, sizeof k}, {PL_SET, &st, sizeof st}, {PL_HELLO, NULL, 0},
                 {PL_PAGE, &page, 1}};
    CHECK(sizeof snap <= 255);
    for (size_t c = 0; c < 5; c++) {
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

TEST(test_new_frame_sizes)
{
    CHECK(sizeof(pl_custom_t) == 198);
    CHECK(sizeof(pl_custom_edit_t) == 6);
    CHECK(sizeof(pl_scene_t) == 3);
    CHECK(sizeof(pl_scene_state_t) == 3);
    static pl_custom_t c;
    memset(&c, PL_NONE, sizeof c);
    c.up = 4;
    c.val[14] = 99;
    uint8_t f[PL_FRAME_MAX];
    start();
    feed(f, pl_encode(PL_CUSTOM, &c, sizeof c, f));
    CHECK(frames == 1 && got_type == PL_CUSTOM && got_len == sizeof c);
    CHECK(got_pl[0] == 4 && memcmp(got_pl, &c, sizeof c) == 0);
    pl_custom_edit_t e = {1, 2, 0, 6 << 4 | 2, 0, 127};
    pl_scene_t sc = {1, 7, 4};
    pl_scene_state_t st = {0x81, 7, 2};
    start();
    feed(f, pl_encode(PL_CUSTOM_EDIT, &e, sizeof e, f));
    feed(f, pl_encode(PL_SCENE, &sc, sizeof sc, f));
    feed(f, pl_encode(PL_SCENE_STATE, &st, sizeof st, f));
    CHECK(frames == 3 && got_type == PL_SCENE_STATE && got_pl[0] == 0x81);
}

// Панель просит следующее демо: кадр без нагрузки проходит разбор и рядом с другими.
TEST(test_demo_frame)
{
    uint8_t f[PL_FRAME_MAX];
    pl_scene_t sc = {0, 1, 0};
    start();
    feed(f, pl_encode(PL_DEMO, NULL, 0, f));
    CHECK(frames == 1 && got_type == PL_DEMO && got_len == 0);
    feed(f, pl_encode(PL_SCENE, &sc, sizeof sc, f));
    CHECK(frames == 2 && got_type == PL_SCENE);
}

// Прямой выбор режима: 1 байт; чужая длина - не кадр. Без BSP годятся все режимы, с BSP - только LIVE и DRONE.
TEST(test_mode_frame)
{
    uint8_t f[PL_FRAME_MAX];
    uint8_t mode = PL_DEMO_SEQ;
    start();
    feed(f, pl_encode(PL_MODE, &mode, 1, f));
    CHECK(frames == 1 && got_type == PL_MODE && got_len == 1 && got_pl[0] == PL_DEMO_SEQ);
    start();
    feed(f, pl_encode(PL_MODE, NULL, 0, f));
    CHECK(frames == 0);
    CHECK(PL_MODE_LIVE == 0);
    for (int m = PL_DEMO_METAL; m <= PL_DEMO_SEQ; m++) {
        CHECK(pl_mode_ok(0, m));
    }
    CHECK(pl_mode_ok(1, PL_MODE_LIVE) && pl_mode_ok(1, PL_DEMO_DRONE));
    CHECK(!pl_mode_ok(1, PL_DEMO_ACID) && !pl_mode_ok(1, PL_DEMO_SEQ));
    CHECK(!pl_mode_ok(0, PL_DEMO_SEQ + 1) && !pl_mode_ok(0, -1) && !pl_mode_ok(1, 200));
}

TEST(test_snapshot_fits)
{
    CHECK(PL_PAGES == 14 && PL_PAGE_GRIDS == 12 && PL_PAGE_PETRI == 13 && PL_USER_FIRST == 14);
    CHECK(sizeof(pl_snapshot_t) <= PL_PAYLOAD_MAX);
}

TEST(test_page_bsp)
{
    CHECK(pl_page_bsp(0) == 0 && pl_page_bsp(9) == 9);
    CHECK(pl_page_bsp(PL_PAGE_LAYER1) == 16 && pl_page_bsp(PL_PAGE_LAYER1 + 1) == 17);
    CHECK(pl_page_bsp(PL_PAGE_GRIDS) == -1);
    CHECK(pl_page_bsp(PL_USER_FIRST) == 10 && pl_page_bsp(PL_USER_FIRST + 5) == 15);
    CHECK(pl_page_bsp(PL_PAGE_MIX_DRUMS) == -1 && pl_page_bsp(PL_PAGE_MIX_SYN1 + 1) == -1);
}

// Шаг Grids: 1 байт; заголовок с другой длиной - не кадр.
TEST(test_grids_step_frame)
{
    uint8_t f[PL_FRAME_MAX];
    uint8_t step = 17;
    start();
    feed(f, pl_encode(PL_GRIDS_STEP, &step, 1, f));
    CHECK(frames == 1 && got_type == PL_GRIDS_STEP && got_len == 1 && got_pl[0] == 17);
    uint8_t two[2] = {1, 2};
    start();
    feed(f, pl_encode(PL_GRIDS_STEP, two, 2, f));
    CHECK(frames == 0);
}

TEST(test_seq_frames)
{
    static const struct {
        uint8_t type;
        size_t len;
    } T[] = {{PL_SEQ_STEP, 4}, {PL_SEQ_OCT, 3}, {PL_SEQ_ROW, 11}, {PL_SEQ_CTRL, 3}, {PL_SEQ_BPM, 1},
             {PL_SEQ_SCALE, 3}, {PL_SEQ_DUMP, sizeof(pl_seq_dump_t)}, {PL_SEQ_POS, sizeof(pl_seq_pos_t)}};
    CHECK(sizeof(pl_seq_dump_t) == 220); // + лады тактов
    CHECK(sizeof(pl_seq_pos_t) == 8);
    static uint8_t pl[PL_PAYLOAD_MAX], f[PL_FRAME_MAX];
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        start();
        feed(f, pl_encode(T[i].type, pl, T[i].len, f));
        CHECK(frames == 1 && got_type == T[i].type && got_len == T[i].len);
        start();
        feed(f, pl_encode(T[i].type, pl, T[i].len + 1, f));
        CHECK(frames == 0);
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
    RUN(test_new_frame_sizes);
    RUN(test_demo_frame);
    RUN(test_mode_frame);
    RUN(test_snapshot_fits);
    RUN(test_page_bsp);
    RUN(test_grids_step_frame);
    RUN(test_seq_frames);
    TEST_MAIN_END();
}
