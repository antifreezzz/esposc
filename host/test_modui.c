// Экран модулятора на панели: сырые значения страниц, ячейки сетки, точка действующего значения, выбор целей.
#include <string.h>

#include "pl_edit.h"
#include "pl_gesture.h"
#include "pl_labels.h"
#include "pl_mod.h"
#include "pl_model.h"
#include "pl_view.h"
#include "test.h"

static pl_model_t m;
static pl_edit_t e;
static pl_mod_edit_t out;

static void setup(void)
{
    pl_model_init(&m);
    m.have = 1;
    m.mod_have = 0xF;
    for (int k = 0; k < PL_MOD_PAGES; k++) {
        m.mod_knob[k][8] = 127;
        for (int i = 0; i < PL_MOD_TARGETS; i++) {
            m.mod_knob[k][9 + i] = 64;
        }
    }
}

TEST(test_model_raw)
{
    setup();
    CHECK(pl_model_raw(&m, 0) == m.raw[0] && pl_model_raw(&m, PL_PAGES - 1) == m.raw[PL_PAGES - 1]);
    CHECK(pl_model_raw(&m, PL_PAGE_MOD1) == m.mod_knob[0] && pl_model_raw(&m, PL_PAGE_MOD1 + 3) == m.mod_knob[3]);
    CHECK(pl_model_raw(&m, PL_PAGES) == NULL && pl_model_raw(&m, PL_USER_FIRST) == NULL);
    CHECK(pl_model_raw(&m, PL_PAGE_MIX_DRUMS) == NULL && pl_model_raw(&m, PL_PAGE_MOD1 + 4) == NULL && pl_model_raw(&m, -1) == NULL);
}

TEST(test_view_cell_mod_page)
{
    setup();
    m.mod_knob[1][1] = 1;
    m.mod_knob[1][2] = 100;
    m.mod_knob[1][11] = 100;
    m.mod_held[1][4] = 1;
    pl_cell_t c;
    pl_view_cell(&m, PL_PAGE_MOD1 + 1, 1, &c);
    CHECK(c.label && !strcmp(c.label, "SHAPE") && !strcmp(c.text, "TRI") && c.v == 1 && c.lo == 0 && c.hi == 7 && c.held == 0);
    CHECK(c.color != 0 && c.src == NULL && c.eff == -1);
    pl_view_cell(&m, PL_PAGE_MOD1 + 1, 2, &c);
    CHECK(!strcmp(c.label, "RATE") && c.v == 100 && c.lo == 0 && c.hi == 127 && !c.bipolar);
    pl_view_cell(&m, PL_PAGE_MOD1 + 1, 4, &c);
    CHECK(!strcmp(c.label, "TRIG") && c.held == 1 && c.hi == 13);
    pl_view_cell(&m, PL_PAGE_MOD1 + 1, 11, &c);
    CHECK(!strcmp(c.label, "DEPTH 3") && c.bipolar && c.v == 100 && !strcmp(c.text, "+36") && c.src == NULL);
    pl_view_cell(&m, PL_PAGE_MOD1 + 1, 13, &c);
    CHECK(c.label == NULL);
    pl_view_cell(&m, PL_PAGE_MOD1 + 4, 1, &c);
    CHECK(c.label == NULL);
    // Под именем глубины - цель: её подпись и цвет страницы цели; пустая или негодная цель - нет.
    m.mod_t[1][2] = (pl_mod_target_t){0, 2}; // MIX DRIVE
    pl_view_cell(&m, PL_PAGE_MOD1 + 1, 11, &c);
    CHECK(c.src && !strcmp(c.src, "DRIVE") && c.src_color != 0);
    m.mod_t[1][2] = (pl_mod_target_t){9, 2}; // TONAL: так не бывает, но и не рисуем
    pl_view_cell(&m, PL_PAGE_MOD1 + 1, 11, &c);
    CHECK(c.src == NULL);
    CHECK(!strcmp(pl_view_page_name(PL_PAGE_MOD1 + 2), "MOD 3") && pl_view_page_color(PL_PAGE_MOD1) != 0);
}

