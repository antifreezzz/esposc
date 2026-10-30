#include "acid_demo.h"
#include "test.h"

#include <string.h>

#define STEP 180 // кусков на шестнадцатую при 125 BPM
#define BAR (16 * STEP)

// Маленькая песня: часть 1 такт с нотами и ручкой, часть 2 такт с пулом моделей.
static const acid_demo_note_t syn1[16] = {
    {45, 100, 1}, {0, 0, 0}, {48, 110, 2}, {50, 90, 1}, // шаг 2 - слайд в шаг 3
};
static const uint32_t drums[16] = {ACID_DEMO_KICK, 0, 0, 0, ACID_DEMO_KICK | ACID_DEMO_SNARE, 0, 0, 0,
                                   ACID_DEMO_CRASH | ACID_DEMO_RIDE | ACID_DEMO_BELL};
static const acid_demo_knob_t knobs1[] = {{2, 3, 0, 127, 0}}; // синт 1 cutoff 0 -> 127 за часть
// Часть 3: вариации ручек и нот синта 1.
static const acid_demo_knob_t knobs3[] = {
    {0, 14, 30, 100, ACID_DEMO_PICK}, // панорама синта 1: круг 0 - 30, дальше случайно 30..100
    {2, 4, 40, 80, 10},               // резонанс 40 -> 80, концы с круга 1 плавают на +-10
};
static const uint8_t pool[] = {11, 3, 21};
static const acid_demo_part_t parts[] = {
    {.bars = 1, .syn1 = syn1, .drums = drums, .knobs = knobs1, .nknobs = 1},
    {.bars = 1, .syn1 = syn1, .syn2_pool = pool, .npool = 3, .first = ACID_DEMO_CRASH},
    {.bars = 1, .syn1 = syn1, .knobs = knobs3, .nknobs = 2, .vary_syn1 = 1},
};
static const acid_demo_song_t song = {parts, 3};
#define LOOP (3 * BAR)

static acid_demo_t d;
static acid_midi_event_t ev[64];

static int chunk(void)
{
    return acid_demo_events(&d, ev, 64);
}

static int find(int n, int type, int ch, int d1)
{
    for (int i = 0; i < n; i++) {
        if (ev[i].type == type && (ch < 0 || ev[i].ch == ch) && (d1 < 0 || ev[i].d1 == d1)) {
            return i;
        }
    }
    return -1;
}

TEST(test_demo_clock_every_960)
{
    acid_demo_init(&d, &song);
    int ok = 1;
    for (int k = 0; k < 400; k++) {
        int n = chunk();
        ok &= (find(n, ACID_MIDI_CLOCK, -1, -1) >= 0) == (k % 30 == 0);
    }
    CHECK(ok);
}

TEST(test_demo_start_at_loop)
{
    acid_demo_init(&d, &song);
    int starts = 0, ok = 1;
    for (int k = 0; k < 2 * LOOP; k++) {
        int n = chunk();
        int s = find(n, ACID_MIDI_START, -1, -1) >= 0;
        starts += s;
        ok &= s == (k % LOOP == 0);
        if (s) {
            ok &= find(n, ACID_MIDI_START, -1, -1) < find(n, ACID_MIDI_CLOCK, -1, -1); // Start до первого тика
        }
    }
    CHECK(ok);
    CHECK(starts == 2);
    CHECK(acid_demo_cycle(&d) == 2);
}

TEST(test_demo_note_on_step)
{
    acid_demo_init(&d, &song);
    int at = -1;
    for (int k = 0; k < BAR; k++) {
        int n = chunk();
        int i = find(n, ACID_MIDI_NOTE_ON, 4, 48);
        if (i >= 0 && at < 0) {
            at = k;
            CHECK(ev[i].d2 == 110);
        }
    }
    CHECK(at == 2 * STEP);
}

TEST(test_demo_cymbal_bits)
{
    acid_demo_init(&d, &song);
    int got = 0;
    for (int k = 0; k <= 8 * STEP; k++) {
        int n = chunk();
        if (k == 8 * STEP) {
            got = find(n, ACID_MIDI_NOTE_ON, 1, 49) >= 0 && find(n, ACID_MIDI_NOTE_ON, 1, 51) >= 0 &&
                  find(n, ACID_MIDI_NOTE_ON, 1, 53) >= 0;
        }
    }
    CHECK(got);
}

