// Экран Grids панели: раскладка, попадание пальцем, ручка под элементом.
#include "pl_grids_ui.h"
#include "test.h"

static void center(int id, int idx, int *x, int *y)
{
    int rx, ry, rw, rh;
    pl_gui_rect(id, idx, &rx, &ry, &rw, &rh);
    *x = rx + rw / 2;
    *y = ry + rh / 2;
}

// Каждый элемент попадается в своём центре и только им.
TEST(test_hit_centers)
{
    static const int N[PL_GUI_IDS] = {[PL_GUI_ON] = 1, [PL_GUI_FILL] = 1, [PL_GUI_HAT] = 3, [PL_GUI_TIME] = 2,
                                      [PL_GUI_EVERY] = 4, [PL_GUI_LEN] = 3, [PL_GUI_SLIDER] = 4, [PL_GUI_XY] = 1};
    for (int id = 1; id < PL_GUI_IDS; id++) {
        for (int i = 0; i < N[id]; i++) {
            int x, y, arg = -1;
            center(id, i, &x, &y);
            CHECK(pl_gui_at(x, y, &arg) == id);
            CHECK(arg == i);
        }
    }
}

// Элементы между полосой вкладок и доком (y 76..432) и не налезают друг на друга.
TEST(test_rects_inside_and_apart)
{
    int x, y, w, h;
    for (int id = 1; id < PL_GUI_IDS; id++) {
        for (int i = 0; i < 4; i++) {
            if (!pl_gui_rect(id, i, &x, &y, &w, &h)) {
                continue;
            }
            CHECK(x >= 0 && y >= 76 && x + w <= 480 && y + h <= 432 && w > 0 && h > 0);
            int arg;
            CHECK(pl_gui_at(x, y, &arg) == id && arg == i); // угол свой
        }
    }
    CHECK(pl_gui_at(5, 20, &x) == PL_GUI_NONE);  // шапка
    CHECK(pl_gui_at(5, 60, &x) == PL_GUI_NONE);  // вкладки
    CHECK(pl_gui_at(5, 450, &x) == PL_GUI_NONE); // док
}

TEST(test_enc_of)
{
    CHECK(pl_gui_enc(PL_GUI_ON, 0) == 1);
    CHECK(pl_gui_enc(PL_GUI_SLIDER, 0) == 4);
    CHECK(pl_gui_enc(PL_GUI_SLIDER, 3) == 7);
    CHECK(pl_gui_enc(PL_GUI_HAT, 2) == 8);
    CHECK(pl_gui_enc(PL_GUI_TIME, 1) == 9);
    CHECK(pl_gui_enc(PL_GUI_EVERY, 0) == 10);
    CHECK(pl_gui_enc(PL_GUI_LEN, 0) == 11);
    CHECK(pl_gui_enc(PL_GUI_FILL, 0) == 12);
    CHECK(pl_gui_enc(PL_GUI_XY, 0) == 0); // две ручки: X и Y через pl_xy
}

// Ползунок: верх - 127, низ - 0, за краями зажат.
TEST(test_slider_value)
{
    int x, y, w, h;
    pl_gui_rect(PL_GUI_SLIDER, 1, &x, &y, &w, &h);
    CHECK(pl_gui_slider_value(y) == 127);
    CHECK(pl_gui_slider_value(y + h - 1) == 0);
    CHECK(pl_gui_slider_value(y - 50) == 127);
    CHECK(pl_gui_slider_value(y + h + 50) == 0);
    int mid = pl_gui_slider_value(y + h / 2);
    CHECK(mid > 55 && mid < 72);
}

// Клетка сетки шагов: 32 столбца, 3 дорожки, под сеткой помещается бегущий шаг до дока.
TEST(test_cell_rects)
{
    int x0, y0, w, h, x1, y1;
    CHECK(pl_gui_cell(0, 0, &x0, &y0, &w, &h));
    CHECK(pl_gui_cell(31, 2, &x1, &y1, &w, &h));
    CHECK(x1 > x0 && y1 > y0 && y0 >= 76 && x1 + w <= 480 && y1 + h + 6 <= 432);
    CHECK(!pl_gui_cell(32, 0, &x0, &y0, &w, &h));
}

int main(void)
{
    RUN(test_hit_centers);
    RUN(test_rects_inside_and_apart);
    RUN(test_enc_of);
    RUN(test_slider_value);
    RUN(test_cell_rects);
    TEST_MAIN_END();
}
