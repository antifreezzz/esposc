// Связка синта с панелью: подхват энкодера BSP после правки с панели, глушение ручек демо, события.
#include "acid.h"
#include "acid_custom.h"
#include "acid_link.h"
#include "test.h"

#define SR 48000.0f

static acid_synth_t s;
static acid_link_t l;
static acid_link_ev_t ev;
static int nev;

static void reset(void)
{
    acid_synth_init(&s, SR);
    acid_link_init(&l);
    acid_link_use_custom(&l, NULL);
}

static int midi(int src, int type, int d1, int d2)
{
    acid_midi_event_t e = {.type = (uint8_t)type, .ch = 0, .d1 = (uint8_t)d1, .d2 = (uint8_t)d2};
    nev = acid_link_midi(&l, &s, &e, src, &ev);
    return nev;
}

// enc - номер энкодера BSP 1..16
static int bsp(int enc, int v) { return midi(ACID_SRC_BSP, ACID_MIDI_CC, acid_bsp_enc_cc[enc - 1], v); }
static int demo(int enc, int v) { return midi(ACID_SRC_DEMO, ACID_MIDI_CC, acid_bsp_enc_cc[enc - 1], v); }
static int panel(int page, int enc, int v) { return nev = acid_link_panel_set(&l, &s, page, enc - 1, v, &ev); }
static int knob(int page, int enc) { return acid_synth_knob(&s, page, enc - 1); }

TEST(test_pickup_blocks_until_cross)
{
    reset();
    bsp(3, 87);
    CHECK(knob(6, 3) == 87);
    CHECK(panel(6, 3, 30) == 1);
    CHECK(ev.src == ACID_SRC_PANEL && ev.value == 30 && ev.held == 1 && ev.bsp_pos == 87);
    int seq[] = {86, 60, 31};
    for (int i = 0; i < 3; i++) {
        CHECK(bsp(3, seq[i]) == 1);
        CHECK(knob(6, 3) == 30);
        CHECK(ev.held == 1 && ev.bsp_pos == seq[i] && ev.value == 30);
    }
    bsp(3, 29);
    CHECK(knob(6, 3) == 29);
    CHECK(ev.held == 0 && ev.value == 29 && ev.src == ACID_SRC_BSP);
    bsp(3, 40);
    CHECK(knob(6, 3) == 40);
}

TEST(test_pickup_skip_over)
{
    reset();
    bsp(3, 40);
    panel(6, 3, 30);
    bsp(3, 24); // быстрый поворот: 30 пропущено
    CHECK(knob(6, 3) == 24);
    CHECK(ev.held == 0);
}

TEST(test_pickup_from_below)
{
    reset();
    bsp(3, 10);
    panel(6, 3, 30);
    bsp(3, 14);
    bsp(3, 22);
    CHECK(knob(6, 3) == 30);
    bsp(3, 33);
    CHECK(knob(6, 3) == 33);
}

TEST(test_pickup_per_page)
{
    reset();
    bsp(3, 50);
    panel(6, 3, 30);
    bsp(1, 7);
    CHECK(ev.enc == 0 && ev.value == 7);
    bsp(3, 100);
    CHECK(knob(7, 3) == 100);
    CHECK(knob(6, 3) == 30);
}

TEST(test_pickup_unknown_bsp_pos)
{
    reset();
    panel(6, 3, 30);
    bsp(3, 90); // позиция BSP неизвестна: только запоминаем
    CHECK(knob(6, 3) == 30);
    CHECK(ev.held == 1 && ev.bsp_pos == 90);
    bsp(3, 20);
    CHECK(knob(6, 3) == 20);
    CHECK(ev.held == 0);
}

