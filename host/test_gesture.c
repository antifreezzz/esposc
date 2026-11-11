// Жест панели: сектор под пальцем, вращение вокруг центра, удержание и касание.
#include "pl_gesture.h"
#include "test.h"

#include <math.h>

#define CX 240
#define CY 240

// Точка на окружности радиуса r под углом deg по часовой от 12 часов.
static void at(double deg, double r, int *x, int *y)
{
    double a = deg * M_PI / 180.0;
    *x = CX + (int)lround(r * sin(a));
    *y = CY - (int)lround(r * cos(a));
}

static int sector_at(double deg, double r)
{
    int x, y;
    at(deg, r, &x, &y);
    return pl_sector(x, y);
}

TEST(test_sector_clock)
{
    CHECK(sector_at(0, 200) == 0);
    CHECK(sector_at(90, 200) == 4); // 90° внутри сектора 4 (84..108)
    CHECK(sector_at(359, 200) == 0);
    CHECK(sector_at(13, 200) == 1);
    CHECK(sector_at(347, 200) == 14);
    CHECK(sector_at(168, 60) == 7); // центр сектора 7
    CHECK(pl_sector(CX, CY) == -1);
    CHECK(sector_at(45, 30) == -1); // ближе 40 px к центру
}

// Провести пальцем от from до to (градусы, можно больше 360) шагами по 3°.
static int sweep(pl_turn_t *t, double from, double to, double r)
{
    int x, y, v = 0;
    double step = to > from ? 3 : -3;
    for (double d = from; step > 0 ? d <= to : d >= to; d += step) {
        at(d, r, &x, &y);
        v = pl_turn_move(t, x, y);
    }
    return v;
}

static void begin(pl_turn_t *t, double deg, int v, int lo, int hi)
{
    int x, y;
    at(deg, 180, &x, &y);
    pl_turn_begin(t, x, y, v, lo, hi);
}

TEST(test_turn_full_circle)
{
    pl_turn_t t;
    begin(&t, 0, 0, 0, 127);
    CHECK(sweep(&t, 0, 360, 180) == 127);
    begin(&t, 0, 127, 0, 127);
    CHECK(sweep(&t, 0, -360, 180) == 0);
    begin(&t, 0, 64, 0, 127);
    int v = sweep(&t, 0, 90, 180);
    CHECK(v >= 64 + 31 && v <= 64 + 33); // четверть оборота - четверть хода
}

TEST(test_turn_wraps_180)
{
    pl_turn_t t;
    begin(&t, 170, 64, 0, 127);
    int v = sweep(&t, 170, 190, 180); // через 6 часов (±180° в atan2)
    CHECK(v >= 70 && v <= 72);
    begin(&t, 190, 64, 0, 127);
    v = sweep(&t, 190, 170, 180);
    CHECK(v >= 56 && v <= 58);
}

TEST(test_turn_dead_center)
{
    pl_turn_t t;
    begin(&t, 0, 64, 0, 127);
    pl_turn_move(&t, CX + 5, CY - 30); // заехал к центру
    pl_turn_move(&t, CX - 5, CY + 30); // и через него на другую сторону
    int x, y;
    at(180, 180, &x, &y);
    CHECK(pl_turn_move(&t, x, y) == 64); // выход из центра - новая опора, не прыжок на 180°
    at(200, 180, &x, &y);
    int v = pl_turn_move(&t, x, y);
    CHECK(v >= 70 && v <= 72);
}

TEST(test_turn_clamp_and_back)
{
    pl_turn_t t;
    begin(&t, 0, 120, 0, 127);
    CHECK(sweep(&t, 0, 180, 180) == 127);
    int v = sweep(&t, 180, 170, 180);
    CHECK(v < 127 && v >= 123);
}

TEST(test_turn_list)
{
    pl_turn_t t;
    begin(&t, 0, 0, 0, 4); // 5 значений: 72° на шаг
    CHECK(sweep(&t, 0, 60, 180) == 0);
    CHECK(sweep(&t, 60, 75, 180) == 1);
    CHECK(sweep(&t, 75, 290, 180) == 4);
    begin(&t, 0, 1, 1, 8);
    CHECK(sweep(&t, 0, -100, 180) == 1);
}

