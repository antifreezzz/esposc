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

int main(void)
{
    RUN(test_sector_clock);
    RUN(test_turn_full_circle);
    RUN(test_turn_wraps_180);
    RUN(test_turn_dead_center);
    RUN(test_turn_clamp_and_back);
    RUN(test_turn_list);
    RUN(test_press_hold);
    RUN(test_press_moved_is_not_hold);
    RUN(test_press_tap);
    RUN(test_press_smear_ends);
    TEST_MAIN_END();
}
