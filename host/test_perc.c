// Перкуссия синтезом: томы, рим, ковбелл, пять моделей, пул голосов.
#include "acid_perc.h"
#include "test.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SR 48000.0f
#define MS(x) ((int)((x) * 48))
#define FLOOR 0.3f // DRUM_VEL_FLOOR синта

static acid_perc_t p;
static const int NOTE_OF[ACID_PERC_N] = {45, 37, 56};

typedef struct {
    double peak, sum_abs, el, er;
    int finite;
    int crossings; // переходов через ноль в окне [0, cross_to)
} stats_t;

// n сэмплов в буфер (если buf), статистика.
static stats_t run(int n, float *buf, int cross_to)
{
    stats_t st = {0, 0, 0, 0, 1, 0};
    float l[64], r[64];
    float prev = 0.0f;
    for (int i = 0; i < n; i += 64) {
        int m = n - i < 64 ? n - i : 64;
        acid_perc_render(&p, l, r, m);
        for (int j = 0; j < m; j++) {
            float x = 0.5f * (l[j] + r[j]);
            st.finite &= isfinite(l[j]) && isfinite(r[j]);
            double a = fabs(x);
            st.peak = a > st.peak ? a : st.peak;
            st.sum_abs += a;
            st.el += (double)l[j] * l[j];
            st.er += (double)r[j] * r[j];
            if (i + j < cross_to && ((prev < 0.0f) != (x < 0.0f))) {
                st.crossings++;
            }
            prev = x;
            if (buf) {
                buf[i + j] = x;
            }
        }
    }
    return st;
}

static void fresh(void)
{
    acid_perc_init(&p, SR);
}

// Спад ручки v в секундах, как в спеке: ~0.02 .. ~2 с экспонентой.
static float decay_s(int v)
{
    return 0.02f * powf(100.0f, (float)v / 127.0f);
}

TEST(test_perc_silent_until_trigger)
{
    fresh();
    stats_t st = run(4800, NULL, 0);
    CHECK(st.peak == 0.0);
    CHECK(acid_perc_voices(&p) == 0);
    CHECK(acid_perc_inst_of(42, NULL) == -1 && acid_perc_inst_of(36, NULL) == -1);
    int tom = -9;
    CHECK(acid_perc_inst_of(41, &tom) == ACID_PERC_TOMS && tom == 0);
    CHECK(acid_perc_inst_of(50, &tom) == ACID_PERC_TOMS && tom == 5);
    CHECK(acid_perc_inst_of(47, &tom) == ACID_PERC_TOMS && tom == 3);
    CHECK(acid_perc_inst_of(37, &tom) == ACID_PERC_RIM && tom == -1);
    CHECK(acid_perc_inst_of(56, &tom) == ACID_PERC_COW && tom == -1);
}

// Удар v = 1 на ручках по умолчанию, кроме заданных; конечно, слышно, затихает и освобождает слоты.
static int sounds_and_ends(int inst, int model, int knob, int v, const char *what)
{
    fresh();
    acid_perc_set(&p, inst, ACID_PERC_MODEL, model);
    if (knob >= 0) {
        acid_perc_set(&p, inst, knob, v);
    }
    float tail = 10.0f * decay_s(p.inst[inst].knob[ACID_PERC_DECAY]) + 0.2f;
    acid_perc_trigger(&p, NOTE_OF[inst], 1.0f);
    stats_t st = run((int)(tail * SR), NULL, 0);
    int ok = st.finite && st.peak > 1e-3 && st.peak < 4.0 && acid_perc_voices(&p) == 0;
    if (!ok) {
        printf("  inst %d model %d knob %d=%d (%s): finite %d peak %g voices %d\n", inst, model, knob, v, what, st.finite,
               st.peak, acid_perc_voices(&p));
    }
    return ok;
}