// Удары first - только на первом шаге части (крэш на входе), в остальных шагах их нет.
TEST(test_demo_part_first_hit)
{
    acid_demo_init(&d, &song);
    int at_entry = 0, elsewhere = 0;
    for (int k = 0; k < LOOP; k++) {
        int n = chunk();
        int crash = find(n, ACID_MIDI_NOTE_ON, 1, 49) >= 0;
        if (k == BAR) {
            at_entry = crash;
        } else if (k != 8 * STEP) { // в части 1 крэш стоит в шаге 8
            elsewhere += crash;
        }
    }
    CHECK(at_entry);
    CHECK(elsewhere == 0);
}

TEST(test_demo_slide_overlap)
{
    acid_demo_init(&d, &song);
    int on50 = -1, off48 = -1;
    for (int k = 0; k < BAR; k++) {
        int n = chunk();
        if (find(n, ACID_MIDI_NOTE_ON, 4, 50) >= 0 && on50 < 0) {
            on50 = k;
        }
        if (find(n, ACID_MIDI_NOTE_OFF, 4, 48) >= 0 && off48 < 0) {
            off48 = k;
        }
    }
    CHECK(on50 == 3 * STEP);
    CHECK(off48 == 4 * STEP); // 48 держится через on 50: слайд
}

TEST(test_demo_knob_ramp)
{
    acid_demo_init(&d, &song);
    int first = -1, last = -1, ok = 1, count = 0;
    for (int k = 0; k < BAR; k++) {
        int n = chunk();
        for (int i = 0; i < n; i++) {
            if (ev[i].type == ACID_MIDI_CC && ev[i].d1 == 71) { // энкодер 3
                ok &= ev[i].d2 > last;
                first = first < 0 ? ev[i].d2 : first;
                last = ev[i].d2;
                count++;
            }
        }
    }
    CHECK(first == 0);
    CHECK(last == 127);
    CHECK(count == 128);
    CHECK(ok);
}

TEST(test_demo_page_before_enc)
{
    acid_demo_init(&d, &song);
    int n = chunk();
    int pg = -1, enc = find(n, ACID_MIDI_CC, -1, 71);
    for (int i = 0; i < n; i++) {
        if (ev[i].type == ACID_MIDI_CC && ev[i].d1 == 10 && ev[i].d2 == 2) {
            pg = i;
        }
    }
    CHECK(pg >= 0 && enc > pg);
    CHECK(ev[enc].ch == 1); // ручки идут в канал ударки, как с BSP
}

TEST(test_demo_loop_seam)
{
    acid_demo_init(&d, &song);
    int held[128] = {0};
    for (int k = 0; k < LOOP; k++) {
        int n = chunk();
        for (int i = 0; i < n; i++) {
            if (ev[i].ch == 4) {
                held[ev[i].d1] += ev[i].type == ACID_MIDI_NOTE_ON ? 1 : (ev[i].type == ACID_MIDI_NOTE_OFF ? -1 : 0);
            }
        }
    }
    int stuck = 0;
    for (int i = 0; i < 128; i++) {
        stuck += held[i] != 0;
    }
    CHECK(stuck == 0);
}

// Модель синта 2 в части 2 круга c.
static int model_of_cycle(int c)
{
    int model = -1;
    for (int k = 0; k < LOOP; k++) {
        int n = chunk();
        if (k < BAR) {
            continue;
        }
        for (int i = 0; i + 1 < n; i++) {
            if (ev[i].type == ACID_MIDI_CC && ev[i].d1 == 10 && ev[i].d2 == 7 && ev[i + 1].d1 == 74) { // страница синта 2
                model = ev[i + 1].d2;
            }
        }
    }
    (void)c;
    return model;
}

TEST(test_demo_vary_bounds)
{
    acid_demo_init(&d, &song);
    CHECK(model_of_cycle(0) == 11);
    int seen[3] = {0}, ok = 1;
    for (int c = 1; c <= 20; c++) {
        int m = model_of_cycle(c);
        ok &= m == 11 || m == 3 || m == 21;
        seen[0] |= m == 11;
        seen[1] |= m == 3;
        seen[2] |= m == 21;
    }
    CHECK(ok);
    CHECK(seen[0] + seen[1] + seen[2] >= 2); // вариации есть
}

