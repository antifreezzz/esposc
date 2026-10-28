#include "acid.h"
#include "test.h"

#include <math.h>
#include <string.h>

#define SR 48000.0f

static acid_synth_t s;

// Рендер по сэмплу, пока не дойдём до такта bar, шага step, позиции pos в шаге.
static void render_to(int cycle, int bar, int step, float pos)
{
    float x;
    for (long guard = 0; guard < 200L * 48000 * 4; guard++) {
        if (s.cycle == cycle && s.bar == bar && s.seq.index == step && s.seq.pos >= pos) {
            return;
        }
        acid_synth_render(&s, &x, 1);
    }
}

static float max_kick_env(int bars)
{
    float x, worst = 0.0f;
    int end_bar = s.bar + bars;
    while (s.bar < end_bar) {
        acid_synth_render(&s, &x, 1);
        worst = s.kick.amp_env > worst ? s.kick.amp_env : worst;
    }
    return worst;
}

// Максимум огибающих бочки и клэпа на bars тактов вперёд.
static void max_drum_env(int bars, float *kick, float *clap)
{
    float x;
    int end_bar = s.bar + bars;
    *kick = *clap = 0.0f;
    while (s.bar < end_bar) {
        acid_synth_render(&s, &x, 1);
        if (s.bar >= end_bar) {
            break; // этот сэмпл уже первый в следующем такте
        }
        *kick = s.kick.amp_env > *kick ? s.kick.amp_env : *kick;
        *clap = s.clap.env > *clap ? s.clap.env : *clap;
    }
}

TEST(test_track_break_has_no_kick)
{
    acid_synth_init(&s, SR);
    render_to(0, 40, 0, 0.0f);    // секция 11
    render_to(0, 40, 4, 0.0f);    // хвост бочки с такта 39 успел затухнуть
    float kick, clap;
    max_drum_env(8, &kick, &clap); // такты 40..47 целиком (брейк, включая филл на 47)
    CHECK(kick < 0.01f);
    CHECK(clap < 0.01f);
    CHECK(s.bar == 48);
    CHECK(max_kick_env(4) > 0.9f);  // 48..51 (дроп)
}

TEST(test_track_transpose)
{
    acid_synth_init(&s, SR);
    // Секция 6 (такты 20-23): транспоз +5, паттерн A, шаг 1 = нота 45 без слайда.
    render_to(0, 20, 1, s.seq.samples_per_step / 2);
    float want = acid_midi_to_hz(45 + 5);
    CHECK_NEAR(acid_synth_pitch_hz(&s), want, want * 0.01f);
}

TEST(test_track_loops_and_mutates)
{
    acid_synth_init(&s, SR);
    CHECK(memcmp(s.mut[ACID_PAT_A], acid_patterns[ACID_PAT_A], sizeof s.mut[0]) == 0);
    render_to(0, 63, 15, 0.0f);
    render_to(1, 0, 0, 0.0f);
    CHECK(s.cycle == 1 && s.bar == 0);
    CHECK(memcmp(s.mut[ACID_PAT_A], acid_patterns[ACID_PAT_A], sizeof s.mut[0]) != 0);

    // Шов без щелчка: в первые 5 мс круга скачок не больше атаки бочки.
    float prev, x, jump = 0.0f;
    acid_synth_render(&s, &prev, 1);
    for (int i = 0; i < 240; i++) {
        acid_synth_render(&s, &x, 1);
        jump = fabsf(x - prev) > jump ? fabsf(x - prev) : jump;
        prev = x;
    }
    CHECK(jump < 0.3f);

    // Мутация ровно одна на круг: seed = 1, и до конца круга паттерны не меняются.
    acid_step_t want[ACID_PAT_COUNT][16];
    for (int p = 0; p < ACID_PAT_COUNT; p++) {
        acid_mutate(want[p], acid_patterns[p], 16, 1);
    }
    CHECK(memcmp(s.mut, want, sizeof want) == 0);
    render_to(1, 63, 15, 0.0f);
    CHECK(memcmp(s.mut, want, sizeof want) == 0);
}

TEST(test_track_long_render)
{
    acid_synth_init(&s, SR);
    static float buf[4800];
    long total = (long)(3 * ACID_TRACK_BARS * 16 * s.seq.samples_per_step);
    int finite = 1;
    float peak = 0.0f;
    double sq = 0.0;
    for (long n = 0; n < total; n += 4800) {
        acid_synth_render(&s, buf, 4800);
        for (int i = 0; i < 4800; i++) {
            finite &= isfinite(buf[i]) != 0;
            peak = fabsf(buf[i]) > peak ? fabsf(buf[i]) : peak;
            sq += (double)buf[i] * buf[i];
        }
    }
    CHECK(finite);
    CHECK(peak <= 0.5012f);
    CHECK(sqrt(sq / total) > 0.03);
    CHECK(s.cycle == 3 || (s.cycle == 2 && s.bar == 63));
}

int main(void)
{
    RUN(test_track_break_has_no_kick);
    RUN(test_track_transpose);
    RUN(test_track_loops_and_mutates);
    RUN(test_track_long_render);
    TEST_MAIN_END();
}
