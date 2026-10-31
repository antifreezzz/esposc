// Зеркало ручек для панели: сырые значения 0..127 всех страниц и правка любой страницы без смены страницы BSP.
#include "acid.h"
#include "test.h"

#include <math.h>
#include <string.h>

#define SR 48000.0f
#define N 96

static acid_synth_t a, b;
static acid_synth_t fresh_b;

static void send(acid_synth_t *s, int type, int ch, int d1, int d2)
{
    acid_midi_event_t e = {.type = (uint8_t)type, .ch = (uint8_t)ch, .d1 = (uint8_t)d1, .d2 = (uint8_t)d2};
    acid_synth_midi(s, &e);
}

// enc - номер энкодера BSP 1..16
static void cc(acid_synth_t *s, int enc, int v) { send(s, ACID_MIDI_CC, 0, acid_bsp_enc_cc[enc - 1], v); }

// Одинаковая партия на обоих синтах: ноты синтов и ударки каждые 125 мс, 2 с; rms разницы стерео.
static double play_diff(acid_synth_t *x, acid_synth_t *y)
{
    static const int drums[] = {36, 38, 39, 42, 46, 37, 41, 45, 56, 44};
    float la[N], ra[N], lb[N], rb[N];
    double sum = 0.0;
    long n = 0;
    for (int blk = 0; blk < (int)(2 * SR) / N; blk++) {
        if (blk % 62 == 0) {
            int step = blk / 62;
            acid_synth_t *both[2] = {x, y};
            for (int k = 0; k < 2; k++) {
                send(both[k], ACID_MIDI_NOTE_ON, 1, drums[step % 10], 110);
                send(both[k], ACID_MIDI_NOTE_ON, 4, 36 + step % 12, step % 3 ? 90 : 120);
                send(both[k], ACID_MIDI_NOTE_ON, 5, 60 + step % 7, 100);
            }
        }
        acid_synth_render_lr(x, la, ra, N);
        acid_synth_render_lr(y, lb, rb, N);
        for (int i = 0; i < N; i++) {
            sum += (double)(la[i] - lb[i]) * (la[i] - lb[i]) + (double)(ra[i] - rb[i]) * (ra[i] - rb[i]);
        }
        n += 2 * N;
    }
    return sqrt(sum / n);
}

TEST(test_set_knob_keeps_page)
{
    acid_synth_init(&a, SR);
    acid_synth_set_knob(&a, 0, 1, 30);
    CHECK(a.page == 6);
    CHECK(acid_synth_knob(&a, 0, 1) == 30);
}

TEST(test_set_knob_equals_cc)
{
    acid_synth_init(&a, SR);
    acid_synth_init(&b, SR);
    cc(&a, 1, 8);
    cc(&a, 4, 100);
    cc(&a, 1, 6);
    acid_synth_set_knob(&b, 8, 3, 100);
    CHECK(b.page == 6);
    CHECK(play_diff(&a, &b) == 0.0);
}

// Значения вне списка синт игнорирует: зеркало остаётся прежним.
static int ignored(int page, int enc, int v)
{
    if (page >= 3 && page <= 5) {
        return (enc == 2 && v > 4) || (page > 3 && enc >= 15); // у рима и ковбелла 15-16 свободны
    }
    if (page == 8) {
        return (enc == 7 || enc == 10) ? v > 4 : enc == 11 ? v > 2 : 0;
    }
    if (page == 9) {
        static const int hi[8] = {0, 16, 8, 2, 2, 2, 7, 11}; // по индексу энкодера с 0
        int slot = enc - 1;
        return slot < 8 ? v > hi[slot] : (v < 52 || v > 76);
    }
    return 0;
}

TEST(test_mirror_follows_cc)
{
    for (int page = 0; page < 10; page++) {
        if (page == 6 || page == 7) {
            continue; // синты - отдельный тест: значение зависит от типа
        }
        acid_synth_init(&a, SR);
        cc(&a, 1, page);
        for (int enc = 2; enc <= 16; enc++) {
            int before = acid_synth_knob(&a, page, enc - 1);
            cc(&a, enc, 77);
            int want = ignored(page, enc, 77) ? before : 77;
            if (acid_synth_knob(&a, page, enc - 1) != want) {
                printf("  page %d enc %d: %d, want %d\n", page, enc, acid_synth_knob(&a, page, enc - 1), want);
            }
            CHECK(acid_synth_knob(&a, page, enc - 1) == want);
        }
    }
}

TEST(test_mirror_synth_type)
{
    acid_synth_init(&a, SR);
    cc(&a, 1, 6);
    cc(&a, 2, 0);
    cc(&a, 3, 10);
    cc(&a, 2, 11);
    CHECK(acid_synth_knob(&a, 6, 1) == 11);
    cc(&a, 3, 90);
    CHECK(acid_synth_knob(&a, 6, 2) == 90);
    cc(&a, 2, 0);
    CHECK(acid_synth_knob(&a, 6, 1) == 0);
    CHECK(acid_synth_knob(&a, 6, 2) == 10);
    cc(&a, 10, 30); // 303: transpose 30 вне 52..76, игнор
    CHECK(acid_synth_knob(&a, 6, 9) == 64);
    cc(&a, 8, 5); // wave 5 не волна
    CHECK(acid_synth_knob(&a, 6, 7) == 0);
    CHECK(acid_synth_knob(&a, 7, 1) == 11); // синт 2 по умолчанию FM 2-op
}

