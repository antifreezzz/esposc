#include "acid_plaits.h"
#include "test.h"

#include <math.h>

#define BLK ACID_PLAITS_BLOCK

static acid_plaits_t *p;
static float buf[48000 * 6];

static acid_plaits_params_t params(int model)
{
    acid_plaits_params_t pp = {
        .model = model, .note = 45.0f, .harmonics = 0.5f, .timbre = 0.5f, .morph = 0.5f,
        .decay = 0.5f, .lpg_colour = 0.5f, .level = 1.0f, .trigger = 1,
    };
    return pp;
}

static void render(const acid_plaits_params_t *pp, float *out, int n)
{
    for (int i = 0; i + BLK <= n; i += BLK) {
        acid_plaits_render(p, pp, out + i, BLK);
    }
}

static float rms(const float *x, int n)
{
    double sq = 0.0;
    for (int i = 0; i < n; i++) {
        sq += (double)x[i] * x[i];
    }
    return (float)sqrt(sq / n);
}

static int all_finite(const float *x, int n)
{
    for (int i = 0; i < n; i++) {
        if (!isfinite(x[i])) {
            return 0;
        }
    }
    return 1;
}

TEST(test_plaits_all_models_sound)
{
    for (int m = 0; m < ACID_PLAITS_MODELS; m++) {
        acid_plaits_init(p);
        acid_plaits_params_t pp = params(m);
        render(&pp, buf, 24000);
        CHECK(all_finite(buf, 24000));
        float r = rms(buf, 24000);
        if (!(r > 0.01f)) {
            printf("    model %d rms %f\n", m, r);
        }
        CHECK(r > 0.01f);
    }
}

// Частота по восходящим нулям с интерполяцией, первые 0.1 с пропускаем.
static double sine_hz(const float *x, int n)
{
    double first = -1.0, last = -1.0;
    int periods = 0;
    for (int i = 4801; i < n; i++) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            double t = (i - 1) - x[i - 1] / (double)(x[i] - x[i - 1]);
            if (first < 0.0) {
                first = t;
            } else {
                periods++;
            }
            last = t;
        }
    }
    return periods / ((last - first) / 48000.0);
}

TEST(test_plaits_pitch)
{
    // FM 2-op без модуляции (timbre 0) - чистый синус на высоте ноты. Строй - как у остальных голосов на 48 кГц:
    // сам Plaits считает от частоты своего кодека 47872 Гц, без поправки он на 4.6 цента выше.
    const float notes[2] = {45.0f, 69.0f};
    for (int k = 0; k < 2; k++) {
        acid_plaits_init(p);
        acid_plaits_params_t pp = params(10);
        pp.timbre = 0.0f;
        pp.note = notes[k];
        render(&pp, buf, 48000 * 4);
        double want = 440.0 * pow(2.0, (notes[k] - 69.0) / 12.0);
        double cents = 1200.0 * log2(sine_hz(buf, 48000 * 4) / want);
        if (!(fabs(cents) < 0.3)) {
            printf("    note %.0f off by %+.2f cents\n", notes[k], cents);
        }
        CHECK(fabs(cents) < 0.3);
    }
}

TEST(test_plaits_gate_off_silences)
{
    // У DX7 (2-4) хвост задаёт патч, резонаторы (19, 20) звенят сами: их не проверяем.
    for (int m = 0; m < ACID_PLAITS_MODELS; m++) {
        if ((m >= 2 && m <= 4) || m == 19 || m == 20) {
            continue;
        }
        acid_plaits_init(p);
        acid_plaits_params_t pp = params(m);
        pp.decay = 0.2f;
        render(&pp, buf, 24000);
        pp.trigger = 0;
        pp.level = 0.0f;
        render(&pp, buf, 96000);
        float r = rms(buf + 48000, 48000);
        if (!(r < 1e-3f)) {
            printf("    model %d tail rms %f\n", m, r);
        }
        CHECK(r < 1e-3f);
    }
}

TEST(test_plaits_dx7_retrigger_smooth)
{
    // Восьмые на 125 bpm, гейт в половину шага: голос DX7 берётся снова, пока звучит хвост.
    const int notes[8] = {36, 36, 48, 36, 39, 36, 43, 46};
    const int step = 48000 * 60 / 125 / 2, n = 48000 * 5;
    acid_plaits_init(p);
    acid_plaits_params_t pp = params(2);
    for (int i = 0; i < n; i += BLK) {
        float t = (float)i / (48000 * 8);
        pp.note = (float)notes[(i / step) % 8];
        pp.timbre = t;
        pp.morph = 1.0f - t;
        pp.trigger = (i % step) < step / 2;
        pp.level = (float)pp.trigger;
        acid_plaits_render(p, &pp, buf + i, BLK);
    }
    float worst = 0.0f;
    for (int i = 1; i < n - 1; i++) {
        float d = fabsf(buf[i + 1] - 2.0f * buf[i] + buf[i - 1]);
        worst = d > worst ? d : worst;
    }
    CHECK(worst < 0.1f);
}

TEST(test_plaits_output_is_float)
{
    // Модуль Plaits резал выход в 16 бит под свой ЦАП. У нас дальше float и перегруз: сетка 1/32768 - лишний
    // шум квантования на тихих хвостах. Выход обязан идти мимо сетки и не выходить за +-1.
    acid_plaits_init(p);
    acid_plaits_params_t pp = params(10);
    render(&pp, buf, 24000);
    int off_grid = 0, clipped = 0;
    for (int i = 0; i < 24000; i++) {
        float k = buf[i] * 32768.0f;
        off_grid += k != floorf(k);
        clipped += fabsf(buf[i]) > 1.0f;
    }
    CHECK(off_grid > 12000);
    CHECK(clipped == 0);
    // Отпущенная нота затухает ниже одного шага 16 бит, а не встаёт на нём.
    pp.trigger = 0;
    pp.level = 0.0f;
    render(&pp, buf, 48000 * 4);
    float tail = 0.0f;
    for (int i = 48000 * 4 - 4800; i < 48000 * 4; i++) {
        tail = fabsf(buf[i]) > tail ? fabsf(buf[i]) : tail;
    }
    CHECK(tail > 0.0f);
    CHECK(tail < 0.5f / 32768.0f);
}

TEST(test_plaits_fast_math_matches_division)
{
    // Плата делит float программно, корень - тоже вызов: в посэмпловых циклах Plaits стоят замены на умножениях.
    // Они обязаны держать точность float на всём рабочем ходе.
    float worst_r = 0.0f, worst_s = 0.0f;
    for (int i = 0; i < 200000; i++) {
        float x = 1e-6f * powf(1e12f, (float)i / 200000.0f); // 1e-6 .. 1e6
        float e = fabsf(acid_plaits_test_rcp(x) * x - 1.0f);
        worst_r = e > worst_r ? e : worst_r;
        e = fabsf(acid_plaits_test_rsqrt(x) * sqrtf(x) - 1.0f);
        worst_s = e > worst_s ? e : worst_s;
    }
    CHECK(worst_r < 4e-7f);
    CHECK(worst_s < 4e-7f);
}

int main(void)
{
    p = acid_plaits_create();
    RUN(test_plaits_all_models_sound);
    RUN(test_plaits_pitch);
    RUN(test_plaits_output_is_float);
    RUN(test_plaits_fast_math_matches_division);
    RUN(test_plaits_gate_off_silences);
    RUN(test_plaits_dx7_retrigger_smooth);
    TEST_MAIN_END();
}
