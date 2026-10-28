#include "acid.h"
#include "test.h"

#include <string.h>

TEST(test_section_lookup_and_wrap)
{
    CHECK(acid_arr_section(0) == &acid_track[0]);
    CHECK(acid_arr_section(3) == &acid_track[0]);
    CHECK(acid_arr_section(4) == &acid_track[1]);
    CHECK(acid_arr_section(63) == &acid_track[15]);
    CHECK(acid_arr_section(64) == &acid_track[0]);
    CHECK(acid_arr_section(64 * 3 + 9) == &acid_track[2]);
}

TEST(test_fill_on_fourth_bar)
{
    for (int s = 0; s < ACID_SECTIONS; s++) {
        const acid_section_t *sec = &acid_track[s];
        for (int b = 0; b < 3; b++) {
            CHECK(acid_arr_pattern(s * 4 + b) == sec->pattern);
        }
        CHECK(acid_arr_pattern(s * 4 + 3) == sec->fill_pattern);
    }
}

TEST(test_progress)
{
    CHECK_NEAR(acid_arr_progress(8, 0), 0.0f, 1e-6);
    CHECK_NEAR(acid_arr_progress(11, 15), 63.0f / 64.0f, 1e-6);
    CHECK_NEAR(acid_arr_progress(10, 0), 0.5f, 1e-6);
}

TEST(test_track_shape)
{
    int breaks = 0, clap_sections = 0;
    for (int s = 0; s < ACID_SECTIONS; s++) {
        const acid_section_t *sec = &acid_track[s];
        CHECK(sec->pattern < ACID_PAT_COUNT && sec->fill_pattern < ACID_PAT_COUNT);
        CHECK(sec->cut_from >= 20.0f && sec->cut_to >= 20.0f);
        CHECK(sec->res_from >= 0.0f && sec->res_to <= 1.0f);
        breaks += !(sec->drums & ACID_DR_KICK);
        clap_sections += !!(sec->drums & ACID_DR_CLAP);
    }
    CHECK(breaks >= 2);
    CHECK(clap_sections >= 6);
    // Стык круга: срез конца последней секции близок к началу первой (бесшовно).
    CHECK(acid_track[15].cut_to <= 1.5f * acid_track[0].cut_from);
}

static int count(const acid_step_t *p, int len, uint8_t flag)
{
    int n = 0;
    for (int i = 0; i < len; i++) {
        n += !!(p[i].flags & flag);
    }
    return n;
}

TEST(test_mutate_keeps_notes_and_rests)
{
    for (int pat = 0; pat < ACID_PAT_COUNT; pat++) {
        for (uint32_t seed = 0; seed < 50; seed++) {
            acid_step_t m[16];
            acid_mutate(m, acid_patterns[pat], 16, seed);
            int diff = 0;
            for (int i = 0; i < 16; i++) {
                const acid_step_t *o = &acid_patterns[pat][i];
                CHECK(m[i].note == o->note);
                CHECK((m[i].flags & ACID_REST) == (o->flags & ACID_REST));
                diff += m[i].flags != o->flags;
            }
            CHECK(diff >= 1);
            int acc = count(m, 16, ACID_ACCENT);
            CHECK(acc >= 2 && acc <= 6);
        }
    }
}

TEST(test_mutate_deterministic)
{
    acid_step_t a[16], b[16], c[16];
    acid_mutate(a, acid_patterns[ACID_PAT_A], 16, 7);
    acid_mutate(b, acid_patterns[ACID_PAT_A], 16, 7);
    CHECK(memcmp(a, b, sizeof a) == 0);
    int differs = 0;
    for (uint32_t seed = 8; seed < 12; seed++) {
        acid_mutate(c, acid_patterns[ACID_PAT_A], 16, seed);
        differs += memcmp(a, c, sizeof a) != 0;
    }
    CHECK(differs >= 3);
}

int main(void)
{
    RUN(test_section_lookup_and_wrap);
    RUN(test_fill_on_fourth_bar);
    RUN(test_progress);
    RUN(test_track_shape);
    RUN(test_mutate_keeps_notes_and_rests);
    RUN(test_mutate_deterministic);
    TEST_MAIN_END();
}
