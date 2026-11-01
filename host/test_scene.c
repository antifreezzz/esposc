// Сцены: снимок, вызов сразу и переходом по тактам, лимит ручек на кусок, blob.
#include <stdlib.h>
#include <string.h>

#include "acid.h"
#include "acid_link.h"
#include "acid_scene.h"
#include "test.h"

static acid_synth_t s;
static acid_link_t l;
static acid_custom_t c;
static acid_scene_t sc;

static void reset(void)
{
    acid_synth_init(&s, 48000.0f);
    acid_link_init(&l);
    acid_custom_init(&c);
    acid_link_use_custom(&l, &c);
    acid_scene_init(&sc);
}

static void midi(int type)
{
    acid_midi_event_t e = {.type = (uint8_t)type};
    acid_synth_midi(&s, &e);
}

static int step(void)
{
    return acid_scene_step(&sc, &l, &s);
}

// Тик клока и шаг сцены после него (кусков между тиками в тесте по одному).
static void tick(int n)
{
    for (int i = 0; i < n; i++) {
        midi(ACID_MIDI_CLOCK);
        step();
    }
}

static void run_to_end(void)
{
    for (int i = 0; i < 10000 && sc.state != 0; i++) {
        step();
    }
}

static int knob(int page, int enc) { return acid_synth_knob(&s, page, enc); }
static void set(int page, int enc, int v) { acid_synth_set_knob(&s, page, enc, v); }

TEST(test_store_recall_instant_no_clock)
{
    reset();
    set(6, 2, 20);
    acid_scene_store(&sc, 0, &s, &c);
    CHECK(sc.used == 1 && sc.save == 1);
    set(6, 2, 90);
    CHECK(acid_scene_recall(&sc, 0, 4) == 1);
    run_to_end();
    CHECK(knob(6, 2) == 20);
    CHECK(sc.state == 0 && sc.cur == 0);
    CHECK(l.held[6][2] == 1); // как правка с панели: BSP подхватывает
}

TEST(test_per_chunk_limit)
{
    reset();
    acid_scene_store(&sc, 1, &s, &c);
    for (int p = 0; p <= 2; p++) {
        for (int e = 1; e < ACID_KNOBS; e++) {
            set(p, e, knob(p, e) == 3 ? 4 : 3);
        }
    }
    acid_scene_recall(&sc, 1, 0);
    static acid_synth_t before;
    before = s;
    step();
    int changed = 0;
    for (int p = 0; p < ACID_PAGES; p++) {
        for (int e = 1; e < ACID_KNOBS; e++) {
            changed += acid_synth_knob(&s, p, e) != acid_synth_knob(&before, p, e);
        }
    }
    CHECK(changed > 0 && changed <= ACID_SCENE_PER_CHUNK);
    run_to_end();
    for (int e = 1; e < ACID_KNOBS; e++) {
        CHECK(knob(0, e) == sc.knob[1][0][e]);
    }
}

TEST(test_waits_for_bar)
{
    reset();
    set(0, 1, 30);
    acid_scene_store(&sc, 0, &s, &c);
    set(0, 1, 80);
    midi(ACID_MIDI_START);
    tick(10);
    acid_scene_recall(&sc, 0, 0);
    int at = -1;
    for (int t = 11; t <= 120 && at < 0; t++) {
        tick(1);
        if (knob(0, 1) == 30) {
            at = t;
        }
    }
    CHECK(at == 97); // первый тик после Start - доля 1, оборот такта на 97-м
}

TEST(test_morph_linear)
{
    reset();
    set(0, 1, 96);
    acid_scene_store(&sc, 0, &s, &c);
    set(0, 1, 0);
    midi(ACID_MIDI_START);
    tick(1);
    acid_scene_recall(&sc, 0, 1);
    tick(95); // до оборота такта
    CHECK(knob(0, 1) == 0);
    tick(1 + 48);
    CHECK_NEAR(knob(0, 1), 48, 2);
    tick(60);
    CHECK(knob(0, 1) == 96);
    CHECK(sc.state == 0);
}

TEST(test_list_jumps_at_start)
{
    reset();
    set(8, 6, 4); // дилэй 3/8
    acid_scene_store(&sc, 0, &s, &c);
    set(8, 6, 0);
    midi(ACID_MIDI_START);
    tick(1);
    acid_scene_recall(&sc, 0, 4);
    tick(96);
    CHECK(knob(8, 6) == 4);
}

