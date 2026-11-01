// XY-пэд панели: точка -> значения осей, рамка, троттлинг отправки, HOLD.
#include "pl_xy.h"
#include "test.h"

// Поле 0..400 x 40..440, рамка PL_XY_EDGE.
#define X0 0
#define Y0 40
#define W 400
#define H 400

static pl_xy_t p;
static int out[2];
static int cur[2];

static void reset(void)
{
    pl_xy_init(&p, X0, Y0, W, H);
    cur[0] = 64;
    cur[1] = 64;
}

static int touch(int x, int y, uint32_t ms) { return pl_xy_touch(&p, x, y, cur, ms, out); }

TEST(test_corners_and_edge)
{
    reset();
    CHECK(pl_xy_value(&p, 0, X0) == 0);
    CHECK(pl_xy_value(&p, 0, X0 + PL_XY_EDGE) == 0);
    CHECK(pl_xy_value(&p, 0, X0 + W - 1) == 127);
    CHECK(pl_xy_value(&p, 0, X0 + W - PL_XY_EDGE) == 127);
    CHECK(pl_xy_value(&p, 0, X0 + W / 2) == 64);
    // Y снизу вверх
    CHECK(pl_xy_value(&p, 1, Y0 + H - 1) == 0);
    CHECK(pl_xy_value(&p, 1, Y0) == 127);
    CHECK(pl_xy_value(&p, 0, -50) == 0 && pl_xy_value(&p, 0, 999) == 127);
}

TEST(test_point_inverse)
{
    reset();
    for (int v = 0; v <= 127; v++) {
        int x, y;
        pl_xy_point(&p, v, v, &x, &y);
        CHECK(pl_xy_value(&p, 0, x) == v);
        CHECK(pl_xy_value(&p, 1, y) == v);
    }
}

TEST(test_inside)
{
    reset();
    CHECK(pl_xy_inside(&p, X0 + 10, Y0 + 10));
    CHECK(!pl_xy_inside(&p, X0 + 10, Y0 - 1));
    CHECK(!pl_xy_inside(&p, X0 + W, Y0 + 10));
}

TEST(test_first_touch_sends_both)
{
    reset();
    CHECK(touch(X0 + W - 1, Y0, 100) == 3);
    CHECK(out[0] == 127 && out[1] == 127);
}

TEST(test_throttle_and_flush)
{
    reset();
    touch(X0 + W / 2, Y0 + H / 2, 100);
    CHECK(touch(X0 + W - 1, Y0 + H / 2, 105) == 0); // рано
    CHECK(touch(X0 + W - 1, Y0 + H / 2, 110) == 1 && out[0] == 127); // догнали без движения пальца
    CHECK(touch(X0 + W - 1, Y0 + H / 2, 130) == 0); // не менялось
}

TEST(test_empty_axis_not_sent)
{
    reset();
    cur[1] = -1;
    CHECK(touch(X0, Y0, 100) == 1 && out[0] == 0);
    CHECK(pl_xy_release(&p, out) == 1 && out[0] == 64);
}

TEST(test_release_returns_without_hold)
{
    reset();
    cur[0] = 20;
    cur[1] = 90;
    touch(X0 + W - 1, Y0 + H - 1, 100);
    cur[0] = 127; // синт отразил
    cur[1] = 0;
    CHECK(pl_xy_release(&p, out) == 3);
    CHECK(out[0] == 20 && out[1] == 90);
    CHECK(touch(X0, Y0, 200) == 3); // новое касание - новые стартовые
    CHECK(pl_xy_release(&p, out) == 3 && out[0] == 127 && out[1] == 0);
}

TEST(test_release_with_hold_flushes_last)
{
    reset();
    p.hold = 1;
    touch(X0 + W / 2, Y0 + H / 2, 100);
    touch(X0, Y0 + H / 2, 103); // троттлинг съел
    CHECK(pl_xy_release(&p, out) == 1 && out[0] == 0); // последнее положение пальца не теряем
    CHECK(pl_xy_release(&p, out) == 0); // второй раз - нечего
}

TEST(test_release_without_touch)
{
    reset();
    CHECK(pl_xy_release(&p, out) == 0);
}

int main(void)
{
    RUN(test_corners_and_edge);
    RUN(test_point_inverse);
    RUN(test_inside);
    RUN(test_first_touch_sends_both);
    RUN(test_throttle_and_flush);
    RUN(test_empty_axis_not_sent);
    RUN(test_release_returns_without_hold);
    RUN(test_release_with_hold_flushes_last);
    RUN(test_release_without_touch);
    TEST_MAIN_END();
}
