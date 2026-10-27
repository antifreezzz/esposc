#include "acid.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f
#define BPM 130.0f
#define SPS (48000.0 * 60.0 / 130.0 / 4.0)

static acid_step_t plain[16];

static void make_plain(void)
{
    for (int i = 0; i < 16; i++) {
        plain[i].note = 45;
        plain[i].flags = 0;
    }
}

TEST(test_seq_step_timing)
{
    make_plain();
    acid_seq_t s;
    acid_seq_init(&s, plain, 16, BPM, SR);
    CHECK_NEAR(s.samples_per_step, 5538.46, 0.01);
    int expected = 0, seen = 0;
    for (long n = 0; n < (long)(17 * SPS) + 2 && seen < 17; n++) {
        int step = acid_seq_tick(&s);
        if (step >= 0) {
            CHECK(step == expected % 16);
            CHECK_NEAR((double)n, floor(seen * SPS), 1.0);
            expected++;
            seen++;
        }
    }
    CHECK(seen == 17);
}

TEST(test_seq_rest_gate)
{
    make_plain();
    plain[1].flags = ACID_REST;
    acid_seq_t s;
    acid_seq_init(&s, plain, 16, BPM, SR);
    int gate_in_rest = 0;
    for (int n = 0; n < (int)(3 * SPS); n++) {
        acid_seq_tick(&s);
        if (s.index == 1 && acid_seq_gate(&s)) {
            gate_in_rest = 1;
        }
    }
    CHECK(!gate_in_rest);
}

TEST(test_seq_gate_length)
{
    make_plain();
    acid_seq_t s;
    acid_seq_init(&s, plain, 16, BPM, SR);
    int open = 0;
    for (int n = 0; n < (int)SPS; n++) {
        acid_seq_tick(&s);
        open += acid_seq_gate(&s);
    }
    CHECK_NEAR(open, 2769, 2);
}

TEST(test_seq_slide_wraps)
{
    make_plain();
    plain[15].flags = ACID_SLIDE;
    acid_seq_t s;
    acid_seq_init(&s, plain, 16, BPM, SR);
    int closed_in_15 = 0, gate_at_wrap = 0, next_ok = 0;
    for (long n = 0; n < (long)(16 * SPS) + 2; n++) {
        int step = acid_seq_tick(&s);
        if (s.index == 15) {
            closed_in_15 |= !acid_seq_gate(&s);
            next_ok = acid_seq_next_step(&s) == &plain[0];
        }
        if (step == 0 && n > 0) {
            gate_at_wrap = acid_seq_gate(&s);
        }
    }
    CHECK(!closed_in_15);
    CHECK(gate_at_wrap);
    CHECK(next_ok);
}

TEST(test_default_pattern_shape)
{
    int accents = 0, slides = 0, rests = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t f = acid_pattern_default[i].flags;
        accents += !!(f & ACID_ACCENT);
        slides += !!(f & ACID_SLIDE);
        rests += !!(f & ACID_REST);
    }
    CHECK(accents >= 3 && accents <= 5);
    CHECK(slides >= 2 && slides <= 4);
    CHECK(rests >= 2);
    CHECK(acid_pattern_default[15].flags & ACID_SLIDE);
}

int main(void)
{
    RUN(test_seq_step_timing);
    RUN(test_seq_rest_gate);
    RUN(test_seq_gate_length);
    RUN(test_seq_slide_wraps);
    RUN(test_default_pattern_shape);
    TEST_MAIN_END();
}