TEST(test_demo_deterministic)
{
    static acid_demo_t a, b;
    static acid_midi_event_t ea[64], eb[64];
    acid_demo_init(&a, &song);
    acid_demo_init(&b, &song);
    int same = 1;
    for (int k = 0; k < 4 * LOOP; k++) {
        int na = acid_demo_events(&a, ea, 64), nb = acid_demo_events(&b, eb, 64);
        same &= na == nb && memcmp(ea, eb, (size_t)na * sizeof ea[0]) == 0;
    }
    CHECK(same);
}

// События части 3 круга (плеер уже в начале круга): первое значение каждой ручки и ноты синта 1.
static void part3(int *pan, int *res_first, int *res_last, acid_demo_note_t *notes, int *nn)
{
    *pan = *res_first = *res_last = -1;
    *nn = 0;
    for (int k = 0; k < LOOP; k++) {
        int n = chunk();
        if (k < 2 * BAR) {
            continue;
        }
        for (int i = 0; i < n; i++) {
            if (ev[i].type == ACID_MIDI_CC && ev[i].d1 == 91 && *pan < 0) {
                *pan = ev[i].d2;
            }
            if (ev[i].type == ACID_MIDI_CC && ev[i].d1 == 76) {
                *res_first = *res_first < 0 ? ev[i].d2 : *res_first;
                *res_last = ev[i].d2;
            }
            if (ev[i].type == ACID_MIDI_NOTE_ON && ev[i].ch == 4 && k < 2 * BAR + BAR && *nn < 16) {
                notes[(*nn)++] = (acid_demo_note_t){ev[i].d1, ev[i].d2, (uint8_t)((k - 2 * BAR) / STEP)};
            }
        }
    }
}

TEST(test_demo_vary_knobs)
{
    acid_demo_init(&d, &song);
    int pan, rf, rl, nn;
    acid_demo_note_t notes[16];
    part3(&pan, &rf, &rl, notes, &nn);
    CHECK(pan == 30 && rf == 40 && rl == 80); // круг 0 канонический
    int ok = 1, pan_diff = 0, res_diff = 0;
    for (int c = 1; c <= 20; c++) {
        part3(&pan, &rf, &rl, notes, &nn);
        ok &= pan >= 30 && pan <= 100 && rf >= 30 && rf <= 50 && rl >= 70 && rl <= 90;
        pan_diff |= pan != 30;
        res_diff |= rf != 40 || rl != 80;
    }
    CHECK(ok);
    CHECK(pan_diff && res_diff);
}

TEST(test_demo_vary_syn1)
{
    acid_demo_init(&d, &song);
    int pan, rf, rl, n0, n1;
    acid_demo_note_t a[16], b[16];
    part3(&pan, &rf, &rl, a, &n0);
    int ok = 1, diff = 0;
    for (int c = 1; c <= 10; c++) {
        part3(&pan, &rf, &rl, b, &n1);
        ok &= n1 == n0;
        for (int i = 0; i < n0 && i < n1; i++) {
            ok &= b[i].note == a[i].note && b[i].len == a[i].len; // ноты и шаги те же
            diff |= b[i].vel != a[i].vel;
        }
    }
    CHECK(n0 == 3);
    CHECK(ok);
    CHECK(diff); // акценты меняются
}

int main(void)
{
    RUN(test_demo_clock_every_960);
    RUN(test_demo_start_at_loop);
    RUN(test_demo_note_on_step);
    RUN(test_demo_cymbal_bits);
    RUN(test_demo_part_first_hit);
    RUN(test_demo_slide_overlap);
    RUN(test_demo_knob_ramp);
    RUN(test_demo_page_before_enc);
    RUN(test_demo_loop_seam);
    RUN(test_demo_vary_bounds);
    RUN(test_demo_deterministic);
    RUN(test_demo_vary_knobs);
    RUN(test_demo_vary_syn1);
    TEST_MAIN_END();
}
