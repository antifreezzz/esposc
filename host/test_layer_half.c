// Слой на половинной частоте (FX 15 LYR HALF): считается каждый второй сэмпл, звучит как полный - та же высота,
// те же огибающие, глайд и смена типа (у 303); переход без щелчка.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "acid.h"
#include "test.h"

#define SR 48000.0f
#define N 24000 // 0.5 с
#define WIN 240 // окно огибающей 5 мс
#define PAGE_FX 8
#define PI_D 3.14159265358979

static acid_synth_t f, h; // f - слой на полной частоте, h - на половинной
static float yf[N], yh[N];

static void init_pair(int type)
{
    acid_synth_init(&f, SR);
    acid_synth_init(&h, SR);
    acid_synth_set_knob(&f, ACID_PAGE_LAYER1, 1, type);
    acid_synth_set_knob(&h, ACID_PAGE_LAYER1, 1, type);
    acid_synth_set_knob(&h, PAGE_FX, 14, 127);
}

static void note(int on, int n)
{
    acid_midi_event_t e = {(uint8_t)(on ? ACID_MIDI_NOTE_ON : ACID_MIDI_NOTE_OFF), 4, (uint8_t)n, 90};
    acid_synth_midi(&f, &e);
    acid_synth_midi(&h, &e);
}

static void render(float *a, float *b, int n)
{
    acid_synth_render_layer(&f, 0, a, (size_t)n);
    acid_synth_render_layer(&h, 0, b, (size_t)n);
}

static float rms(const float *x, int n)
{
    double e = 0.0;
    for (int i = 0; i < n; i++) {
        e += (double)x[i] * x[i];
    }
    return (float)sqrt(e / n);
}

// Амплитуда частоты fr в куске (Гёрцель).
static double amp_at(const float *x, int n, double fr)
{
    double w = 2.0 * PI_D * fr / SR, c = 2.0 * cos(w), s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; i++) {
        double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

static int crossings(const float *x, int n)
{
    int c = 0;
    for (int i = 1; i < n; i++) {
        c += (x[i - 1] < 0.0f) != (x[i] < 0.0f);
    }
    return c;
}

// Первое окно, где громкость ниже доли от пика.
static int tail_win(const float *x, int n, float frac)
{
    float pk = 0.0f;
    for (int w = 0; w + WIN <= n; w += WIN) {
        float r = rms(x + w, WIN);
        pk = r > pk ? r : pk;
    }
    for (int w = 0; w + WIN <= n; w += WIN) {
        if (rms(x + w, WIN) < frac * pk && w > 0) {
            return w / WIN;
        }
    }
    return -1;
}

TEST(test_half_knob_is_switch)
{
    acid_synth_init(&h, SR);
    CHECK(!h.layer_half);
    acid_synth_set_knob(&h, PAGE_FX, 14, 63);
    CHECK(!h.layer_half);
    acid_synth_set_knob(&h, PAGE_FX, 14, 64);
    CHECK(h.layer_half);
    acid_synth_set_knob(&h, PAGE_FX, 14, 0);
    CHECK(!h.layer_half);
    acid_synth_set_knob(&h, PAGE_FX, 14, 127);
    acid_synth_init(&h, SR); // смена режима (демо/живой) начинает с выключенной
    CHECK(!h.layer_half);
}

// 303: та же высота и та же огибающая громкости по окнам 5 мс.
TEST(test_half_303_same_pitch_and_envelope)
{
    init_pair(0);
    note(1, 45);
    render(yf, yh, N / 2);
    note(0, 45);
    render(yf + N / 2, yh + N / 2, N / 2);
    CHECK(h.layer[0].half && !f.layer[0].half);
    int cf = crossings(yf + 2400, 9600), ch = crossings(yh + 2400, 9600);
    CHECK(abs(cf - ch) <= cf / 50 + 2);
    for (int w = 0; w + WIN <= N; w += WIN) {
        float rf = rms(yf + w, WIN), rh = rms(yh + w, WIN);
        if (rf > 1e-3f) {
            CHECK(rh > 0.75f * rf && rh < 1.33f * rf);
        }
    }
    CHECK(tail_win(yf, N, 0.01f) == tail_win(yh, N, 0.01f));
}

// Глайд 303: вторая нота поверх первой едет к цели так же быстро.
TEST(test_half_303_glide)
{
    init_pair(0);
    note(1, 45);
    render(yf, yh, 4800);
    note(1, 57); // легато - скольжение
    render(yf, yh, 1200);
    float pf = f.layer[0].pitch_log2, ph = h.layer[0].pitch_log2;
    CHECK(fabsf(pf - ph) < 0.02f);
    CHECK(fabsf(pf - f.layer[0].target_log2) > 0.02f); // ещё едем, сравнение не вырожденное
}

// Plaits: нота +12 на половинной частоте - основной тон на месте, а не на октаву выше.
TEST(test_half_plaits_pitch)
{
    init_pair(7);
    note(1, 57); // A3, 220 Гц
    render(yf, yh, N);
    double af = amp_at(yf + N / 2, N / 2, 220.0), ah = amp_at(yh + N / 2, N / 2, 220.0);
    CHECK(af > 1e-3);
    CHECK(ah > 0.5 * af && ah < 2.0 * af);
}

// Смена типа на половинной частоте занимает столько же сэмплов.
TEST(test_half_type_switch_same_time)
{
    init_pair(0);
    note(1, 45);
    render(yf, yh, 2400);
    acid_synth_set_knob(&f, ACID_PAGE_LAYER1, 1, 7);
    acid_synth_set_knob(&h, ACID_PAGE_LAYER1, 1, 7);
    int nf = 0, nh = 0;
    while (f.layer[0].type != 7 && nf < 4800) {
        render(yf, yh, 1);
        nf++;
        nh += h.layer[0].type != 7;
    }
    CHECK(f.layer[0].type == 7 && h.layer[0].type == 7);
    CHECK(abs(nf - nh) <= 2);
}

// Включение и выключение посреди ноты - без щелчка (скачок между сэмплами не больше, чем в самом звуке).
TEST(test_half_toggle_no_click)
{
    for (int type = 0; type <= 7; type += 7) {
        init_pair(type);
        acid_synth_set_knob(&h, PAGE_FX, 14, 0);
        note(1, 45);
        render(yf, yh, 4800);
        float base = 0.0f;
        for (int i = 1; i < 4800; i++) {
            base = fabsf(yh[i] - yh[i - 1]) > base ? fabsf(yh[i] - yh[i - 1]) : base;
        }
        float jump = 0.0f, prev = yh[4799];
        for (int k = 0; k < 6; k++) {
            acid_synth_set_knob(&h, PAGE_FX, 14, k % 2 ? 0 : 127);
            render(yf, yh, 1000);
            for (int i = 0; i < 1000; i++) {
                jump = fabsf(yh[i] - prev) > jump ? fabsf(yh[i] - prev) : jump;
                prev = yh[i];
            }
        }
        CHECK(jump < 1.2f * base + 0.01f);
    }
}

int main(void)
{
    RUN(test_half_knob_is_switch);
    RUN(test_half_303_same_pitch_and_envelope);
    RUN(test_half_303_glide);
    RUN(test_half_plaits_pitch);
    RUN(test_half_type_switch_same_time);
    RUN(test_half_toggle_no_click);
    TEST_MAIN_END();
}
