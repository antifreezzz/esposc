// Выбор типа синта сеткой на панели и переход к чашке Петри со страниц синта.
#include <string.h>

#include "pl_typepick.h"
#include "pl_view.h"
#include "test.h"

TEST(test_picker_only_on_type_knobs)
{
    CHECK(pl_tp_on(PL_PAGE_SYNTH1, 1));
    CHECK(pl_tp_on(PL_PAGE_SYNTH1 + 1, 1));
    CHECK(pl_tp_on(PL_PAGE_LAYER1, 1));
    CHECK(pl_tp_on(PL_PAGE_LAYER1 + 1, 1));
    CHECK(!pl_tp_on(PL_PAGE_SYNTH1, 2));
    CHECK(!pl_tp_on(3, 1)); // модель перкуссии - короткий список, крутилкой
    CHECK(!pl_tp_on(PL_PAGE_PETRI, 1));
    CHECK(!pl_tp_on(PL_USER_FIRST, 1));
}

TEST(test_synth_types_in_order)
{
    CHECK(pl_tp_count(PL_PAGE_SYNTH1) == 26);
    for (int s = 0; s < 26; s++) {
        CHECK(pl_tp_type(PL_PAGE_SYNTH1, s) == s);
        CHECK(pl_tp_slot(PL_PAGE_SYNTH1, s) == s);
    }
    CHECK(pl_tp_type(PL_PAGE_SYNTH1, 26) == -1);
    CHECK(pl_tp_type(PL_PAGE_SYNTH1, -1) == -1);
    CHECK(pl_tp_slot(PL_PAGE_SYNTH1, PL_LAYER_OFF) == -1); // у синта нет «выкл»
}

TEST(test_layer_types_skip_petri)
{
    CHECK(pl_tp_count(PL_PAGE_LAYER1) == 26);
    CHECK(pl_tp_type(PL_PAGE_LAYER1, 24) == 24);
    CHECK(pl_tp_type(PL_PAGE_LAYER1, 25) == PL_LAYER_OFF);
    CHECK(pl_tp_slot(PL_PAGE_LAYER1, PL_TYPE_PETRI) == -1);
    CHECK(pl_tp_slot(PL_PAGE_LAYER1, PL_LAYER_OFF) == 25);
    for (int s = 0; s < 26; s++) {
        CHECK(pl_tp_type(PL_PAGE_LAYER1, s) != PL_TYPE_PETRI);
    }
}

TEST(test_cells_hit_and_fit)
{
    for (int s = 0; s < PL_TP_COLS * PL_TP_ROWS; s++) {
        int x, y, w, h;
        pl_tp_rect(s, &x, &y, &w, &h);
        CHECK(x >= 0 && y >= 40 && x + w <= 480 && y + h <= 480 && w >= 100 && h >= 50); // под шапкой, под палец
        CHECK(pl_tp_at(x + w / 2, y + h / 2) == s);
        CHECK(pl_tp_at(x, y) == s && pl_tp_at(x + w - 1, y + h - 1) == s);
    }
    CHECK(pl_tp_at(240, 10) == -1); // шапка
    int x, y, w, h;
    pl_tp_rect(0, &x, &y, &w, &h);
    CHECK(pl_tp_at(x + w, y) == -1 || pl_tp_at(x + w, y) == 1); // промежуток между клетками - мимо
    CHECK(pl_tp_at(x + w + 1, y) == -1);
}

TEST(test_dish_link)
{
    static pl_model_t m;
    pl_model_init(&m);
    CHECK(!pl_view_dish(&m, PL_PAGE_PETRI)); // до снимка не знаем
    m.have = 1;
    CHECK(pl_view_dish(&m, PL_PAGE_PETRI));
    CHECK(!pl_view_dish(&m, PL_PAGE_SYNTH1));
    m.raw[PL_PAGE_SYNTH1 + 1][1] = PL_TYPE_PETRI;
    CHECK(pl_view_dish(&m, PL_PAGE_SYNTH1 + 1));
    CHECK(!pl_view_dish(&m, PL_PAGE_SYNTH1));
    CHECK(!pl_view_dish(&m, PL_PAGE_LAYER1 + 1)); // слой не PETRI, даже если синт PETRI
    CHECK(!pl_view_dish(&m, 8));
    CHECK(!pl_view_dish(&m, PL_USER_FIRST));
}

int main(void)
{
    RUN(test_picker_only_on_type_knobs);
    RUN(test_synth_types_in_order);
    RUN(test_layer_types_skip_petri);
    RUN(test_cells_hit_and_fit);
    RUN(test_dish_link);
    TEST_MAIN_END();
}
