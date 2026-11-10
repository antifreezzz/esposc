// Экран PERF панели: раскладка в рабочей области 40..432, попадание пальцем, цели ручек и ползунки.
#include "pl_perf_ui.h"
#include "pl_proto.h"
#include "test.h"

static void center(int id, int idx, int *x, int *y)
{
    int rx, ry, rw, rh;
    pl_pui_rect(id, idx, &rx, &ry, &rw, &rh);
    *x = rx + rw / 2;
    *y = ry + rh / 2;
}

// Каждый элемент попадается в своём центре и возвращает свой индекс.
TEST(test_hit_centers)
{
    static const int N[PL_PUI_IDS] = {
        [PL_PUI_SCENE] = PL_PUI_SCENES,     [PL_PUI_MORPH] = 1, [PL_PUI_XY] = 1,   [PL_PUI_FILL] = 1,
        [PL_PUI_GRIDS] = 1,                 [PL_PUI_STUTTER] = PL_PUI_STUTTERS,    [PL_PUI_DJF] = 1,
        [PL_PUI_PUMP] = 1,                  [PL_PUI_FADER] = PL_PUI_FADERS,
    };
    for (int id = 1; id < PL_PUI_IDS; id++) {
        for (int i = 0; i < N[id]; i++) {
            int x, y, arg = -1;
            center(id, i, &x, &y);
            CHECK(pl_pui_at(x, y, &arg) == id);
            CHECK(arg == i);
        }
    }
    int x, y, w, h;
    CHECK(!pl_pui_rect(PL_PUI_NONE, 0, &x, &y, &w, &h));
    CHECK(!pl_pui_rect(PL_PUI_SCENE, 8, &x, &y, &w, &h));
    CHECK(!pl_pui_rect(PL_PUI_STUTTER, 5, &x, &y, &w, &h));
    CHECK(!pl_pui_rect(PL_PUI_FADER, 7, &x, &y, &w, &h));
}

// Все прямоугольники лежат между шапкой (40) и доком (432) и не пересекаются; XY ровно 200x200.
TEST(test_rects_inside_and_apart)
{
    int x, y, w, h;
    for (int id = 1; id < PL_PUI_IDS; id++) {
        for (int i = 0; i < 8; i++) {
            if (!pl_pui_rect(id, i, &x, &y, &w, &h)) {
                continue;
            }
            CHECK(x >= 0 && y >= 40 && x + w <= 480 && y + h <= 432 && w > 0 && h > 0);
            int arg = -1;
            CHECK(pl_pui_at(x, y, &arg) == id && arg == i);
            CHECK(pl_pui_at(x + w - 1, y + h - 1, &arg) == id && arg == i);
        }
    }
    CHECK(pl_pui_rect(PL_PUI_XY, 0, &x, &y, &w, &h));
    CHECK(w == 200 && h == 200);
    CHECK(pl_pui_at(10, 20, &x) == PL_PUI_NONE);  // шапка
    CHECK(pl_pui_at(10, 450, &x) == PL_PUI_NONE); // док
}

// Цели ручек синта за элементами PERF.
TEST(test_targets)
{
    int p = -1, e = -1;
    CHECK(!pl_pui_target(PL_PUI_SCENE, 0, &p, &e));
    CHECK(!pl_pui_target(PL_PUI_MORPH, 0, &p, &e));
    CHECK(!pl_pui_target(PL_PUI_XY, 0, &p, &e));
    CHECK(pl_pui_target(PL_PUI_FILL, 0, &p, &e) && p == PL_PAGE_GRIDS && e == 12);
    CHECK(pl_pui_target(PL_PUI_GRIDS, 0, &p, &e) && p == PL_PAGE_GRIDS && e == 1);
    CHECK(pl_pui_target(PL_PUI_STUTTER, 3, &p, &e) && p == 8 && e == 9);
    CHECK(pl_pui_target(PL_PUI_DJF, 0, &p, &e) && p == 0 && e == 11);
    CHECK(pl_pui_target(PL_PUI_PUMP, 0, &p, &e) && p == 0 && e == 9);
    // Громкости: MASTER, KICK, HATS, CLAP, SNARE, SYN 1, SYN 2 на странице MIX (0).
    static const int FENC[PL_PUI_FADERS] = {1, 5, 6, 7, 8, 3, 4};
    for (int i = 0; i < PL_PUI_FADERS; i++) {
        CHECK(pl_pui_target(PL_PUI_FADER, i, &p, &e));
        CHECK(p == 0 && e == FENC[i]);
    }
    CHECK(!pl_pui_target(PL_PUI_FADER, 7, &p, &e));
}

// Ползунки: горизонтальный (0..127 слева направо) и вертикальный (127..0 сверху вниз).
TEST(test_sliders)
{
    int x, y, w, h;
    pl_pui_rect(PL_PUI_DJF, 0, &x, &y, &w, &h);
    CHECK(pl_pui_hbar_value(x) == 0);
    CHECK(pl_pui_hbar_value(x + w - 1) == 127);
    CHECK(pl_pui_hbar_value(x - 40) == 0);
    CHECK(pl_pui_hbar_value(x + w + 40) == 127);
    int mid = pl_pui_hbar_value(x + w / 2);
    CHECK(mid >= 63 && mid <= 65);

    pl_pui_rect(PL_PUI_FADER, 0, &x, &y, &w, &h);
    CHECK(pl_pui_fader_value(y) == 127);
    CHECK(pl_pui_fader_value(y + h - 1) == 0);
    CHECK(pl_pui_fader_value(y - 40) == 127);
    CHECK(pl_pui_fader_value(y + h + 40) == 0);
    int vmid = pl_pui_fader_value(y + h / 2);
    CHECK(vmid >= 58 && vmid <= 68);
}

int main(void)
{
    RUN(test_hit_centers);
    RUN(test_rects_inside_and_apart);
    RUN(test_targets);
    RUN(test_sliders);
    TEST_MAIN_END();
}
