// Шапка, вкладки и док панели по инструментам: кнопки, полосы, тап мимо ничего не делает.
#include <string.h>

#include "pl_ia.h"
#include "pl_nav.h"
#include "pl_proto.h"
#include "test.h"

TEST(test_main_bar)
{
    CHECK(pl_nav_at(PL_BAR_MAIN, 260) == PL_NAV_PLAY);
    CHECK(pl_nav_at(PL_BAR_MAIN, 360) == PL_NAV_MODE);
    CHECK(pl_nav_at(PL_BAR_MAIN, 40) == PL_NAV_NONE);  // имя группы - надпись
    CHECK(pl_nav_at(PL_BAR_MAIN, 450) == PL_NAV_NONE); // загрузка ядер - надпись
    CHECK(pl_nav_at(PL_BAR_MAIN, 20) != PL_NAV_CLOSE);
}

TEST(test_overlay_bar)
{
    CHECK(pl_nav_at(PL_BAR_OVERLAY, 20) == PL_NAV_CLOSE);
    CHECK(pl_nav_at(PL_BAR_OVERLAY, 240) == PL_NAV_NONE);
    CHECK(pl_nav_at(PL_BAR_OVERLAY, 440) == PL_NAV_ACTION);
    CHECK(pl_nav_at(PL_BAR_OVERLAY, 260) != PL_NAV_PLAY); // у оверлея своя шапка
}

TEST(test_buttons_fit_and_dont_overlap)
{
    for (int bar = 0; bar < PL_BARS; bar++) {
        int last = PL_NAV_NONE;
        for (int x = 0; x < 480; x++) {
            int a = pl_nav_at(bar, x);
            if (a != PL_NAV_NONE) {
                int x0, x1;
                pl_nav_rect(bar, a, &x0, &x1);
                CHECK(x >= x0 && x < x1 && x1 - x0 >= 44); // под палец
            }
            if (a != last && last != PL_NAV_NONE && a != PL_NAV_NONE) {
                CHECK(0); // между кнопками есть зазор
            }
            last = a;
        }
        int t0, t1;
        pl_nav_title(bar, &t0, &t1);
        CHECK(t1 - t0 >= 150);
        for (int x = t0; x < t1; x++) {
            CHECK(pl_nav_at(bar, x) == PL_NAV_NONE); // заголовок не пересекает кнопки
        }
    }
    int x0, x1;
    pl_nav_rect(PL_BAR_MAIN, PL_NAV_CLOSE, &x0, &x1);
    CHECK(x0 == x1); // нет кнопки - пустой прямоугольник
    CHECK(pl_nav_at(PL_BAR_MAIN, -5) == PL_NAV_NONE && pl_nav_at(9, 20) == PL_NAV_NONE);
}

TEST(test_tap_needs_same_button)
{
    CHECK(pl_nav_tap(PL_BAR_OVERLAY, 20, 10, 25, 30) == PL_NAV_CLOSE);
    CHECK(pl_nav_tap(PL_BAR_OVERLAY, 20, 10, 22, 52) == PL_NAV_CLOSE); // палец чуть ушёл вниз
    CHECK(pl_nav_tap(PL_BAR_OVERLAY, 20, 10, 22, 70) == PL_NAV_NONE);  // ушёл в рабочую область
    CHECK(pl_nav_tap(PL_BAR_OVERLAY, 20, 10, 440, 10) == PL_NAV_NONE); // съехал на другую кнопку
    CHECK(pl_nav_tap(PL_BAR_OVERLAY, 20, 60, 20, 10) == PL_NAV_NONE);  // начали не в шапке
    CHECK(pl_nav_tap(PL_BAR_MAIN, 360, 10, 362, 20) == PL_NAV_MODE);
}

