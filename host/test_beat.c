#include "acid_beat.h"
#include "test.h"

TEST(test_start_first_tick_is_step0)
{
    acid_beat_t b;
    acid_beat_init(&b);
    acid_beat_start(&b);
    CHECK(acid_beat_tick(&b) == 1);
    CHECK(b.step == 0);
    CHECK(b.bar == 0);
}

TEST(test_step_every_6_ticks)
{
    acid_beat_t b;
    acid_beat_init(&b);
    acid_beat_start(&b);
    int steps = 0;
    for (int t = 0; t < 96; t++) {
        int hit = acid_beat_tick(&b);
        if (t % 6 == 0) {
            CHECK(hit == 1);
            CHECK(b.step == t / 6);
            CHECK(b.bar == 0);
            steps++;
        } else {
            CHECK(hit == 0);
        }
    }
    CHECK(steps == 16);
    CHECK(acid_beat_tick(&b) == 1);
    CHECK(b.step == 0);
    CHECK(b.bar == 1);
}

TEST(test_stop_silent)
{
    acid_beat_t b;
    acid_beat_init(&b);
    acid_beat_start(&b);
    for (int t = 0; t < 15; t++) { // шаги 0, 1, 2 (тики 0..14: 3 тика внутри шага 2)
        acid_beat_tick(&b);
    }
    CHECK(b.step == 2);
    acid_beat_stop(&b);
    for (int t = 0; t < 50; t++) {
        CHECK(acid_beat_tick(&b) == 0);
    }
    acid_beat_continue(&b);
    // До шага 3 оставалось 3 тика.
    CHECK(acid_beat_tick(&b) == 0);
    CHECK(acid_beat_tick(&b) == 0);
    CHECK(acid_beat_tick(&b) == 0);
    CHECK(acid_beat_tick(&b) == 1);
    CHECK(b.step == 3);
    CHECK(b.bar == 0);

    acid_beat_stop(&b);
    CHECK(acid_beat_tick(&b) == 0);
    acid_beat_start(&b);
    CHECK(acid_beat_tick(&b) == 1);
    CHECK(b.step == 0);
    CHECK(b.bar == 0);
}

TEST(test_no_start_plays)
{
    acid_beat_t b;
    acid_beat_init(&b);
    CHECK(acid_beat_tick(&b) == 1);
    CHECK(b.step == 0);
    CHECK(b.bar == 0);
    for (int t = 1; t < 6; t++) {
        CHECK(acid_beat_tick(&b) == 0);
    }
    CHECK(acid_beat_tick(&b) == 1);
    CHECK(b.step == 1);
    CHECK(b.bar == 0);
}

// Шаг Grids на этом тике: прямая сетка совпадает с шагами такта, свинг задерживает нечётные шестнадцатые,
// триольная сетка - 12 шагов по 8 тиков.
TEST(test_grid_due)
{
    for (int swing = 0; swing <= 3; swing++) {
        acid_beat_t b;
        acid_beat_init(&b);
        acid_beat_start(&b);
        int seen = 0;
        for (int t = 0; t < 192; t++) {
            int hit = acid_beat_tick(&b);
            int g = acid_beat_grid_due(&b, 0, swing);
            int st = (t % 96) / 6, in = t % 6;
            CHECK(g == (in == ((st & 1) ? swing : 0) ? st : -1));
            if (swing == 0) {
                CHECK((g >= 0) == hit && (g < 0 || g == b.step));
            }
            seen += g >= 0;
        }
        CHECK(seen == 32);
    }
    acid_beat_t b;
    acid_beat_init(&b);
    acid_beat_start(&b);
    for (int t = 0; t < 192; t++) {
        acid_beat_tick(&b);
        int g = acid_beat_grid_due(&b, 1, 3); // свинг в триолях не действует
        CHECK(g == (t % 8 == 0 ? (t % 96) / 8 : -1));
    }
    acid_beat_stop(&b);
    acid_beat_tick(&b);
    CHECK(acid_beat_grid_due(&b, 0, 0) == -1);
    CHECK(acid_beat_grid_due(&b, 1, 0) == -1);
}

int main(void)
{
    RUN(test_grid_due);
    RUN(test_start_first_tick_is_step0);
    RUN(test_step_every_6_ticks);
    RUN(test_stop_silent);
    RUN(test_no_start_plays);
    TEST_MAIN_END();
}
