// Связка синта с панелью: подхват энкодера BSP после правки с панели, глушение ручек демо, события.
#include "acid.h"
#include "acid_custom.h"
#include "acid_link.h"
#include "pl_proto.h"
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
    acid_link_use_mod(&l, NULL);
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
    CHECK(acid_link_panel_page(&l, &s, ACID_USER_FIRST, &ev) == 0); // своя без определений - не страница
    CHECK(s.page == 2);
}

TEST(test_reverb_panel_only)
{
    reset();
    bsp(1, 8);
    CHECK(panel(ACID_PAGE_REVERB, 2, 33) == 1);
    CHECK(knob(ACID_PAGE_REVERB, 2) == 33);
    CHECK(ev.page == ACID_PAGE_REVERB && ev.enc == 1 && ev.value == 33 && ev.src == ACID_SRC_PANEL && ev.held == 1);
    CHECK(s.page == 8 && l.held[ACID_PAGE_REVERB][1] == 1);
    CHECK(panel(ACID_PAGE_REVERB, 8, 99) == 0);
    CHECK(acid_link_panel_page(&l, &s, ACID_PAGE_REVERB, &ev) == 0);
    CHECK(s.page == 8 && acid_link_page(&l, &s) == 8);
}

static acid_custom_t c;

static void reset_custom(void)
{
    reset();
    acid_custom_init(&c);
    acid_link_use_custom(&l, &c);
}

static int take(void) { return acid_link_take(&l, &ev); }
static int step(void) { return nev = acid_link_step(&l, &s, &ev); }

TEST(test_user_page_macro_from_bsp)
{
    reset_custom();
    acid_custom_edit(&c, 0, 2, 0, 6, 2, 0, 127);
    acid_custom_edit(&c, 0, 2, 1, 0, 1, 127, 0);
    CHECK(bsp(1, 10) == 1);
    CHECK(ev.page == ACID_USER_FIRST && ev.enc == 0 && ev.value == ACID_USER_FIRST);
    int master = knob(0, 2);
    CHECK(bsp(3, 127) == 1);
    CHECK(ev.page == ACID_USER_FIRST && ev.enc == 2 && ev.value == 127);
    CHECK(take() == 0);
    CHECK(knob(0, 2) == master); // цели макро - в следующем шаге связки, не в этом событии
    CHECK(step() == 1 && ev.page == 6 && ev.enc == 2 && ev.value == 127);
    CHECK(take() == 1 && ev.page == 0 && ev.enc == 1 && ev.value == 0);
    CHECK(take() == 0);
    CHECK(knob(6, 3) == 127 && knob(0, 2) == 0);
    CHECK(c.val[0][2] == 127 && c.save == 1);
}

TEST(test_user_page_alias_from_bsp)
{
    reset_custom();
    acid_custom_edit(&c, 0, 2, 0, 6, 2, 0, 127);
    bsp(1, 10);
    bsp(3, 40);
    CHECK(knob(6, 3) == 40);
    CHECK(ev.page == ACID_USER_FIRST && ev.value == 40);
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
    CHECK(panel(ACID_USER_FIRST, 3, 30) == 1);
    CHECK(ev.page == ACID_USER_FIRST && ev.held == 1 && knob(6, 3) == 30);
    while (take()) {
    }
    bsp(3, 80);
    CHECK(knob(6, 3) == 30 && ev.held == 1 && ev.page == ACID_USER_FIRST);
    bsp(3, 29);
    CHECK(knob(6, 3) == 29 && ev.held == 0);
}

TEST(test_user_panel_set_macro)
{
    reset_custom();
    acid_custom_edit(&c, 1, 4, 0, 8, 3, 0, 100);
    CHECK(panel(ACID_USER_FIRST + 1, 5, 127) == 1);
    CHECK(c.val[1][4] == 127 && l.held[ACID_USER_FIRST + 1][4] == 1);
    CHECK(step() == 1 && ev.page == 8 && ev.enc == 3 && ev.value == 100);
    CHECK(knob(8, 4) == 100);
    CHECK(panel(ACID_USER_FIRST + 6, 5, 1) == 0);
}