TEST(test_perc_models_sound_and_end)
{
    static const int VALS[3] = {0, 64, 127};
    for (int inst = 0; inst < ACID_PERC_N; inst++) {
        for (int m = 0; m < ACID_PERC_MODELS; m++) {
            CHECK(sounds_and_ends(inst, m, -1, 0, "defaults"));
            for (int k = ACID_PERC_TUNE; k < ACID_PERC_KNOBS; k++) {
                if (k == ACID_PERC_LEVEL || k == ACID_PERC_PAN || k == ACID_PERC_SENS) {
                    continue; // громкость 0 тихая по смыслу, панорама и чувствительность при v = 1 не важны
                }
                for (int i = 0; i < 3; i++) {
                    CHECK(sounds_and_ends(inst, m, k, VALS[i], "one knob"));
                }
            }
            for (int all = 0; all <= 127; all += 127) {
                fresh();
                acid_perc_set(&p, inst, ACID_PERC_MODEL, m);
                for (int k = ACID_PERC_TUNE; k < ACID_PERC_KNOBS; k++) {
                    acid_perc_set(&p, inst, k, k == ACID_PERC_LEVEL ? 127 : all);
                }
                acid_perc_trigger(&p, NOTE_OF[inst], 1.0f);
                stats_t st = run((int)((10.0f * decay_s(all) + 0.2f) * SR), NULL, 0);
                CHECK(st.finite && st.peak > 1e-3 && st.peak < 4.0);
                CHECK(acid_perc_voices(&p) == 0);
            }
        }
    }
}

static float buf_a[MS(300)], buf_b[MS(300)];

TEST(test_perc_every_knob_acts)
{
    static const int KN[] = {ACID_PERC_TONE, ACID_PERC_CHAR, ACID_PERC_CLICK, ACID_PERC_DRIVE, ACID_PERC_CRUSH,
                             ACID_PERC_DECAY, ACID_PERC_PDEPTH, ACID_PERC_PTIME};
    for (int m = 0; m < ACID_PERC_MODELS; m++) {
        for (unsigned i = 0; i < sizeof KN / sizeof KN[0]; i++) {
            float *b[2] = {buf_a, buf_b};
            double sum = 0.0;
            for (int j = 0; j < 2; j++) {
                fresh();
                acid_perc_set(&p, ACID_PERC_TOMS, ACID_PERC_MODEL, m);
                if (KN[i] == ACID_PERC_PTIME) {
                    acid_perc_set(&p, ACID_PERC_TOMS, ACID_PERC_PDEPTH, 100); // время высоты слышно при глубине
                }
                acid_perc_set(&p, ACID_PERC_TOMS, KN[i], j ? 127 : 0);
                acid_perc_trigger(&p, 45, 1.0f);
                sum += run(MS(300), b[j], 0).sum_abs;
            }
            double d = 0.0;
            for (int n = 0; n < MS(300); n++) {
                d += fabs(buf_a[n] - buf_b[n]);
            }
            if (!(d > 1e-3 * sum)) {
                printf("  model %d knob %d does nothing\n", m, KN[i]);
            }
            CHECK(d > 1e-3 * sum);
        }
    }
}

static int crossings(int inst, int note, int knob, int v)
{
    fresh();
    acid_perc_set(&p, inst, ACID_PERC_MODEL, ACID_PERC_ANALOG);
    acid_perc_set(&p, inst, ACID_PERC_PDEPTH, 0);
    acid_perc_set(&p, inst, ACID_PERC_CHAR, 0);
    acid_perc_set(&p, inst, ACID_PERC_CLICK, 0);
    acid_perc_set(&p, inst, ACID_PERC_TONE, 0);
    acid_perc_set(&p, inst, ACID_PERC_DECAY, 127);
    if (knob >= 0) {
        acid_perc_set(&p, inst, knob, v);
    }
    acid_perc_trigger(&p, note, 1.0f);
    return run(MS(100), NULL, MS(100)).crossings;
}