TEST(test_defaults_exact)
{
    acid_synth_init(&a, SR);
    static const int want[][3] = {
        {0, 11, 64}, {0, 12, 0}, {0, 13, 64}, {0, 14, 64},
        {1, 13, 80}, {1, 14, 48}, {1, 15, 64},
        {2, 1, 28}, {2, 2, 127}, {2, 3, 64}, {2, 4, 64}, {2, 5, 35}, {2, 6, 127}, {2, 7, 64}, {2, 8, 64},
        {2, 9, 40}, {2, 10, 100},
        {8, 1, 0}, {8, 2, 0}, {8, 3, 0}, {8, 9, 0}, {8, 10, 0}, {8, 11, 127}, {8, 7, 53}, {8, 8, 83},
        {9, 1, 0}, {9, 2, 4}, {9, 3, 0}, {9, 4, 0}, {9, 5, 1}, {9, 6, 0}, {9, 7, 0}, {9, 8, 64}, {9, 15, 64},
        {6, 1, 0}, {6, 13, 0}, {6, 14, 64}, {6, 15, 0}, {6, 9, 64}, {7, 8, 64},
    };
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        int got = acid_synth_knob(&a, want[i][0], want[i][1]);
        if (got != want[i][2]) {
            printf("  page %d enc %d: %d, want %d\n", want[i][0], want[i][1] + 1, got, want[i][2]);
        }
        CHECK(got == want[i][2]);
    }
}

// Параметр после умолчания из зеркала - в полшага CC от исходного (шаг expo по самой широкой шкале ~3%).
#define SAME(field) CHECK_NEAR(fresh_b.field, a.field, 0.02 * fabs((double)a.field) + 0.006)

TEST(test_defaults_match_init)
{
    acid_synth_init(&a, SR);
    acid_synth_init(&fresh_b, SR);
    for (int page = 0; page < 10; page++) {
        for (int enc = 1; enc < 16; enc++) {
            if ((page == 6 || page == 7) && enc == 1) {
                continue;
            }
            acid_synth_set_knob(&fresh_b, page, enc, acid_synth_knob(&a, page, enc));
        }
    }
    SAME(master);
    SAME(drive);
    SAME(slot[0].gain);
    SAME(slot[1].gain);
    SAME(kick_gain);
    SAME(hat_gain);
    SAME(clap_gain);
    SAME(snare_gain);
    SAME(pump.depth);
    SAME(pump.rel_coef);
    SAME(kick_tune);
    SAME(kick_sweep);
    SAME(kick_pitch_s);
    SAME(kick_amp_s);
    SAME(hat_tone);
    SAME(hat_closed_s);
    SAME(hat_open_s);
    SAME(clap_tone);
    SAME(clap_tail_s);
    SAME(snare_tune);
    SAME(snare_snappy);
    SAME(snare_decay_s);
    SAME(slot[0].cut_base);
    SAME(slot[0].res);
    SAME(slot[0].env_hz);
    SAME(slot[0].decay_s);
    SAME(slot[0].accent_amt);
    SAME(slot[0].glide_coef);
    SAME(slot[0].transpose);
    SAME(slot[1].pp.harmonics);
    SAME(slot[1].pp.timbre);
    SAME(slot[1].pp.morph);
    SAME(slot[1].pp.decay);
    SAME(slot[1].pp.lpg_colour);
    SAME(slot[1].pp.timbre_env);
    SAME(slot[1].pp.morph_env);
    SAME(slot[1].pp.fm_env);
    SAME(slot[1].p_accent);
    SAME(slot[1].glide_coef);
    SAME(send1);
    SAME(send2);
    SAME(send_drums);
    SAME(dl_time);
    SAME(dl_fb);
    SAME(dl_tone);
    SAME(delay.motion);
    SAME(delay.width);
    SAME(stutter.knob);
    SAME(crush.bits_knob);
    SAME(crush.rate_knob);
    SAME(djf.knob);
    SAME(djf.res_knob);
    SAME(cym.sens);
    SAME(cym.gain[0]);
    SAME(cym.gain[1]);
    SAME(cym.gain[2]);
    for (int k = 0; k < ACID_PANS; k++) {
        SAME(pan_l[k]);
        SAME(pan_r[k]);
    }
}

int main(void)
{
    RUN(test_set_knob_keeps_page);
    RUN(test_set_knob_equals_cc);
    RUN(test_mirror_follows_cc);
    RUN(test_mirror_synth_type);
    RUN(test_defaults_exact);
    RUN(test_defaults_match_init);
    TEST_MAIN_END();
}
