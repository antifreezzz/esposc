// Шапка панели: видимые кнопки по виду экрана, тап мимо кнопок ничего не делает.
#include "pl_nav.h"
#include "test.h"

TEST(test_page_bar)
{
    CHECK(pl_nav_at(PL_BAR_PAGE, 20) == PL_NAV_MENU);
    CHECK(pl_nav_at(PL_BAR_PAGE, 110) == PL_NAV_PREV);
    CHECK(pl_nav_at(PL_BAR_PAGE, 320) == PL_NAV_NEXT);
    CHECK(pl_nav_at(PL_BAR_PAGE, 440) == PL_NAV_ACTION);
    CHECK(pl_nav_at(PL_BAR_PAGE, 220) == PL_NAV_NONE); // имя страницы - не кнопка: раньше уводило в меню
    CHECK(pl_nav_at(PL_BAR_PAGE, 380) == PL_NAV_NONE); // XY только у своих страниц
    CHECK(pl_nav_at(PL_BAR_USER, 380) == PL_NAV_XY);
    CHECK(pl_nav_at(PL_BAR_USER, 440) == PL_NAV_ACTION);
}

TEST(test_menu_bar)
{
    CHECK(pl_nav_at(PL_BAR_MENU, 180) == PL_NAV_TAB_PAGES);
    CHECK(pl_nav_at(PL_BAR_MENU, 300) == PL_NAV_TAB_USER);
    CHECK(pl_nav_at(PL_BAR_MENU, 460) == PL_NAV_STATUS);
    CHECK(pl_nav_at(PL_BAR_MENU, 20) == PL_NAV_NONE); // ESPOSC - надпись
}

TEST(test_screen_bar)
{
    CHECK(pl_nav_at(PL_BAR_SCREEN, 20) == PL_NAV_MENU);
    CHECK(pl_nav_at(PL_BAR_SCREEN, 240) == PL_NAV_NONE);
    CHECK(pl_nav_at(PL_BAR_SCREEN, 440) == PL_NAV_ACTION);
}

TEST(test_buttons_fit_and_dont_overlap)
{
    for (int bar = 0; bar < PL_BARS; bar++) {
        int last = -1;
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
        for (int x = t0; x < t1; x++) {
            CHECK(pl_nav_at(bar, x) == PL_NAV_NONE); // заголовок не пересекает кнопки
        }
    }
    int x0, x1;
    pl_nav_rect(PL_BAR_PAGE, PL_NAV_XY, &x0, &x1);
    CHECK(x0 == x1); // нет кнопки - пустой прямоугольник
    CHECK(pl_nav_at(PL_BAR_PAGE, -5) == PL_NAV_NONE && pl_nav_at(PL_BAR_PAGE, 480) == PL_NAV_NONE);
}

TEST(test_tap_needs_same_button)
{
    CHECK(pl_nav_tap(PL_BAR_PAGE, 110, 10, 115, 30) == PL_NAV_PREV);
    CHECK(pl_nav_tap(PL_BAR_PAGE, 110, 10, 112, 52) == PL_NAV_PREV); // палец чуть ушёл вниз
    CHECK(pl_nav_tap(PL_BAR_PAGE, 110, 10, 112, 70) == PL_NAV_NONE); // ушёл в сетку
    CHECK(pl_nav_tap(PL_BAR_PAGE, 110, 10, 320, 10) == PL_NAV_NONE); // съехал на другую кнопку
    CHECK(pl_nav_tap(PL_BAR_PAGE, 110, 60, 110, 10) == PL_NAV_NONE); // начали не в шапке
}

int main(void)
{
    RUN(test_page_bar);
    RUN(test_menu_bar);
    RUN(test_screen_bar);
    RUN(test_buttons_fit_and_dont_overlap);
    RUN(test_tap_needs_same_button);
    TEST_MAIN_END();
}