TEST(test_perc_tune_raises_pitch)
{
    CHECK(crossings(ACID_PERC_TOMS, 41, ACID_PERC_TUNE, 127) > 2 * crossings(ACID_PERC_TOMS, 41, ACID_PERC_TUNE, 0));
    CHECK(crossings(ACID_PERC_RIM, 37, ACID_PERC_TUNE, 127) > 2 * crossings(ACID_PERC_RIM, 37, ACID_PERC_TUNE, 0));
    CHECK(crossings(ACID_PERC_COW, 56, ACID_PERC_TUNE, 127) > 2 * crossings(ACID_PERC_COW, 56, ACID_PERC_TUNE, 0));
}

TEST(test_perc_toms_interval)
{
    // Интервал 0: все томы одной высоты. crossings() начинает с умолчаний (строй ~80 Гц).
    int lo = crossings(ACID_PERC_TOMS, 41, ACID_PERC_INTERVAL, 0);
    int hi = crossings(ACID_PERC_TOMS, 50, ACID_PERC_INTERVAL, 0);
    CHECK(abs(hi - lo) <= lo / 20 + 1);
    // Интервал 127 (12 полутонов): 50 на 5 октав выше 41 (~2.6 кГц, до зажатия далеко).
    lo = crossings(ACID_PERC_TOMS, 41, ACID_PERC_INTERVAL, 127);
    hi = crossings(ACID_PERC_TOMS, 50, ACID_PERC_INTERVAL, 127);
    printf("  interval 12: tom 41 %d crossings, tom 50 %d\n", lo, hi);
    CHECK(hi > lo * 32 * 0.9 && hi < lo * 32 * 1.1);
}

static stats_t one_hit(int inst, int note, int knob, int v)
{
    fresh();
    if (knob >= 0) {
        acid_perc_set(&p, inst, knob, v);
    }
    acid_perc_trigger(&p, note, 1.0f);
    return run(MS(200), NULL, 0);
}

TEST(test_perc_width_spreads_toms)
{
    fresh();
    acid_perc_set(&p, ACID_PERC_TOMS, ACID_PERC_WIDTH, 0);
    acid_perc_trigger(&p, 41, 1.0f);
    stats_t c = run(MS(200), NULL, 0);
    CHECK(fabs(c.el - c.er) <= 0.01 * c.el);
    stats_t a = one_hit(ACID_PERC_TOMS, 41, ACID_PERC_WIDTH, 127);
    stats_t b = one_hit(ACID_PERC_TOMS, 50, ACID_PERC_WIDTH, 127);
    int a_left = a.el > a.er;
    CHECK(a_left != (b.el > b.er));
    CHECK((a_left ? a.el / a.er : a.er / a.el) > 4.0);
    CHECK((a_left ? b.er / b.el : b.el / b.er) > 4.0);
}

TEST(test_perc_pan)
{
    stats_t a = one_hit(ACID_PERC_RIM, 37, ACID_PERC_PAN, 0);
    CHECK(a.er < 0.05 * a.el);
    stats_t b = one_hit(ACID_PERC_COW, 56, ACID_PERC_PAN, 127);
    CHECK(b.el < 0.05 * b.er);
}

static double peak_at(int sens, float v)
{
    fresh();
    acid_perc_set(&p, ACID_PERC_TOMS, ACID_PERC_SENS, sens);
    acid_perc_trigger(&p, 45, v);
    return run(MS(200), NULL, 0).peak;
}

TEST(test_perc_velocity_sens)
{
    CHECK_NEAR(peak_at(0, 0.3f), peak_at(0, 1.0f), 0.02 * peak_at(0, 1.0f));
    CHECK(peak_at(127, 0.3f) < 0.5 * peak_at(127, 1.0f));
}

TEST(test_perc_soft_hit_audible)
{
    CHECK(peak_at(127, FLOOR) > 0.2 * peak_at(127, 1.0f));
}