TEST(test_user_panel_page)
{
    reset_custom();
    CHECK(acid_link_panel_page(&l, &s, ACID_USER_FIRST + 3, &ev) == 1);
    CHECK(l.user == 3 && ev.value == ACID_USER_FIRST + 3 && acid_link_page(&l, &s) == ACID_USER_FIRST + 3);
    CHECK(acid_link_panel_page(&l, &s, 2, &ev) == 1);
    CHECK(l.user == -1 && s.page == 2);
    CHECK(acid_link_panel_page(&l, &s, ACID_USER_FIRST + 6, &ev) == 1); // следом за своими - вкладки MIX
    CHECK(l.user == -1 && l.mix == ACID_USER_FIRST + 6);
    CHECK(acid_link_panel_page(&l, &s, ACID_USER_FIRST + 3, &ev) == 1 && l.mix == -1 && l.user == 3);
    CHECK(acid_link_panel_page(&l, &s, PL_PAGE_MIX_DRUMS + PL_MIX_PAGES, &ev) == 0);
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
    CHECK(acid_link_page(&l, &s) == ACID_USER_FIRST + 1);
}

TEST(test_custom_edit_marks_due)
{
    reset_custom();
    CHECK(acid_link_custom_edit(&l, 2, 3, 0, 0, 1, 0, 127) == 1);
    CHECK(l.custom_due == (1 << 2));
    CHECK(acid_link_custom_edit(&l, 2, 3, 0, 6, 1, 0, 127) == 0);
}

TEST(test_targets_do_not_toast)
{
    reset_custom();
    acid_custom_edit(&c, 0, 2, 0, 6, 2, 0, 127);
    acid_custom_edit(&c, 0, 2, 1, 0, 1, 0, 127);
    bsp(1, 10);
    bsp(3, 50);
    CHECK(ev.page == ACID_USER_FIRST && ev.src == ACID_SRC_BSP); // всплывашка - слот своей страницы
    CHECK(step() == 1 && ev.src != ACID_SRC_BSP);
    while (take()) {
        CHECK(ev.src != ACID_SRC_BSP); // цели - отражение, не поворот на их странице
    }
}

TEST(test_rejected_edit_resyncs_panel)
{
    reset_custom();
    CHECK(acid_link_custom_edit(&l, 3, 2, 2, 0, 1, 0, 127) == 0); // idx 2 при пустом слоте
    CHECK(l.custom_due == (1 << 3)); // панели правду, иначе её редактор застрянет на чужой картине
    l.custom_due = 0;
    CHECK(acid_link_custom_edit(&l, 9, 2, 0, 0, 1, 0, 127) == 0);
    CHECK(l.custom_due == 0);
}

// Два макро по 4 цели: 8 разных ручек.
static void two_macros(void)
{
    reset_custom();
    for (int i = 0; i < 4; i++) {
        acid_custom_edit(&c, 0, 1, i, 0, 1 + i, 0, 127); // E2: микс 2..5
        acid_custom_edit(&c, 0, 2, i, 8, 2 + i, 0, 127); // E3: эффекты 3..6
    }
}

static int drain(void)
{
    int n = step();
    while (n && take()) {
        n++;
    }
    return n;
}

TEST(test_macro_targets_limited_per_step)
{
    two_macros();
    panel(ACID_USER_FIRST, 2, 100);
    panel(ACID_USER_FIRST, 3, 100);
    for (int i = 0; i < 4; i++) {
        CHECK(drain() == 2);
    }
    CHECK(drain() == 0);
    for (int i = 0; i < 4; i++) {
        CHECK(knob(0, 2 + i) == 100 && knob(8, 3 + i) == 100);
    }
}

TEST(test_macro_latest_value_wins)
{
    two_macros();
    panel(ACID_USER_FIRST, 2, 10);
    panel(ACID_USER_FIRST, 2, 20);
    panel(ACID_USER_FIRST, 2, 30);
    CHECK(drain() == 2); // одна запись на ручку, не три
    CHECK(drain() == 2);
    CHECK(knob(0, 2) == 30 && knob(0, 5) == 30);
    CHECK(drain() == 0);
}

TEST(test_direct_set_cancels_deferred)
{
    two_macros();
    panel(ACID_USER_FIRST, 2, 100);
    panel(0, 3, 7); // прямая правка цели новее макро
    bsp(1, 0);
    bsp(5, 9); // и с BSP
    CHECK(drain() == 2);
    CHECK(knob(0, 3) == 7 && knob(0, 5) == 9 && knob(0, 2) == 100 && knob(0, 4) == 100);
}