// Карточка ручки-цели: база в v, действующее значение в eff.
TEST(test_view_cell_eff)
{
    setup();
    pl_cell_t c;
    m.raw[0][2] = 60;
    pl_view_cell(&m, 0, 2, &c);
    CHECK(c.v == 60 && c.eff == -1); // не цель - точки нет
    m.mod_t[0][0] = (pl_mod_target_t){0, 2};
    m.mod_knob[0][9] = 127; // DEPTH 1 +63
    m.mod_out[0] = 64;
    pl_view_cell(&m, 0, 2, &c);
    CHECK(c.v == 60 && c.eff == pl_mod_effective(&m, 0, 2) && c.eff > 60);
    m.mod_out[0] = 0;
    pl_view_cell(&m, 0, 2, &c);
    CHECK(c.v == 60 && c.eff == 60); // цель, но сейчас в центре: точка на базе
    pl_view_cell(&m, 0, 3, &c);
    CHECK(c.eff == -1);
    // Список и переключатель целью быть не могут: точки нет, даже если цель записана.
    m.mod_t[0][1] = (pl_mod_target_t){8, 14};
    pl_view_cell(&m, 8, 14, &c);
    CHECK(c.eff == -1);
    // На вкладке MIX карточка - ячейка цели, точка тоже.
    m.mod_t[0][2] = (pl_mod_target_t){0, 5};
    m.mod_out[0] = 127;
    pl_view_cell(&m, PL_PAGE_MIX_DRUMS, 1, &c); // E2 DRUMS MIX - бочка, MIX E6
    CHECK(c.label && c.eff == pl_mod_effective(&m, 0, 5));
    // Своя страница с псевдонимом: то же.
    m.user_have = 1;
    m.user_t[0][3][0] = (pl_target_t){0 << 4 | 2, 0, 127};
    pl_view_cell(&m, PL_USER_FIRST, 3, &c);
    CHECK(c.label && c.eff == pl_mod_effective(&m, 0, 2));
    // Страница модулятора сама не цель.
    pl_view_cell(&m, PL_PAGE_MOD1, 2, &c);
    CHECK(c.eff == -1);
}

TEST(test_pickable_pages)
{
    setup();
    for (int p = 0; p < PL_PAGES; p++) {
        int want = p <= 8 || (p >= 10 && p <= 12);
        CHECK(pl_mod_page_has_target(&m, p) == want);
    }
    m.raw[10][1] = PL_LAYER_OFF; // выключенный слой - плавных ручек нет
    CHECK(!pl_mod_page_has_target(&m, 10) && pl_mod_page_has_target(&m, 11));
    CHECK(!pl_mod_page_has_target(&m, PL_USER_FIRST) && !pl_mod_page_has_target(&m, PL_PAGE_MOD1) && !pl_mod_page_has_target(&m, -1));
}

TEST(test_edit_mod_targets_path)
{
    setup();
    pl_edit_mod_start(&e, 2);
    CHECK(e.state == PL_EDIT_TARGETS && e.mod == 1 && e.up == 2);
    pl_edit_mod_row(&e, &m, 1);
    CHECK(e.state == PL_EDIT_PICK_PAGE && e.idx == 1);
    pl_edit_mod_page(&e, &m, 9); // TONAL: ни одной плавной ручки целью
    CHECK(e.state == PL_EDIT_PICK_PAGE);
    pl_edit_mod_page(&e, &m, PL_PAGE_PETRI);
    CHECK(e.state == PL_EDIT_PICK_PAGE);
    pl_edit_mod_page(&e, &m, 6);
    CHECK(e.state == PL_EDIT_PICK_KNOB && e.page == 6);
    CHECK(pl_edit_mod_knob(&e, &m, 1, &out) == 0); // TYPE синта
    CHECK(e.state == PL_EDIT_PICK_KNOB);
    CHECK(pl_edit_mod_knob(&e, &m, 7, &out) == 0); // WAVE у 303 - список
    CHECK(pl_edit_mod_knob(&e, &m, 2, &out) == 1);
    CHECK(out.mod == 2 && out.idx == 1 && out.page == 6 && out.enc == 2);
    CHECK(e.state == PL_EDIT_TARGETS && m.mod_t[2][1].page == 6 && m.mod_t[2][1].enc == 2);
    CHECK(pl_edit_mod_knob(&e, &m, 3, &out) == 0); // не тот шаг
}