TEST(test_perc_model_out_of_range_ignored)
{
    fresh();
    acid_perc_set(&p, ACID_PERC_TOMS, ACID_PERC_MODEL, 3);
    acid_perc_set(&p, ACID_PERC_TOMS, ACID_PERC_MODEL, 5);
    CHECK(p.inst[ACID_PERC_TOMS].model == 3);
    acid_perc_set(&p, ACID_PERC_TOMS, ACID_PERC_MODEL, 127);
    CHECK(p.inst[ACID_PERC_TOMS].model == 3);
    static acid_perc_inst_t before;
    before = p.inst[ACID_PERC_RIM];
    acid_perc_set(&p, ACID_PERC_RIM, ACID_PERC_INTERVAL, 0);
    acid_perc_set(&p, ACID_PERC_RIM, ACID_PERC_WIDTH, 127);
    acid_perc_set(&p, ACID_PERC_COW, ACID_PERC_INTERVAL, 0);
    CHECK(memcmp(&before, &p.inst[ACID_PERC_RIM], sizeof before) == 0);
}

TEST(test_perc_retrigger_fades)
{
    fresh();
    acid_perc_trigger(&p, 45, 1.0f);
    run(MS(10), NULL, 0);
    acid_perc_trigger(&p, 45, 1.0f);
    CHECK(acid_perc_voices(&p) == 2);
    CHECK(acid_perc_sounding(&p) == 1);
    run(MS(30), NULL, 0);
    CHECK(acid_perc_voices(&p) == 1);
    // Разные томы друг друга не глушат.
    acid_perc_trigger(&p, 41, 1.0f);
    CHECK(acid_perc_sounding(&p) == 2);
    CHECK(p.hard_cuts == 0);
}

TEST(test_perc_roll_no_hard_cut)
{
    static const int NOTES[8] = {37, 41, 43, 45, 47, 48, 50, 56};
    fresh();
    for (int i = 0; i < ACID_PERC_N; i++) {
        acid_perc_set(&p, i, ACID_PERC_DECAY, 127); // хвосты 2 с: слоты кончаются на звучащих
    }
    uint32_t rng = 12345;
    int step = (int)(SR * 60.0f / 180.0f / 4.0f);
    int worst = 0;
    for (int s = 0; s < 20 * 180 * 4 / 60; s++) {
        if (s % 16 == 0) {
            for (int i = 0; i < ACID_PERC_N; i++) {
                acid_perc_set(&p, i, ACID_PERC_MODEL, (s / 16 + i) % ACID_PERC_MODELS);
            }
        }
        rng = rng * 1664525u + 1013904223u;
        int k = 1 + (int)((rng >> 24) % 3);
        int first = (int)((rng >> 8) % 8);
        for (int j = 0; j < k; j++) {
            acid_perc_trigger(&p, NOTES[(first + j * 3) % 8], 0.3f + 0.7f * (float)((rng >> (j * 4)) & 15) / 15.0f);
            worst = acid_perc_sounding(&p) > worst ? acid_perc_sounding(&p) : worst;
        }
        stats_t st = run(step, NULL, 0);
        CHECK(st.finite);
    }
    CHECK(worst == ACID_PERC_SOUNDING);
    CHECK(p.hard_cuts == 0);
}

TEST(test_perc_steal_quietest)
{
    fresh();
    for (int i = 0; i < ACID_PERC_N; i++) {
        acid_perc_set(&p, i, ACID_PERC_DECAY, 127);
    }
    acid_perc_trigger(&p, 41, 0.2f); // тихий
    run(MS(50), NULL, 0);
    acid_perc_trigger(&p, 43, 1.0f);
    acid_perc_trigger(&p, 37, 1.0f);
    run(MS(20), NULL, 0);
    acid_perc_trigger(&p, 56, 1.0f); // четвёртый: уходит тихий 41
    CHECK(acid_perc_sounding(&p) == 3);
    int alive41 = 0, alive43 = 0;
    for (int i = 0; i < ACID_PERC_SLOTS; i++) {
        const acid_perc_voice_t *v = &p.v[i];
        if (v->active && v->fade_step == 0.0f) {
            alive41 |= v->note == 41;
            alive43 |= v->note == 43;
        }
    }
    CHECK(!alive41 && alive43);
    CHECK(p.hard_cuts == 0);
}