TEST(test_demo_muted_after_panel)
{
    reset();
    demo(1, 0);
    demo(2, 99);
    CHECK(knob(0, 2) == 99);
    CHECK(ev.src == ACID_SRC_DEMO && ev.held == 0);
    panel(0, 3, 10);
    CHECK(l.demo_muted);
    CHECK(demo(2, 20) == 0);
    CHECK(knob(0, 2) == 99);
    // ноты демо идут дальше
    midi(ACID_SRC_DEMO, ACID_MIDI_NOTE_ON, 36, 120);
    CHECK(s.slot[0].nheld == 0); // канал 0 не синт; проверяем канал 5 ниже
    acid_midi_event_t on = {.type = ACID_MIDI_NOTE_ON, .ch = 4, .d1 = 48, .d2 = 100};
    acid_link_midi(&l, &s, &on, ACID_SRC_DEMO, &ev);
    CHECK(s.slot[0].nheld == 1);
    acid_link_init(&l);
    CHECK(!l.demo_muted);
    demo(2, 20);
    CHECK(knob(0, 2) == 20);
}

TEST(test_type_change_sends_snapshot)
{
    reset();
    bsp(1, 6);
    bsp(2, 11);
    CHECK(l.snapshot_due);
    l.snapshot_due = 0;
    bsp(3, 50);
    CHECK(!l.snapshot_due);
    panel(7, 2, 0);
    CHECK(l.snapshot_due);
    l.snapshot_due = 0;
    panel(0, 2, 5); // энкодер 2 микса - не тип
    CHECK(!l.snapshot_due);
}

TEST(test_page_event)
{
    reset();
    CHECK(bsp(1, 3) == 1);
    CHECK(ev.enc == 0 && ev.value == 3 && ev.src == ACID_SRC_BSP);
    CHECK(s.page == 3);
    CHECK(bsp(1, 20) == 0); // не страница: синт игнорирует, панели сообщать нечего
    CHECK(s.page == 3);
    CHECK(panel(0, 1, 5) == 0); // энкодер 1 с панели не трогаем
    CHECK(s.page == 3);
}

TEST(test_panel_page)
{
    reset();
    bsp(1, 6);
    CHECK(acid_link_panel_page(&l, &s, 8, &ev) == 1);
    CHECK(s.page == 8);
    CHECK(ev.enc == 0 && ev.value == 8 && ev.src == ACID_SRC_PANEL);
    bsp(3, 90); // энкодеры BSP крутят страницу с экрана
    CHECK(knob(8, 3) == 90);
    bsp(1, 2); // энкодер 1 - снова страница с BSP
    CHECK(s.page == 2);
    CHECK(acid_link_panel_page(&l, &s, 10, &ev) == 0); // не страница
    CHECK(s.page == 2);
}

static acid_custom_t c;

static void reset_custom(void)
{
    reset();
    acid_custom_init(&c);
    acid_link_use_custom(&l, &c);
}

static int take(void) { return acid_link_take(&l, &ev); }

TEST(test_user_page_macro_from_bsp)
{
    reset_custom();
    acid_custom_edit(&c, 0, 2, 0, 6, 2, 0, 127);
    acid_custom_edit(&c, 0, 2, 1, 0, 1, 127, 0);
    CHECK(bsp(1, 10) == 1);
    CHECK(ev.page == 10 && ev.enc == 0 && ev.value == 10);
    CHECK(bsp(3, 127) == 1);
    CHECK(knob(6, 3) == 127 && knob(0, 2) == 0);
    CHECK(ev.page == 10 && ev.enc == 2 && ev.value == 127);
    CHECK(take() == 1 && ev.page == 6 && ev.enc == 2 && ev.value == 127);
    CHECK(take() == 1 && ev.page == 0 && ev.enc == 1 && ev.value == 0);
    CHECK(take() == 0);
    CHECK(c.val[0][2] == 127 && c.save == 1);
}

TEST(test_user_page_alias_from_bsp)
{
    reset_custom();
    acid_custom_edit(&c, 0, 2, 0, 6, 2, 0, 127);
    bsp(1, 10);
    bsp(3, 40);
    CHECK(knob(6, 3) == 40);
    CHECK(ev.page == 10 && ev.value == 40);
}