// Слой: TYPE 0..26, но 25 (PETRI) слою не бывает - крутилка его перескакивает в обе стороны.
TEST(test_turn_skips_value)
{
    pl_turn_t t;
    begin(&t, 0, 24, 0, 26);
    pl_turn_skip(&t, 25);
    int v = 24, seen25 = 0;
    for (double d = 0; d <= 60; d += 3) {
        int x, y;
        at(d, 180, &x, &y);
        v = pl_turn_move(&t, x, y);
        seen25 |= v == 25;
    }
    CHECK(!seen25 && v == 26);
    begin(&t, 0, 26, 0, 26);
    pl_turn_skip(&t, 25);
    int x, y;
    at(-14, 180, &x, &y); // один шаг назад: 360/27 ~ 13.3°
    CHECK(pl_turn_move(&t, x, y) == 24);
    begin(&t, 0, 24, 0, 25);
    pl_turn_skip(&t, 25); // пропуск на упоре: упор - соседнее значение
    CHECK(sweep(&t, 0, 90, 180) == 24);
}

TEST(test_press_hold)
{
    pl_press_t p = {0};
    CHECK(pl_press_step(&p, 1, 100, 100, 1000) == PL_PRESS_NONE);
    CHECK(pl_press_step(&p, 1, 104, 103, 1030) == PL_PRESS_NONE);
    CHECK(pl_press_step(&p, 1, 105, 103, 1050) == PL_PRESS_HOLD); // 50 мс: мазок отсекает сдвиг, не время
    CHECK(pl_press_step(&p, 1, 200, 103, 1400) == PL_PRESS_NONE); // после захвата двигать можно
    CHECK(pl_press_step(&p, 0, 0, 0, 1500) == PL_PRESS_RELEASE);
    CHECK(pl_press_step(&p, 0, 0, 0, 1600) == PL_PRESS_NONE);
}

TEST(test_press_moved_is_not_hold)
{
    pl_press_t p = {0};
    pl_press_step(&p, 1, 100, 100, 1000);
    pl_press_step(&p, 1, 120, 100, 1020); // мазок: сдвиг раньше, чем истекло удержание
    CHECK(pl_press_step(&p, 1, 120, 100, 1400) == PL_PRESS_NONE); // мазок не захват
    CHECK(pl_press_step(&p, 0, 0, 0, 1500) == PL_PRESS_END);
}

TEST(test_press_tap)
{
    pl_press_t p = {0};
    pl_press_step(&p, 1, 100, 100, 1000);
    CHECK(pl_press_step(&p, 0, 0, 0, 1030) == PL_PRESS_TAP);
    int x, y;
    pl_press_where(&p, &x, &y);
    CHECK(x == 100 && y == 100);
}

TEST(test_press_smear_ends)
{
    // Палец поплыл на 20 px (пятно касания GT911): захвата нет, но отпускание - событие, с точкой конца.
    pl_press_t p = {0};
    pl_press_step(&p, 1, 100, 100, 1000);
    pl_press_step(&p, 1, 120, 104, 1020);
    CHECK(pl_press_step(&p, 1, 121, 104, 1100) == PL_PRESS_NONE);
    CHECK(pl_press_step(&p, 0, 0, 0, 1150) == PL_PRESS_END);
    int x, y;
    pl_press_last(&p, &x, &y);
    CHECK(x == 121 && y == 104);
    pl_press_where(&p, &x, &y);
    CHECK(x == 100 && y == 100);
}