TEST(test_requeue_after_cancel)
{
    two_macros();
    panel(ACID_USER_FIRST, 2, 100);
    panel(0, 2, 7);
    panel(ACID_USER_FIRST, 2, 50); // снова макро: запись живая, без дублей в очереди
    CHECK(drain() == 2);
    CHECK(drain() == 2);
    CHECK(knob(0, 2) == 50);
    CHECK(drain() == 0);
}

TEST(test_init_drops_deferred)
{
    two_macros();
    panel(ACID_USER_FIRST, 2, 100);
    acid_link_init(&l);
    CHECK(drain() == 0);
}

// Вкладка MIX инструмента открыта на панели: энкодеры BSP крутят её слоты, слот ведёт на штатную ручку.
TEST(test_mix_page_from_bsp)
{
    reset();
    bsp(1, 6);
    CHECK(acid_link_panel_page(&l, &s, PL_PAGE_MIX_SYN1, &ev) == 1);
    CHECK(ev.enc == 0 && ev.value == PL_PAGE_MIX_SYN1 && acid_link_page(&l, &s) == PL_PAGE_MIX_SYN1);
    CHECK(s.page == 6); // штатная страница синта прежняя
    CHECK(bsp(2, 90) == 1);
    CHECK(knob(0, 4) == 90); // громкость синта 1: MIX E4
    CHECK(ev.page == 0 && ev.enc == 3 && ev.value == 90 && ev.bsp_pos == 90 && ev.src == ACID_SRC_BSP);
    CHECK(bsp(6, 70) == 1);
    CHECK(knob(6, 10) == 70); // TRANS у 303 - E10
    int was = knob(0, 12);
    CHECK(bsp(12, 50) == 0); // пустой слот
    CHECK(knob(0, 12) == was && knob(6, 12) != 50);
    CHECK(acid_link_panel_page(&l, &s, PL_PAGE_MIX_DRUMS, &ev) == 1);
    CHECK(bsp(2, 55) == 1 && knob(0, 6) == 55); // бочка: MIX E6
    bsp(1, 2); // энкодер 1 - снова страница с BSP
    CHECK(acid_link_page(&l, &s) == 2);
    bsp(2, 33);
    CHECK(knob(2, 2) == 33);
    CHECK(acid_link_panel_page(&l, &s, PL_PAGE_MIX_DRUMS + PL_MIX_PAGES, &ev) == 0);
}

// Подхват на вкладке MIX - по цели: ручку, поправленную с панели, энкодер не двигает, пока не дойдёт.
TEST(test_mix_page_pickup)
{
    reset();
    panel(0, 4, 100);
    acid_link_panel_page(&l, &s, PL_PAGE_MIX_SYN1, &ev);
    CHECK(bsp(2, 20) == 1);
    CHECK(knob(0, 4) == 100 && ev.held == 1 && ev.page == 0 && ev.enc == 3 && ev.bsp_pos == 20);
    bsp(2, 110);
    CHECK(knob(0, 4) == 110 && ev.held == 0);
}

TEST(test_bsp_user_page_still_10)
{
    reset_custom();
    CHECK(bsp(1, 10) == 1);
    CHECK(ev.page == ACID_USER_FIRST && ev.value == ACID_USER_FIRST);
    CHECK(acid_link_page(&l, &s) == ACID_USER_FIRST);
    CHECK(bsp(1, 15) == 1);
    CHECK(acid_link_page(&l, &s) == ACID_USER_FIRST + 5);
}

TEST(test_bsp_layer_page)
{
    reset();
    CHECK(bsp(1, 16) == 1);
    CHECK(s.page == ACID_PAGE_LAYER1 && ev.value == ACID_PAGE_LAYER1);
    CHECK(bsp(1, 17) == 1);
    CHECK(s.page == ACID_PAGE_LAYER1 + 1);
    CHECK(bsp(1, 18) == 1); // 18 - чашка Петри
    CHECK(s.page == ACID_PAGE_PETRI);
    CHECK(bsp(1, 19) == 0);
    CHECK(s.page == ACID_PAGE_PETRI);
}

TEST(test_link_layer_type_report)
{
    reset();
    bsp(1, 16);
    l.snapshot_due = 0;
    bsp(2, 0); // TYPE слоя: у каждого типа своя память, панели нужна вся страница
    CHECK(l.snapshot_due);
    CHECK(acid_synth_knob(&s, ACID_PAGE_LAYER1, 1) == 0);
}