TEST(test_user_page_empty_slot)
{
    reset_custom();
    static acid_synth_t before;
    before = s;
    bsp(1, 15);
    CHECK(bsp(5, 99) == 1);
    CHECK(c.val[5][4] == 99);
    CHECK(take() == 0);
    for (int p = 0; p < ACID_PAGES; p++) {
        for (int e = 1; e < ACID_KNOBS; e++) {
            CHECK(acid_synth_knob(&s, p, e) == acid_synth_knob(&before, p, e));
        }
    }
}

TEST(test_user_page_no_custom)
{
    reset();
    CHECK(bsp(1, 12) == 0);
    bsp(3, 50);
    CHECK(s.page == 6 && knob(6, 3) == 50);
    CHECK(acid_link_page(&l, &s) == 6);
}

TEST(test_back_to_standard_page)
{
    reset_custom();
    bsp(1, 10);
    bsp(1, 6);
    CHECK(l.user == -1);
    bsp(3, 33);
    CHECK(knob(6, 3) == 33);
    CHECK(ev.page == 6);
}

TEST(test_user_page_pickup)
{
    reset_custom();
    acid_custom_edit(&c, 0, 2, 0, 6, 2, 0, 127);
    bsp(1, 10);
    bsp(3, 90);
    CHECK(panel(10, 3, 30) == 1);
    CHECK(ev.page == 10 && ev.held == 1 && knob(6, 3) == 30);
    while (take()) {
    }
    bsp(3, 80);
    CHECK(knob(6, 3) == 30 && ev.held == 1 && ev.page == 10);
    bsp(3, 29);
    CHECK(knob(6, 3) == 29 && ev.held == 0);
}

TEST(test_user_panel_set_macro)
{
    reset_custom();
    acid_custom_edit(&c, 1, 4, 0, 8, 3, 0, 100);
    CHECK(panel(11, 5, 127) == 1);
    CHECK(knob(8, 4) == 100 && c.val[1][4] == 127 && l.held[11][4] == 1);
    CHECK(take() == 1 && ev.page == 8 && ev.enc == 3 && ev.value == 100);
    CHECK(panel(16, 5, 1) == 0);
}

TEST(test_user_panel_page)
{
    reset_custom();
    CHECK(acid_link_panel_page(&l, &s, 13, &ev) == 1);
    CHECK(l.user == 3 && ev.value == 13 && acid_link_page(&l, &s) == 13);
    CHECK(acid_link_panel_page(&l, &s, 2, &ev) == 1);
    CHECK(l.user == -1 && s.page == 2);
    CHECK(acid_link_panel_page(&l, &s, 16, &ev) == 0);
}

TEST(test_link_init_keeps_custom)
{
    reset_custom();
    bsp(1, 10);
    acid_link_init(&l);
    CHECK(l.custom == &c && l.user == -1);
}

TEST(test_page_reports_user)
{
    reset_custom();
    bsp(1, 11);
    CHECK(acid_link_page(&l, &s) == 11);
}

TEST(test_custom_edit_marks_due)
{
    reset_custom();
    CHECK(acid_link_custom_edit(&l, 2, 3, 0, 0, 1, 0, 127) == 1);
    CHECK(l.custom_due == (1 << 2));
    CHECK(acid_link_custom_edit(&l, 2, 3, 0, 6, 1, 0, 127) == 0);
}

int main(void)
{
    RUN(test_pickup_blocks_until_cross);
    RUN(test_pickup_skip_over);
    RUN(test_pickup_from_below);
    RUN(test_pickup_per_page);
    RUN(test_pickup_unknown_bsp_pos);
    RUN(test_demo_muted_after_panel);
    RUN(test_type_change_sends_snapshot);
    RUN(test_page_event);
    RUN(test_panel_page);
    RUN(test_user_page_macro_from_bsp);
    RUN(test_user_page_alias_from_bsp);
    RUN(test_user_page_empty_slot);
    RUN(test_user_page_no_custom);
    RUN(test_back_to_standard_page);
    RUN(test_user_page_pickup);
    RUN(test_user_panel_set_macro);
    RUN(test_user_panel_page);
    RUN(test_link_init_keeps_custom);
    RUN(test_page_reports_user);
    RUN(test_custom_edit_marks_due);
    TEST_MAIN_END();
}