TEST(test_edit_mod_row_replaces_and_removes)
{
    setup();
    m.mod_t[0][0] = (pl_mod_target_t){0, 2};
    pl_edit_mod_start(&e, 0);
    pl_edit_mod_row(&e, &m, 0); // тап по занятой строке: выбрать другую цель на её место
    CHECK(e.state == PL_EDIT_PICK_PAGE && e.idx == 0);
    pl_edit_mod_page(&e, &m, 12);
    CHECK(pl_edit_mod_knob(&e, &m, 2, &out) == 1 && out.idx == 0 && out.page == 12 && m.mod_t[0][0].page == 12);
    CHECK(pl_edit_mod_remove(&e, &m, 0, &out) == 1);
    CHECK(out.mod == 0 && out.idx == 0 && out.page == PL_NONE && m.mod_t[0][0].page == PL_NONE);
    CHECK(pl_edit_mod_remove(&e, &m, 0, &out) == 0); // убирать нечего
    CHECK(pl_edit_mod_remove(&e, &m, 4, &out) == 0 && pl_edit_mod_remove(&e, &m, -1, &out) == 0);
    pl_edit_mod_row(&e, &m, 4);
    CHECK(e.state == PL_EDIT_TARGETS); // нет такой строки
}

TEST(test_edit_mod_row_gesture)
{
    // Обычное касание часто длиннее общего HOLD на 50 мс: оно всё равно выбирает строку.
    CHECK(pl_edit_mod_row_action(PL_PRESS_NONE, 49, 0) == PL_MOD_ROW_NONE);
    CHECK(pl_edit_mod_row_action(PL_PRESS_HOLD, 50, 1) == PL_MOD_ROW_NONE);
    CHECK(pl_edit_mod_row_action(PL_PRESS_TAP, 30, 0) == PL_MOD_ROW_SELECT);
    CHECK(pl_edit_mod_row_action(PL_PRESS_RELEASE, 100, 0) == PL_MOD_ROW_SELECT);
    CHECK(pl_edit_mod_row_action(PL_PRESS_RELEASE, 599, 1) == PL_MOD_ROW_SELECT);
    CHECK(pl_edit_mod_row_action(PL_PRESS_RELEASE, 600, 1) == PL_MOD_ROW_REMOVE);
    CHECK(pl_edit_mod_row_action(PL_PRESS_RELEASE, 1000, 1) == PL_MOD_ROW_REMOVE);
    CHECK(pl_edit_mod_row_action(PL_PRESS_RELEASE, 1000, 0) == PL_MOD_ROW_SELECT);
    CHECK(pl_edit_mod_row_action(PL_PRESS_NONE, 1000, 1) == PL_MOD_ROW_NONE);
}

TEST(test_edit_mod_needs_frame_and_back)
{
    setup();
    m.mod_have = 0; // кадра модулятора ещё не было: правка не принимается
    pl_edit_mod_start(&e, 1);
    pl_edit_mod_row(&e, &m, 0);
    pl_edit_mod_page(&e, &m, 0);
    CHECK(pl_edit_mod_knob(&e, &m, 2, &out) == 0);
    m.mod_have = 0xF;
    pl_edit_mod_row(&e, &m, 0);
    CHECK(e.state == PL_EDIT_PICK_PAGE || e.state == PL_EDIT_PICK_KNOB);
    pl_edit_mod_start(&e, 1);
    pl_edit_mod_row(&e, &m, 0);
    pl_edit_mod_page(&e, &m, 0);
    CHECK(e.state == PL_EDIT_PICK_KNOB);
    pl_edit_back(&e);
    CHECK(e.state == PL_EDIT_PICK_PAGE);
    pl_edit_back(&e);
    CHECK(e.state == PL_EDIT_TARGETS);
    pl_edit_back(&e);
    CHECK(e.state == PL_EDIT_OFF); // из списка целей модулятора - наружу, не в слоты
    // Редактор своих страниц как был.
    pl_edit_start(&e, 0);
    CHECK(e.mod == 0);
    pl_edit_slot(&e, 2);
    pl_edit_back(&e);
    CHECK(e.state == PL_EDIT_SLOTS);
    pl_edit_mod_row(&e, &m, 0); // чужой редактор не трогаем
    CHECK(e.state == PL_EDIT_SLOTS);
}

int main(void)
{
    RUN(test_model_raw);
    RUN(test_view_cell_mod_page);
    RUN(test_view_cell_eff);
    RUN(test_pickable_pages);
    RUN(test_edit_mod_targets_path);
    RUN(test_edit_mod_row_replaces_and_removes);
    RUN(test_edit_mod_row_gesture);
    RUN(test_edit_mod_needs_frame_and_back);
    TEST_MAIN_END();
}