TEST(test_grid_cell)
{
    CHECK(pl_grid_cell(100, 20) == -1);  // шапка
    CHECK(pl_grid_cell(100, 60) == -1);  // вкладки
    CHECK(pl_grid_cell(100, 450) == -1); // док
    CHECK(pl_grid_cell(2, 100) == -1);   // левое поле
    CHECK(pl_grid_cell(476, 100) == -1); // правое поле
    CHECK(pl_grid_cell(60, 100) == 0);   // левый верх: 1-я ручка (энкодер 2)
    CHECK(pl_grid_cell(240, 100) == 1);  // середина верх: 2-я ручка (энкодер 3)
    CHECK(pl_grid_cell(400, 100) == 2);  // правый верх: 3-я ручка (энкодер 4)
    CHECK(pl_grid_cell(60, 170) == 3);   // 2-й ряд, 1-й столбец: 4-я ручка
    CHECK(pl_grid_cell(240, 254) == 7);  // центр (3-й ряд, 2-й столбец): 8-я ручка
    CHECK(pl_grid_cell(400, 420) == 14); // правый низ: 15-я ручка (энкодер 16)
}

// Карточка 150 x 66 с шагом 70 между вкладками и доком; её середина попадает в неё же.
TEST(test_grid_rect)
{
    for (int i = 0; i < 15; i++) {
        int x, y;
        pl_grid_rect(i, &x, &y);
        CHECK(x >= 0 && x + PL_CARD_W <= 480);
        CHECK(y >= PL_IA_TOP_H + PL_IA_TABS_H && y + PL_CARD_H <= 480 - PL_IA_DOCK_H);
        CHECK(pl_grid_cell(x + PL_CARD_W / 2, y + PL_CARD_H / 2) == i);
        CHECK(pl_grid_cell(x, y) == i && pl_grid_cell(x + PL_CARD_W - 1, y + PL_CARD_H - 1) == i);
    }
    int x0, y0, x1, y1;
    pl_grid_rect(0, &x0, &y0);
    pl_grid_rect(4, &x1, &y1);
    CHECK(x1 - x0 == 156 && y1 - y0 == PL_IA_ROW_H);
}

TEST(test_press_tap_includes_long_press)
{
    // Палец дольше PL_HOLD_MS - это захват, но для кнопок и плиток отпускание после него - тоже тап.
    pl_press_t p = {0};
    pl_press_step(&p, 1, 100, 100, 0);
    CHECK(pl_press_step(&p, 1, 100, 100, PL_HOLD_MS + 10) == PL_PRESS_HOLD);
    int ev = pl_press_step(&p, 0, 0, 0, 200);
    CHECK(ev == PL_PRESS_RELEASE && pl_press_tap(ev));
    CHECK(pl_press_tap(PL_PRESS_TAP) && pl_press_tap(PL_PRESS_END));
    CHECK(!pl_press_tap(PL_PRESS_NONE) && !pl_press_tap(PL_PRESS_HOLD));
}

TEST(test_press_began_once)
{
    // Навигация (док, вкладки) срабатывает в момент касания: «началось» - ровно один опрос на касание.
    pl_press_t p = {0};
    CHECK(!pl_press_began(&p));
    pl_press_step(&p, 1, 100, 100, 1000);
    CHECK(pl_press_began(&p));
    int x, y;
    pl_press_last(&p, &x, &y);
    CHECK(x == 100 && y == 100); // в первый опрос последняя точка равна первой
    pl_press_step(&p, 1, 100, 100, 1010);
    CHECK(!pl_press_began(&p));
    pl_press_step(&p, 0, 0, 0, 1020);
    CHECK(!pl_press_began(&p));
    pl_press_step(&p, 0, 0, 0, 1030);
    CHECK(!pl_press_began(&p));
    pl_press_step(&p, 1, 50, 50, 1040);
    CHECK(pl_press_began(&p));
}

int main(void)
{
    RUN(test_sector_clock);
    RUN(test_turn_full_circle);
    RUN(test_turn_wraps_180);
    RUN(test_turn_dead_center);
    RUN(test_turn_clamp_and_back);
    RUN(test_turn_list);
    RUN(test_turn_skips_value);
    RUN(test_press_hold);
    RUN(test_press_moved_is_not_hold);
    RUN(test_press_tap);
    RUN(test_press_smear_ends);
    RUN(test_grid_cell);
    RUN(test_grid_rect);
    RUN(test_press_tap_includes_long_press);
    RUN(test_press_began_once);
    TEST_MAIN_END();
}
