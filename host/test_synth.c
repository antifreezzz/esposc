#include "acid.h"
#include "acid_demo.h"
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

static void accumulate(stats_t *st, const float *x, int n)
{
    for (int i = 0; i < n; i++) {
        st->finite &= isfinite(x[i]);
        float a = fabsf(x[i]);
        st->peak = a > st->peak ? a : st->peak;
        st->sum_sq += (double)x[i] * x[i];
    }
    st->n += n;
}

static void midi(int type, int ch, int d1, int d2)
{
    acid_midi_event_t e = {.type = (uint8_t)type, .ch = (uint8_t)ch, .d1 = (uint8_t)d1, .d2 = (uint8_t)d2};
    acid_synth_midi(&s, &e);
}

TEST(test_synth_demo_finite_and_peak)
{
    static acid_demo_t d;
    static acid_midi_event_t ev[96];
    acid_synth_init(&s, SR);
    acid_demo_init(&d, &acid_demo_song);
    stats_t st = {1, 0.0f, 0.0, 0};
    float l[ACID_DEMO_CHUNK], r[ACID_DEMO_CHUNK];
    for (long k = 0; k < 60L * 48000 / ACID_DEMO_CHUNK; k++) {
        int n = acid_demo_events(&d, ev, 96);
        for (int i = 0; i < n; i++) {
            acid_synth_midi(&s, &ev[i]);
        }
        acid_synth_render_lr(&s, l, r, ACID_DEMO_CHUNK);
        accumulate(&st, l, ACID_DEMO_CHUNK);
        accumulate(&st, r, ACID_DEMO_CHUNK);
    }
    CHECK(st.finite);
    CHECK(st.peak <= PEAK_6DB);
    CHECK(sqrt(st.sum_sq / st.n) > 0.02);
}

TEST(test_synth_stress_peak)
{
    // Акцентированные низкие ноты на максимальном резонансе 303.
    acid_synth_init(&s, SR);
    midi(ACID_MIDI_CC, 1, 74, 0);   // энкодер 2 на странице синта 1: 303
    midi(ACID_MIDI_CC, 1, 76, 127); // энкодер 4: резонанс
    stats_t st = {1, 0.0f, 0.0, 0};
    float buf[480];
    for (int k = 0; k < 3000; k++) {
        if (k % 12 == 0) {
            midi(ACID_MIDI_NOTE_ON, 4, 36, 127);
        } else if (k % 12 == 6) {
            midi(ACID_MIDI_NOTE_OFF, 4, 36, 0);
        }
        acid_synth_render(&s, buf, 480);
        accumulate(&st, buf, 480);
    }
    CHECK(st.finite);
    CHECK(st.peak <= PEAK_6DB);
}

TEST(test_render_mono_is_mean)
{
    static acid_synth_t a;
    static float l[4800], r[4800], m[4800];
    acid_synth_init(&s, SR);
    acid_synth_init(&a, SR);
    acid_midi_event_t e = {.type = ACID_MIDI_NOTE_ON, .ch = 1, .d1 = 42, .d2 = 120}; // хэт правее центра
    acid_synth_midi(&s, &e);
    acid_synth_midi(&a, &e);
    acid_synth_render_lr(&s, l, r, 4800);
    acid_synth_render(&a, m, 4800);
    int same = 1;
    for (int i = 0; i < 4800; i++) {
        same &= m[i] == 0.5f * (l[i] + r[i]);
    }
    CHECK(same);
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

TEST(test_pcm_rounds_like_lrintf)
{
    // Перевод без libm (на плате lrintf - программный вызов на каждый сэмпл) обязан совпадать с прежним
    // lrintf бит в бит: и на половинках (к чётному), и за краями.
    static float l[4096], r[4096];
    static int16_t out[8192];
    uint32_t rng = 1;
    int bad = 0;
    for (int pass = 0; pass < 300; pass++) {
        for (int i = 0; i < 4096; i++) {
            rng = rng * 1664525u + 1013904223u;
            l[i] = (float)(int32_t)rng * (1.1f / 2147483648.0f);          // весь диапазон с запасом
            r[i] = ((float)((int)(rng >> 8) % 65536 - 32768) + 0.5f) / 32767.0f; // около половинок
            if (pass == 0) {
                r[i] = (float)(i - 2048) * (0.5f / 32767.0f) * 16.0f; // и точные половинки после умножения
            }
        }
        acid_pcm_lr_to_s16(l, r, out, 4096);
        for (int i = 0; i < 4096; i++) {
            float a = l[i] > 1.0f ? 1.0f : l[i] < -1.0f ? -1.0f : l[i];
            float b = r[i] > 1.0f ? 1.0f : r[i] < -1.0f ? -1.0f : r[i];
            bad += out[2 * i] != (int16_t)lrintf(a * 32767.0f);
            bad += out[2 * i + 1] != (int16_t)lrintf(b * 32767.0f);
        }
    }
    CHECK(bad == 0);
}

TEST(test_pcm_gain)
{
    // Q15: 16384 = половина, округление к ближайшему, знак сохраняется.
    const int16_t in[5] = {32767, -32768, 3, -3, 0};
    int16_t out[5];
    acid_pcm_gain_s16(in, out, 5, 16384);
    const int16_t expect[5] = {16384, -16384, 2, -2, 0};
    for (int i = 0; i < 5; i++) {
        CHECK(out[i] == expect[i]);
    }
    acid_pcm_gain_s16(in, out, 5, 32768); // единица: без изменений
    for (int i = 0; i < 5; i++) {
        CHECK(out[i] == in[i]);
    }
}

TEST(test_bsp_page_mapping)
{
    // На BSP: 0-9 штатные, 10-15 свои, 16-17 слои, 18 чашка, 19-22 модуляторы.
    static const int bsp[] = {0, 9, 10, 15, 16, 17, 18, 19, 22};
    static const int page[] = {0, 9, 14, 19, 10, 11, 13, 23, 26};
    for (int i = 0; i < 9; i++) {
        CHECK(acid_page_from_bsp(bsp[i]) == page[i]);
        CHECK(acid_page_to_bsp(page[i]) == bsp[i]);
    }
    CHECK(acid_page_from_bsp(23) == -1);
    CHECK(acid_page_from_bsp(127) == -1);
    CHECK(acid_page_from_bsp(-1) == -1);
    CHECK(acid_page_to_bsp(20) == -1);
    CHECK(acid_page_to_bsp(27) == -1);
    CHECK(acid_page_to_bsp(ACID_PAGE_GRIDS) == -1);
}

int main(void)
{
    RUN(test_bsp_page_mapping);
    RUN(test_synth_demo_finite_and_peak);
    RUN(test_synth_stress_peak);
    RUN(test_render_mono_is_mean);
    RUN(test_pcm_convert);
    RUN(test_pcm_rounds_like_lrintf);
    RUN(test_pcm_lr);
    RUN(test_pcm_gain);
    TEST_MAIN_END();
}
