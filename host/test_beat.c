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

int main(void)
{
    RUN(test_start_first_tick_is_step0);
    RUN(test_step_every_6_ticks);
    RUN(test_stop_silent);
    RUN(test_no_start_plays);
    TEST_MAIN_END();
}