static acid_mod_t m;

static void reset_mod(void)
{
    reset_custom();
    acid_mod_init(&m, 32.0f / SR);
    acid_link_use_mod(&l, &m);
}

TEST(test_mod_page_from_bsp_and_panel)
{
    reset();
    CHECK(bsp(1, 19) == 0); // без модуляторов страницы 19..22 игнорируются
    reset_mod();
    CHECK(acid_page_from_bsp(19) == ACID_PAGE_MOD1 && acid_page_from_bsp(22) == ACID_PAGE_MOD1 + 3);
    CHECK(acid_page_to_bsp(ACID_PAGE_MOD1) == 19 && acid_page_to_bsp(ACID_PAGE_MOD1 + 3) == 22);
    CHECK(bsp(1, 19) == 1);
    CHECK(ev.page == ACID_PAGE_MOD1 && ev.enc == 0 && ev.value == ACID_PAGE_MOD1);
    CHECK(acid_link_page(&l, &s) == ACID_PAGE_MOD1);
    CHECK(bsp(1, 23) == 0 && acid_link_page(&l, &s) == ACID_PAGE_MOD1);

    // Ручка модулятора с BSP; значение вне списка оставляет прежнее.
    CHECK(bsp(2, ACID_SHAPE_ENV) == 1 && ev.page == ACID_PAGE_MOD1 && ev.enc == 1 && ev.value == ACID_SHAPE_ENV);
    CHECK(acid_mod_knob(&m, 0, ACID_MOD_SHAPE) == ACID_SHAPE_ENV);
    CHECK(bsp(2, 99) == 1 && ev.value == ACID_SHAPE_ENV && acid_mod_knob(&m, 0, ACID_MOD_SHAPE) == ACID_SHAPE_ENV);

    // Правка с панели и подхват BSP на странице модулятора.
    bsp(3, 90);
    CHECK(panel(ACID_PAGE_MOD1, 3, 20) == 1);
    CHECK(ev.page == ACID_PAGE_MOD1 && ev.enc == 2 && ev.value == 20 && ev.held == 1);
    CHECK(bsp(3, 80) == 1 && ev.value == 20 && ev.held == 1);
    CHECK(bsp(3, 15) == 1 && ev.value == 15 && ev.held == 0 && acid_mod_knob(&m, 0, ACID_MOD_RATE) == 15);
    CHECK(panel(ACID_PAGE_MOD1, 2, 99) == 0); // вне списка

    // Переключение между страницами модуляторов, своими, MIX и штатными.
    CHECK(acid_link_panel_page(&l, &s, ACID_PAGE_MOD1 + 2, &ev) == 1 && l.mod_page == 2);
    CHECK(acid_link_page(&l, &s) == ACID_PAGE_MOD1 + 2);
    CHECK(acid_link_panel_page(&l, &s, ACID_USER_FIRST + 1, &ev) == 1 && l.mod_page == -1 && l.user == 1);
    CHECK(acid_link_panel_page(&l, &s, ACID_PAGE_MOD1 + 1, &ev) == 1 && l.user == -1 && l.mod_page == 1);
    CHECK(acid_link_panel_page(&l, &s, PL_PAGE_MIX_SYN1, &ev) == 1 && l.mod_page == -1 && l.mix == PL_PAGE_MIX_SYN1);
    CHECK(bsp(1, 20) == 1 && l.mix == -1 && l.mod_page == 1);
    CHECK(bsp(1, 6) == 1 && l.mod_page == -1 && s.page == 6);
    acid_link_init(&l);
    CHECK(l.mod == &m && l.mod_page == -1);
}

