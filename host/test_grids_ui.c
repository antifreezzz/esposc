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
                                      [PL_GUI_EVERY] = 4, [PL_GUI_LEN] = 3, [PL_GUI_SLIDER] = 6, [PL_GUI_XY] = 1,
                                      [PL_GUI_SRC] = 1, [PL_GUI_GRID] = 1};
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
        for (int i = 0; i < 6; i++) {
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
    CHECK(pl_gui_enc(PL_GUI_SLIDER, 4) == 14); // SWING
    CHECK(pl_gui_enc(PL_GUI_SLIDER, 5) == 15); // ROLL
    int x, y, w, h;
    CHECK(pl_gui_rect(PL_GUI_SLIDER, 5, &x, &y, &w, &h) && w >= 40 && x + w <= 472);
    CHECK(!pl_gui_rect(PL_GUI_SLIDER, 6, &x, &y, &w, &h));
    CHECK(pl_gui_enc(PL_GUI_SRC, 0) == 13);
    CHECK(pl_gui_enc(PL_GUI_GRID, 0) == 0); // тап по сетке открывает редактор
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

#include "pl_grids.h"

// Свинг - ползунок на 4 положения: низ 0 (50 %), верх 3 (75 %).
TEST(test_swing_value)
{
    int x, y, w, h, seen = 0;
    pl_gui_rect(PL_GUI_SLIDER, 4, &x, &y, &w, &h);
    CHECK(pl_gui_swing_value(y) == 3 && pl_gui_swing_value(y + h - 1) == 0);
    CHECK(pl_gui_swing_value(y - 40) == 3 && pl_gui_swing_value(y + h + 40) == 0);
    for (int yy = y + h - 1, last = 0; yy >= y; yy--) {
        int v = pl_gui_swing_value(yy);
        CHECK(v >= last && v <= 3);
        last = v;
        seen |= 1 << v;
    }
    CHECK(seen == 15);
}

// Сетка превью: прямая - 32 клетки как раньше, триольная - 24 клетки пошире на той же ширине.
TEST(test_cell_grid)
{
    int x, y, w, h, x2, y2, w2, h2;
    for (int st = 0; st < 32; st++) {
        CHECK(pl_gui_cell(st, 1, &x, &y, &w, &h) && pl_gui_cell_at(PL_GRIDS_16, st, 1, &x2, &y2, &w2, &h2));
        CHECK(x == x2 && y == y2 && w == w2 && h == h2);
    }
    int gx, gy, gw, gh, prev = -1;
    pl_gui_rect(PL_GUI_GRID, 0, &gx, &gy, &gw, &gh);
    for (int st = 0; st < 32; st++) {
        int ok = pl_gui_cell_at(PL_GRIDS_12T, st, 2, &x, &y, &w, &h);
        CHECK(ok == (st % 16 < 12));
        if (ok) {
            CHECK(x > prev && x >= gx && x + w <= gx + gw && y >= gy && y + h <= gy + gh && w > 12);
            prev = x + w - 1;
        }
    }
    pl_gui_cell(31, 2, &x, &y, &w, &h);
    CHECK(x + w <= gx + gw && y + h <= gy + gh);
    pl_gui_cell(0, 0, &x, &y, &w, &h);
    CHECK(x >= gx && y >= gy);
}

// Редактор шагов: один такт в два яруса по 8 шагов (у триолей по 6) - клетки широкие, палец не промахивается;
// кнопки сверху и снизу, внутри яруса клетки без мёртвых щелей.
TEST(test_editor_layout)
{
    static const int N[PL_GED_IDS] = {[PL_GED_BACK] = 1, [PL_GED_SLOT] = 4, [PL_GED_BAR] = 2, [PL_GED_TRIP] = 1,
                                      [PL_GED_COPY] = 1, [PL_GED_CLEAR] = 1};
    int x, y, w, h, arg;
    for (int grid = 0; grid < 2; grid++) {
        for (int id = 1; id < PL_GED_IDS; id++) {
            for (int i = 0; i < 5; i++) {
                int ok = pl_ged_rect(id, i, &x, &y, &w, &h);
                CHECK(ok == (i < N[id]));
                if (ok) {
                    CHECK(x >= 0 && y >= 76 && x + w <= 480 && y + h <= 432);
                    CHECK(pl_ged_at(grid, x + w / 2, y + h / 2, &arg) == id && arg == i);
                    CHECK(pl_ged_at(grid, x, y, &arg) == id && arg == i);
                }
            }
        }
        int cols = grid ? 12 : 16;
        for (int part = 0; part < 3; part++) {
            for (int col = 0; col < 16; col++) {
                int ok = pl_ged_cell(grid, col, part, &x, &y, &w, &h);
                CHECK(ok == (col < cols));
                if (ok) {
                    CHECK(x >= 0 && y >= 76 && x + w <= 480 && y + h <= 432 && w >= 50 && h >= 30);
                    int per = cols / 2, bx, by, bw, bh, cx, cy, cw, ch;
                    pl_ged_rect(PL_GED_COPY, 0, &bx, &by, &bw, &bh);
                    CHECK(y + h + 6 <= by); // под ярусом место для бегущего шага
                    pl_ged_cell(grid, col % per, part, &cx, &cy, &cw, &ch);
                    CHECK(x == cx && (col < per ? y == cy : y >= cy + 3 * ch)); // второй ярус под первым
                    CHECK(pl_ged_at(grid, x + w / 2, y + h / 2, &arg) == PL_GED_CELL && arg == part * 16 + col);
                    CHECK(pl_ged_at(grid, x, y, &arg) == PL_GED_CELL && arg == part * 16 + col);
                    CHECK(pl_ged_at(grid, x + w - 1, y + h - 1, &arg) == PL_GED_CELL && arg == part * 16 + col);
                }
            }
        }
        CHECK(!pl_ged_cell(grid, 0, 3, &x, &y, &w, &h) && !pl_ged_cell(grid, -1, 0, &x, &y, &w, &h));
    }
    CHECK(pl_ged_at(0, 5, 20, &arg) == PL_GED_NONE);
    CHECK(pl_ged_at(0, 5, 450, &arg) == PL_GED_NONE);
}

// Тап по клетке: пусто -> удар -> акцент -> гост -> пусто; чужие уровни (копия карты, украшения) - по классу.
TEST(test_editor_cycle)
{
    CHECK(pl_ged_next(0) == PL_GRIDS_LV_HIT);
    CHECK(pl_ged_next(PL_GRIDS_LV_HIT) == PL_GRIDS_LV_ACC);
    CHECK(pl_ged_next(PL_GRIDS_LV_ACC) == PL_GRIDS_LV_GHOST);
    CHECK(pl_ged_next(PL_GRIDS_LV_GHOST) == 0);
    CHECK(pl_ged_next(PL_GRIDS_LV_EXTRA) == 0);
    CHECK(pl_ged_next(150) == PL_GRIDS_LV_ACC);
    CHECK(pl_ged_next(200) == PL_GRIDS_LV_GHOST);
}

int main(void)
{
    RUN(test_swing_value);
    RUN(test_cell_grid);
    RUN(test_editor_layout);
    RUN(test_editor_cycle);
    RUN(test_hit_centers);
    RUN(test_rects_inside_and_apart);
    RUN(test_enc_of);
    RUN(test_slider_value);
    RUN(test_cell_rects);
    TEST_MAIN_END();
}
