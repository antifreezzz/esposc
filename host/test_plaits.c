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

TEST(test_plaits_pitch)
{
    // FM 2-op без модуляции (timbre 0) - чистый синус на высоте ноты.
    acid_plaits_init(p);
    acid_plaits_params_t pp = params(10);
    pp.timbre = 0.0f;
    render(&pp, buf, 48000 + 4800);
    int zc = 0;
    for (int i = 4801; i < 48000 + 4800; i++) {
        zc += (buf[i - 1] < 0.0f) != (buf[i] < 0.0f);
    }
    CHECK_NEAR(zc / 2.0f, 110.0f, 2.0f);
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

int main(void)
{
    p = acid_plaits_create();
    RUN(test_plaits_all_models_sound);
    RUN(test_plaits_pitch);
    RUN(test_plaits_gate_off_silences);
    RUN(test_plaits_dx7_retrigger_smooth);
    TEST_MAIN_END();
}