// PLAY: тап - пуск и стоп, сдвиг вбок - темп, это уже не тап.
TEST(test_play_drag_is_not_tap)
{
    CHECK(pl_nav_tap(PL_BAR_MAIN, 260, 20, 262, 22) == PL_NAV_PLAY);
    CHECK(pl_nav_tap(PL_BAR_MAIN, 260, 20, 260 + PL_NAV_DRAG_PX + 1, 20) == PL_NAV_NONE);
    CHECK(pl_nav_tap(PL_BAR_MAIN, 300, 20, 300 - PL_NAV_DRAG_PX - 1, 20) == PL_NAV_NONE);
    CHECK(pl_nav_drag(PL_BAR_MAIN, 260, 20, 260 + PL_NAV_DRAG_PX + 1));
    CHECK(!pl_nav_drag(PL_BAR_MAIN, 260, 20, 262));
    CHECK(!pl_nav_drag(PL_BAR_MAIN, 360, 20, 420)); // с режима темп не тянется
    CHECK(!pl_nav_drag(PL_BAR_MAIN, 260, 60, 320)); // начали не в шапке
}

// Темп на плашке: идём по клоку BSP - его темп, и тянуть нельзя.
TEST(test_tempo)
{
    int ext = -1;
    CHECK(pl_nav_tempo(120, 0, &ext) == 120 && ext == 0); // позиции ещё нет
    CHECK(pl_nav_tempo(120, 120, &ext) == 120 && ext == 0);
    CHECK(pl_nav_tempo(120, 133, &ext) == 133 && ext == 1); // клок BSP
}

TEST(test_zones)
{
    CHECK(pl_nav_zone(0, 1) == PL_ZONE_TOP && pl_nav_zone(39, 1) == PL_ZONE_TOP);
    CHECK(pl_nav_zone(40, 1) == PL_ZONE_TABS && pl_nav_zone(75, 1) == PL_ZONE_TABS);
    CHECK(pl_nav_zone(76, 1) == PL_ZONE_WORK && pl_nav_zone(431, 1) == PL_ZONE_WORK);
    CHECK(pl_nav_zone(432, 1) == PL_ZONE_DOCK && pl_nav_zone(479, 1) == PL_ZONE_DOCK);
    CHECK(pl_nav_zone(50, 0) == PL_ZONE_WORK); // PERF без вкладок
    CHECK(pl_nav_work_y(1) == 76 && pl_nav_work_y(0) == 40);
}

TEST(test_dock_tap)
{
    CHECK(pl_nav_dock_tap(40, 450, 42, 460) == PL_GRP_PERF);
    CHECK(pl_nav_dock_tap(200, 450, 200, 470) == PL_GRP_SYN1);
    CHECK(pl_nav_dock_tap(200, 450, 200, 420) == PL_GRP_SYN1); // палец чуть ушёл вверх
    CHECK(pl_nav_dock_tap(200, 450, 200, 380) == -1);
    CHECK(pl_nav_dock_tap(200, 450, 300, 450) == -1); // съехал на соседнюю
    CHECK(pl_nav_dock_tap(440, 450, 440, 450) == PL_GRP_MOD);
    CHECK(pl_nav_dock_tap(200, 300, 200, 450) == -1); // начали не в доке
}

TEST(test_tab_tap)
{
    CHECK(pl_nav_tab_tap(PL_GRP_SYN1, 10, 60, 12, 60) == 0);
    CHECK(pl_nav_tab_tap(PL_GRP_SYN1, 390, 60, 390, 60) == 3);
    CHECK(pl_nav_tab_tap(PL_GRP_SYN1, 410, 60, 410, 60) == -1); // правее вкладок - действие
    CHECK(pl_nav_tab_tap(PL_GRP_SYN1, 10, 60, 10, 100) == -1);
    CHECK(pl_nav_tab_tap(PL_GRP_DRUMS, 470, 60, 470, 60) == 6);
    CHECK(pl_nav_tab_tap(PL_GRP_PERF, 10, 60, 10, 60) == -1); // у PERF вкладок нет
    CHECK(pl_nav_tab_tap(PL_GRP_SYN1, 10, 20, 10, 60) == -1);
}