TEST(test_perc_extreme_pitch_stable)
{
    for (int m = 0; m < ACID_PERC_MODELS; m++) {
        fresh();
        acid_perc_set(&p, ACID_PERC_TOMS, ACID_PERC_MODEL, m);
        static const int K[] = {ACID_PERC_TUNE, ACID_PERC_INTERVAL, ACID_PERC_PDEPTH, ACID_PERC_TONE, ACID_PERC_CHAR};
        for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) {
            acid_perc_set(&p, ACID_PERC_TOMS, K[i], 127);
        }
        acid_perc_trigger(&p, 50, 1.0f);
        stats_t st = run((int)((10.0f * decay_s(p.inst[0].knob[ACID_PERC_DECAY]) + 0.2f) * SR), NULL, 0);
        CHECK(st.finite && st.peak < 4.0);
        CHECK(acid_perc_voices(&p) == 0);
    }
}

TEST(test_perc_knobs_while_ringing)
{
    fresh();
    uint32_t rng = 777;
    double worst = 0.0;
    int finite = 1;
    for (int hit = 0; hit < 40; hit++) {
        acid_perc_trigger(&p, (int[]){37, 41, 45, 50, 56}[hit % 5], 1.0f);
        for (int t = 0; t < 20; t++) {
            rng = rng * 1664525u + 1013904223u;
            int inst = (int)((rng >> 8) % ACID_PERC_N), k = (int)((rng >> 12) % ACID_PERC_KNOBS);
            acid_perc_set(&p, inst, k, (int)((rng >> 20) % 128));
            stats_t st = run(MS(5), NULL, 0);
            finite &= st.finite;
            worst = st.peak > worst ? st.peak : worst;
        }
    }
    CHECK(finite);
    CHECK(worst < 4.0);
    CHECK(p.hard_cuts == 0);
}

// Перегруз: лимит звучащих голосов падает, лишние уходят сразу и плавно.
TEST(test_perc_limit_fades_excess)
{
    fresh();
    for (int i = 0; i < ACID_PERC_N; i++) {
        acid_perc_set(&p, i, ACID_PERC_DECAY, 127);
    }
    acid_perc_trigger(&p, 41, 1.0f);
    acid_perc_trigger(&p, 37, 1.0f);
    acid_perc_trigger(&p, 56, 0.5f);
    CHECK(acid_perc_sounding(&p) == 3);
    acid_perc_set_limit(&p, 1);
    CHECK(acid_perc_sounding(&p) == 1);
    CHECK(acid_perc_voices(&p) == 3); // уходящие ещё звучат
    run(MS(10), NULL, 0);
    CHECK(acid_perc_voices(&p) == 1);
    acid_perc_trigger(&p, 45, 1.0f);
    CHECK(acid_perc_sounding(&p) == 1);
    acid_perc_set_limit(&p, ACID_PERC_SOUNDING);
    acid_perc_trigger(&p, 50, 1.0f);
    acid_perc_trigger(&p, 43, 1.0f);
    CHECK(acid_perc_sounding(&p) == 3);
    CHECK(p.hard_cuts == 0);
}

int main(void)
{
    RUN(test_perc_limit_fades_excess);
    RUN(test_perc_silent_until_trigger);
    RUN(test_perc_models_sound_and_end);
    RUN(test_perc_every_knob_acts);
    RUN(test_perc_tune_raises_pitch);
    RUN(test_perc_toms_interval);
    RUN(test_perc_width_spreads_toms);
    RUN(test_perc_pan);
    RUN(test_perc_velocity_sens);
    RUN(test_perc_soft_hit_audible);
    RUN(test_perc_model_out_of_range_ignored);
    RUN(test_perc_retrigger_fades);
    RUN(test_perc_roll_no_hard_cut);
    RUN(test_perc_steal_quietest);
    RUN(test_perc_extreme_pitch_stable);
    RUN(test_perc_knobs_while_ringing);
    TEST_MAIN_END();
}
