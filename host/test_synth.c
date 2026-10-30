#include "acid.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f
#define PEAK_6DB 0.5012f

static acid_synth_t s;

typedef struct {
    int finite;
    float peak;
    double sum_sq;
    long n;
} stats_t;

static stats_t render_stats(float seconds)
{
    stats_t st = {1, 0.0f, 0.0, 0};
    float buf[480];
    long total = (long)(seconds * SR);
    while (st.n < total) {
        acid_synth_render(&s, buf, 480);
        for (int i = 0; i < 480; i++) {
            if (!isfinite(buf[i])) {
                st.finite = 0;
            }
            float a = fabsf(buf[i]);
            st.peak = a > st.peak ? a : st.peak;
            st.sum_sq += (double)buf[i] * buf[i];
        }
        st.n += 480;
    }
    return st;
}

TEST(test_synth_finite_and_peak)
{
    acid_synth_init(&s, SR);
    stats_t st = render_stats(60.0f);
    CHECK(st.finite);
    CHECK(st.peak <= PEAK_6DB);
    CHECK(sqrt(st.sum_sq / st.n) > 0.02);
}

TEST(test_synth_stress_peak)
{
    static acid_step_t loud[16];
    for (int i = 0; i < 16; i++) {
        loud[i].note = 36;
        loud[i].flags = ACID_ACCENT;
    }
    acid_synth_init(&s, SR);
    acid_synth_set_pattern(&s, loud, 16);
    s.slot[0].res = 1.0f;
    stats_t st = render_stats(30.0f);
    CHECK(st.finite);
    CHECK(st.peak <= PEAK_6DB);
}

// Рендер по одному сэмплу до момента offset сэмплов внутри шага step.
static void render_until(int step, double offset)
{
    float x;
    for (long guard = 0; guard < 10L * 48000; guard++) {
        acid_synth_render(&s, &x, 1);
        if (s.seq.index == step && s.seq.pos >= offset) {
            return;
        }
    }
}

TEST(test_synth_pitch_follows_step)
{
    static const acid_step_t pat[2] = {{45, 0}, {57, 0}};
    acid_synth_init(&s, SR);
    acid_synth_set_pattern(&s, pat, 2);
    double half = s.seq.samples_per_step / 2;
    render_until(0, half);
    CHECK_NEAR(acid_synth_pitch_hz(&s), 110.0, 1.1);
    render_until(1, half);
    CHECK_NEAR(acid_synth_pitch_hz(&s), 220.0, 2.2);
}

TEST(test_synth_slide_glides)
{
    static const acid_step_t pat[2] = {{45, ACID_SLIDE}, {57, 0}};
    acid_synth_init(&s, SR);
    acid_synth_set_pattern(&s, pat, 2);
    render_until(0, 2000);
    float min_amp = 1.0f;
    float x;
    while (!(s.seq.index == 1 && s.seq.pos >= 480)) {
        acid_synth_render(&s, &x, 1);
        min_amp = s.slot[0].amp.value < min_amp ? s.slot[0].amp.value : min_amp;
    }
    float hz = acid_synth_pitch_hz(&s);
    CHECK(hz > 111.0f);
    CHECK(hz < 219.0f);
    CHECK(min_amp > 0.5f);
}

TEST(test_synth_slide_into_rest)
{
    static const acid_step_t pat[2] = {{45, ACID_SLIDE}, {0, ACID_REST}};
    acid_synth_init(&s, SR);
    acid_synth_set_pattern(&s, pat, 2);
    s.kick_on = 0; // проверяем бас, бочка тут мешает
    render_until(1, s.seq.samples_per_step / 2);
    double sum_sq = 0.0;
    int n = 0;
    float x;
    while (s.seq.index == 1) {
        acid_synth_render(&s, &x, 1);
        sum_sq += (double)x * x;
        n++;
    }
    CHECK(n > 1000);
    CHECK(sqrt(sum_sq / n) < 0.001);
}

TEST(test_synth_accent_on_slid_note)
{
    // Шаг 0 слайдом в акцентированный шаг 1: акцент громкости должен сработать.
    static const acid_step_t pat[3] = {{45, ACID_SLIDE}, {57, ACID_ACCENT}, {45, 0}};
    acid_synth_init(&s, SR);
    acid_synth_set_pattern(&s, pat, 3);
    s.kick_on = 0;
    render_until(1, 100);
    CHECK(s.slot[0].gliding);
    CHECK(s.slot[0].accent_gain > 1.0f);
}

TEST(test_synth_slid_note_without_accent_drops_accent)
{
    // Акцентированный шаг слайдом в обычный: акцент не тянется дальше.
    static const acid_step_t pat[3] = {{45, ACID_ACCENT | ACID_SLIDE}, {57, 0}, {45, 0}};
    acid_synth_init(&s, SR);
    acid_synth_set_pattern(&s, pat, 3);
    s.kick_on = 0;
    render_until(1, 100);
    CHECK(s.slot[0].gliding);
    CHECK_NEAR(s.slot[0].accent_gain, 1.0f, 1e-6);
}

TEST(test_pcm_convert)
{
    const float in[4] = {0.0f, 0.5f, -1.0f, 2.0f};
    int16_t out[8];
    acid_pcm_to_s16_stereo(in, out, 4);
    const int16_t expect[8] = {0, 0, 16384, 16384, -32767, -32767, 32767, 32767};
    for (int i = 0; i < 8; i++) {
        CHECK(out[i] == expect[i]);
    }
}

TEST(test_pcm_lr)
{
    const float l[3] = {0.5f, 0.0f, 2.0f};
    const float r[3] = {-2.0f, 0.25f, -0.5f};
    int16_t out[6];
    acid_pcm_lr_to_s16(l, r, out, 3);
    const int16_t expect[6] = {16384, -32767, 0, 8192, 32767, -16384};
    for (int i = 0; i < 6; i++) {
        CHECK(out[i] == expect[i]);
    }
}

// Автопилот в стерео: оба канала равны и совпадают с моно-рендером бит в бит.
static acid_synth_t s_lr;
static float lr_l[48000], lr_r[48000], lr_m[48000];

TEST(test_autopilot_lr_equal)
{
    acid_synth_init(&s, 48000.0f);
    acid_synth_init(&s_lr, 48000.0f);
    acid_synth_render(&s, lr_m, 48000);
    acid_synth_render_lr(&s_lr, lr_l, lr_r, 48000);
    int diff = 0;
    for (int i = 0; i < 48000; i++) {
        diff += lr_l[i] != lr_r[i] || lr_l[i] != lr_m[i];
    }
    CHECK(diff == 0);
}

int main(void)
{
    RUN(test_pcm_lr);
    RUN(test_autopilot_lr_equal);
    RUN(test_synth_finite_and_peak);
    RUN(test_synth_stress_peak);
    RUN(test_synth_pitch_follows_step);
    RUN(test_synth_slide_glides);
    RUN(test_synth_slide_into_rest);
    RUN(test_synth_accent_on_slid_note);
    RUN(test_synth_slid_note_without_accent_drops_accent);
    RUN(test_pcm_convert);
    TEST_MAIN_END();
}