// Действия страницы правее вкладок: поровну, под палец, у DRUMS места нет.
TEST(test_actions)
{
    int x0, x1;
    for (int n = 1; n <= 2; n++) {
        for (int i = 0; i < n; i++) {
            pl_nav_act_rect(PL_GRP_FX, n, i, &x0, &x1);
            CHECK(x0 >= pl_ia_tabs_width(PL_GRP_FX) && x1 <= 480 && x1 - x0 >= 44);
            CHECK(pl_nav_act_tap(PL_GRP_FX, n, (x0 + x1) / 2, 58, (x0 + x1) / 2, 60) == i);
        }
    }
    pl_nav_act_rect(PL_GRP_SYN1, 1, 0, &x0, &x1);
    CHECK(x0 >= 400 && x1 - x0 >= 44);
    pl_nav_act_rect(PL_GRP_DRUMS, 1, 0, &x0, &x1);
    CHECK(x0 == x1);
    CHECK(pl_nav_act_tap(PL_GRP_DRUMS, 1, 470, 58, 470, 58) == -1);
    CHECK(pl_nav_act_tap(PL_GRP_FX, 1, 100, 58, 100, 58) == -1); // по вкладке - не действие
    CHECK(pl_nav_act_tap(PL_GRP_FX, 0, 470, 58, 470, 58) == -1);
}

// Режим списком: без BSP четыре, с BSP два; сетка 2 столбца, клетка попадается своей серединой.
TEST(test_modes)
{
    int mode[4];
    CHECK(pl_nav_modes(0, mode) == 4);
    CHECK(mode[0] == PL_DEMO_METAL && mode[1] == PL_DEMO_ACID && mode[2] == PL_DEMO_DRONE && mode[3] == PL_DEMO_SEQ);
    CHECK(pl_nav_modes(1, mode) == 2);
    CHECK(mode[0] == PL_MODE_LIVE && mode[1] == PL_DEMO_DRONE);
    for (int live = 0; live < 2; live++) {
        int n = pl_nav_modes(live, mode);
        for (int i = 0; i < n; i++) {
            CHECK(pl_mode_ok(live, mode[i]));
            int x, y, w, h;
            pl_nav_mode_rect(n, i, &x, &y, &w, &h);
            CHECK(x >= 0 && y >= 40 && x + w <= 480 && y + h <= 480 && w >= 100 && h >= 60);
            CHECK(pl_nav_mode_at(n, x + w / 2, y + h / 2) == i);
        }
    }
    CHECK(pl_nav_mode_at(4, 240, 20) == -1);
    CHECK(pl_nav_mode_at(4, 2, 200) == -1);
    CHECK(!strcmp(pl_nav_mode_name(1, PL_MODE_LIVE), "LIVE") && !strcmp(pl_nav_mode_name(0, PL_DEMO_METAL), "METAL"));
    CHECK(!strcmp(pl_nav_mode_name(0, PL_DEMO_SEQ), "SEQ") && !strcmp(pl_nav_mode_name(1, PL_DEMO_DRONE), "DRONE"));
}

// Выбор режима до снимка: поле demo модели как его пришлёт синт (в live - песня, за дроном - PL_DEMO_DRONE).
TEST(test_mode_picked_demo)
{
    CHECK(pl_nav_mode_demo(0, PL_DEMO_METAL, PL_DEMO_SEQ) == PL_DEMO_SEQ);
    CHECK(pl_nav_mode_demo(1, PL_DEMO_ACID, PL_DEMO_DRONE) == PL_DEMO_DRONE);
    CHECK(pl_nav_mode_demo(1, PL_DEMO_ACID, PL_MODE_LIVE) == PL_DEMO_ACID); // песня в live не меняется
    CHECK(pl_nav_mode_demo(1, PL_DEMO_DRONE, PL_MODE_LIVE) != PL_DEMO_DRONE);
    CHECK(pl_nav_mode_demo(1, PL_DEMO_DRONE, PL_MODE_LIVE) <= PL_DEMO_SEQ);
}

int main(void)
{
    RUN(test_main_bar);
    RUN(test_overlay_bar);
    RUN(test_buttons_fit_and_dont_overlap);
    RUN(test_tap_needs_same_button);
    RUN(test_play_drag_is_not_tap);
    RUN(test_tempo);
    RUN(test_zones);
    RUN(test_dock_tap);
    RUN(test_tab_tap);
    RUN(test_actions);
    RUN(test_modes);
    RUN(test_mode_picked_demo);
    TEST_MAIN_END();
}