TEST(test_mod_edit_and_shared_budget)
{
    reset_mod();
    CHECK(acid_link_mod_edit(&l, &s, 0, 0, 6, 2) == 1 && l.mod_due == 1); // CUTOFF (плавная)
    l.mod_due = 0;
    CHECK(acid_link_mod_edit(&l, &s, 0, 1, 6, 1) == 0 && l.mod_due == 1); // TYPE (список): отказ, но кадр шлём
    CHECK(acid_link_mod_edit(&l, &s, 0, 1, 6, 7) == 0);                   // WAVE у 303 (переключатель)
    acid_synth_set_knob(&s, 6, 1, 1);                                     // Plaits: enc 7 теперь GLIDE (плавная)
    CHECK(acid_link_mod_edit(&l, &s, 0, 1, 6, 7) == 1);
    CHECK(acid_link_mod_edit(&l, &s, 0, 1, ACID_MOD_NONE, 0) == 1 && m.m[0].t[1].page == ACID_MOD_NONE);
    l.mod_due = 0;
    CHECK(acid_link_mod_edit(&l, &s, 4, 0, 0, 1) == 0 && l.mod_due == 0);

    // Общий бюджет куска: цели макро и модулятор вместе применяют не больше 2 ручек.
    for (int i = 0; i < 3; i++) {
        acid_custom_edit(&c, 0, 1, i, 0, 1 + i, 0, 127);
    }
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_SQR);
    acid_link_mod_edit(&l, &s, 0, 0, 8, 3);
    acid_link_mod_edit(&l, &s, 0, 1, 8, 4);
    acid_mod_set(&m, 0, ACID_MOD_DEPTH1, 100);
    acid_mod_set(&m, 0, ACID_MOD_DEPTH1 + 1, 100);
    panel(ACID_USER_FIRST, 2, 90);
    CHECK(step() == 1 && l.step_n == 2);
    while (take()) {
    }
    CHECK(acid_mod_step(&m, &s, ACID_LINK_PER_CHUNK - l.step_n) == 0);
    CHECK(step() == 1 && l.step_n == 1);
    CHECK(acid_mod_step(&m, &s, ACID_LINK_PER_CHUNK - l.step_n) == 1);
    CHECK(step() == 0 && l.step_n == 0);
    CHECK(acid_mod_step(&m, &s, ACID_LINK_PER_CHUNK - l.step_n) == 1);
}

TEST(test_pitch_panel_only)
{
    reset();
    bsp(1, 6);
    CHECK(panel(ACID_PAGE_PITCH, 3, 2) == 1);
    CHECK(knob(ACID_PAGE_PITCH, 3) == 2);
    CHECK(ev.page == ACID_PAGE_PITCH && ev.enc == 2 && ev.value == 2 && ev.src == ACID_SRC_PANEL);
    CHECK(l.held[ACID_PAGE_PITCH][2] == 1);
    CHECK(panel(ACID_PAGE_PITCH, 5, 64) == 1 && knob(ACID_PAGE_PITCH, 5) == 64);
    CHECK(panel(ACID_PAGE_PITCH, 7, 1) == 0); // внутри 6: за S2 GATE ручек нет
    CHECK(acid_link_panel_page(&l, &s, ACID_PAGE_PITCH, &ev) == 0);
    CHECK(s.page == 6);
}

int main(void)
{
    RUN(test_link_layer_type_report);
    RUN(test_bsp_user_page_still_10);
    RUN(test_bsp_layer_page);
    RUN(test_pickup_blocks_until_cross);
    RUN(test_pickup_skip_over);
    RUN(test_pickup_from_below);
    RUN(test_pickup_per_page);
    RUN(test_pickup_unknown_bsp_pos);
    RUN(test_demo_muted_after_panel);
    RUN(test_type_change_sends_snapshot);
    RUN(test_page_event);
    RUN(test_panel_page);
    RUN(test_reverb_panel_only);
    RUN(test_user_page_macro_from_bsp);
    RUN(test_user_page_alias_from_bsp);
    RUN(test_user_page_empty_slot);
    RUN(test_user_page_no_custom);
    RUN(test_back_to_standard_page);
    RUN(test_user_page_pickup);
    RUN(test_user_panel_set_macro);
    RUN(test_user_panel_page);
    RUN(test_mix_page_from_bsp);
    RUN(test_mix_page_pickup);
    RUN(test_link_init_keeps_custom);
    RUN(test_page_reports_user);
    RUN(test_custom_edit_marks_due);
    RUN(test_targets_do_not_toast);
    RUN(test_rejected_edit_resyncs_panel);
    RUN(test_macro_targets_limited_per_step);
    RUN(test_macro_latest_value_wins);
    RUN(test_direct_set_cancels_deferred);
    RUN(test_requeue_after_cancel);
    RUN(test_init_drops_deferred);
    RUN(test_mod_page_from_bsp_and_panel);
    RUN(test_mod_edit_and_shared_budget);
    RUN(test_pitch_panel_only);
    TEST_MAIN_END();
}