TEST(test_type_first)
{
    reset();
    set(7, 1, 21); // синт 2 - modal
    set(7, 3, 99);
    acid_scene_store(&sc, 0, &s, &c);
    set(7, 3, 5); // память типа 21 теперь 5
    set(7, 1, 11);
    acid_scene_recall(&sc, 0, 0);
    run_to_end();
    CHECK(s.slot[1].sel_type == 21);
    CHECK(knob(7, 3) == 99);
}

TEST(test_recall_during_morph_starts_from_current)
{
    reset();
    set(0, 1, 0);
    acid_scene_store(&sc, 1, &s, &c);
    set(0, 1, 100);
    acid_scene_store(&sc, 0, &s, &c);
    set(0, 1, 0);
    midi(ACID_MIDI_START);
    tick(1);
    acid_scene_recall(&sc, 0, 2);
    tick(96 + 96); // половина перехода на 2 такта
    int mid = knob(0, 1);
    CHECK_NEAR(mid, 50, 3);
    acid_scene_recall(&sc, 1, 2);
    tick(95); // ждём черту такта: первый переход стоит
    CHECK(knob(0, 1) == mid);
    tick(1); // черта: второй переход от текущего значения
    CHECK(abs(knob(0, 1) - mid) <= 2);
}

TEST(test_empty_slot_ignored)
{
    reset();
    CHECK(acid_scene_recall(&sc, 3, 0) == 0);
    CHECK(acid_scene_recall(&sc, 8, 0) == 0);
    CHECK(sc.state == 0);
}

TEST(test_snap_rate)
{
    reset();
    set(0, 1, 127);
    acid_scene_store(&sc, 0, &s, &c);
    set(0, 1, 0);
    midi(ACID_MIDI_START);
    tick(1);
    acid_scene_recall(&sc, 0, 2);
    tick(95);
    int at[64], n = 0, steps = 0, r = 0;
    while (sc.state != 0 && steps < 1000) {
        midi(ACID_MIDI_CLOCK);
        steps++;
        r = step();
        if (r && n < 64) {
            at[n++] = steps;
        }
    }
    CHECK(sc.state == 0);
    CHECK(r == 1); // в конце снимок
    CHECK(n >= 2);
    for (int i = 1; i < n - 1; i++) {
        CHECK(at[i] - at[i - 1] >= ACID_SCENE_SNAP_EVERY); // промежуточные не чаще раза в 150 кусков
    }
    CHECK(at[0] >= ACID_SCENE_SNAP_EVERY);
}

TEST(test_user_values_restored)
{
    reset();
    acid_custom_edit(&c, 0, 2, 0, 0, 1, 0, 50); // макро на мастер
    c.val[0][2] = 77;
    acid_scene_store(&sc, 0, &s, &c);
    c.val[0][2] = 10;
    int master = knob(0, 1);
    acid_scene_recall(&sc, 0, 0);
    run_to_end();
    CHECK(c.val[0][2] == 77);
    CHECK(knob(0, 1) == master); // макро цели не двигает: мастер уже из сцены
    CHECK(l.custom_due == 0x3F);
}

TEST(test_blob_roundtrip_and_reject)
{
    static uint8_t blob[ACID_SCENE_BLOB];
    reset();
    set(0, 1, 33);
    acid_scene_store(&sc, 5, &s, &c);
    CHECK(acid_scene_export(&sc, blob, sizeof blob) == ACID_SCENE_BLOB);
    CHECK(acid_scene_export(&sc, blob, sizeof blob - 1) == 0);
    static acid_scene_t d;
    acid_scene_init(&d);
    CHECK(acid_scene_import(&d, blob, sizeof blob) == 1);
    CHECK(d.used == (1 << 5) && d.knob[5][0][1] == 33);
    acid_scene_init(&d);
    CHECK(acid_scene_import(&d, blob, sizeof blob - 1) == 0);
    blob[1] ^= 1;
    CHECK(acid_scene_import(&d, blob, sizeof blob) == 0);
    blob[1] ^= 1;
    blob[sizeof blob - 1] = 200; // значение вне хода
    CHECK(acid_scene_import(&d, blob, sizeof blob) == 0);
    CHECK(d.used == 0);
}

int main(void)
{
    RUN(test_store_recall_instant_no_clock);
    RUN(test_per_chunk_limit);
    RUN(test_waits_for_bar);
    RUN(test_morph_linear);
    RUN(test_list_jumps_at_start);
    RUN(test_type_first);
    RUN(test_recall_during_morph_starts_from_current);
    RUN(test_empty_slot_ignored);
    RUN(test_snap_rate);
    RUN(test_user_values_restored);
    RUN(test_blob_roundtrip_and_reject);
    TEST_MAIN_END();
}
