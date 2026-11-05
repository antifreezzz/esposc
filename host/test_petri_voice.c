#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "acid_petri.h"
#include "test.h"

#define SR 48000.0f
static acid_petri_t P;
static acid_petri_frame_t F;

static void sine(acid_petri_frame_t *f, float amp, float dc)
{
    memset(f, 0, sizeof *f);
    for (int i = 0; i < ACID_PETRI_TABLE; i++) {
        f->tab[i] = (int16_t)lrintf(16000.0f * (amp * sinf(6.2831853f * i / ACID_PETRI_TABLE) + dc));
    }
}

static void start(void)
{
    acid_petri_init(&P, SR);
    acid_petri_use_baked(&P, NULL, 0);
}

static void run(int n)
{
    for (int i = 0; i < n; i++) {
        acid_petri_osc(&P);
    }
}

TEST(silent_without_notes)
{
    start();
    sine(&F, 1, 0);
    acid_petri_post(&P, &F);
    int loud = 0;
    for (int i = 0; i < 4800; i++) {
        loud += acid_petri_osc(&P) != 0.0f;
    }
    CHECK(loud == 0);
    CHECK(!acid_petri_active(&P));
}

static int crossings(int n, float *peak)
{
    int c = 0;
    float prev = 0, pk = 0;
    for (int i = 0; i < n; i++) {
        float y = acid_petri_osc(&P);
        c += (prev < 0) != (y < 0) && i > 0;
        prev = y;
        pk = fabsf(y) > pk ? fabsf(y) : pk;
    }
    if (peak) {
        *peak = pk;
    }
    return c;
}

TEST(plays_table_pitch)
{
    start();
    sine(&F, 1, 0);
    acid_petri_post(&P, &F);
    acid_petri_note_on(&P, 57, 220, 100);
    run(4800);
    float pk;
    int c = crossings(9600, &pk);
    CHECK_NEAR(c, 2 * 220 * 0.2, 3);
    CHECK(pk > 0.1f);
    CHECK(acid_petri_active(&P));
}

TEST(crossfade_no_jump)
{
    start();
    sine(&F, 1, 0);
    acid_petri_post(&P, &F);
    acid_petri_note_on(&P, 57, 220, 100);
    run(9600);
    float prev = acid_petri_osc(&P), dmax_before = 0;
    for (int i = 0; i < 2400; i++) {
        float y = acid_petri_osc(&P);
        dmax_before = fabsf(y - prev) > dmax_before ? fabsf(y - prev) : dmax_before;
        prev = y;
    }
    sine(&F, -1, 0);
    acid_petri_post(&P, &F);
    float dmax = 0;
    for (int i = 0; i < 2880; i++) { // 60 мс: кроссфейд 50 мс
        float y = acid_petri_osc(&P);
        dmax = fabsf(y - prev) > dmax ? fabsf(y - prev) : dmax;
        prev = y;
    }
    CHECK(dmax < 1.5f * dmax_before);
    CHECK_NEAR(P.x, 1.0, 1e-6);
}

TEST(latest_table_wins)
{
    start();
    sine(&F, 1, 0);
    acid_petri_post(&P, &F);
    acid_petri_note_on(&P, 57, 220, 100);
    run(4800);
    sine(&F, 0.5f, 0);
    acid_petri_post(&P, &F);
    run(100); // кроссфейд начался
    sine(&F, -1, 0);
    acid_petri_post(&P, &F); // ещё во время перехода
    sine(&F, 0.25f, 0);
    acid_petri_post(&P, &F); // последняя
    run(9600);
    CHECK(!P.pending);
    CHECK(P.w[P.b].t[0][64] == (int16_t)lrintf(16000.0f * 0.25f));
}

TEST(four_voices_then_steal)
{
    start();
    sine(&F, 1, 0);
    acid_petri_post(&P, &F);
    float hz[5] = {110, 220, 330, 440, 550};
    for (int i = 0; i < 5; i++) {
        acid_petri_note_on(&P, i, hz[i], 100);
        run(48);
    }
    int on = 0, has110 = 0;
    for (int i = 0; i < ACID_PETRI_VOICES; i++) {
        on += P.v[i].gate;
        has110 += P.v[i].gate && P.v[i].hz == 110;
    }
    CHECK(on == 4);
    CHECK(has110 == 0);
    acid_petri_note_off(&P, 2); // нота 2 - 330 Гц
    on = 0;
    for (int i = 0; i < ACID_PETRI_VOICES; i++) {
        on += P.v[i].gate;
        CHECK(!(P.v[i].gate && P.v[i].hz == 330));
    }
    CHECK(on == 3);
}

TEST(mip_by_pitch)
{
    start();
    acid_petri_note_on(&P, 100, 3000, 100);
    acid_petri_note_on(&P, 60, 300, 100);
    int lv3000 = -1, lv300 = -1;
    for (int i = 0; i < ACID_PETRI_VOICES; i++) {
        if (P.v[i].gate && P.v[i].hz == 3000) {
            lv3000 = P.v[i].level;
        }
        if (P.v[i].gate && P.v[i].hz == 300) {
            lv300 = P.v[i].level;
        }
    }
    CHECK(lv3000 == 2);
    CHECK(lv300 == 0);
}

TEST(wave_build_averages)
{
    static acid_petri_wave_t w;
    int16_t t[ACID_PETRI_TABLE];
    for (int i = 0; i < ACID_PETRI_TABLE; i++) {
        t[i] = i < 128 ? 1000 : -1000;
    }
    acid_petri_wave_build(&w, t);
    CHECK(w.t[0][ACID_PETRI_TABLE] == w.t[0][0]);
    CHECK(w.t[1][10] == 1000);
    CHECK(abs(w.t[1][127]) < 1000); // на переходе - среднее
    CHECK(abs(w.t[3][127]) < 1000);
    CHECK(w.t[3][ACID_PETRI_TABLE] == w.t[3][0]);
    CHECK(w.t[3][64] == 1000);
}

TEST(freeze_holds)
{
    start();
    sine(&F, 1, 0);
    acid_petri_post(&P, &F);
    acid_petri_note_on(&P, 57, 220, 100);
    run(4800);
    acid_petri_freeze(&P, 1);
    run(4800);
    sine(&F, -1, 0);
    acid_petri_post(&P, &F);
    run(4800);
    CHECK(P.m > 0.99f);
    CHECK(P.fz.t[0][64] == 16000);
}

TEST(baked_after_silence)
{
    static acid_petri_frame_t set[2];
    sine(&set[0], 1, 0);
    sine(&set[1], 0.5f, 0);
    acid_petri_init(&P, SR);
    acid_petri_use_baked(&P, set, 2);
    acid_petri_note_on(&P, 57, 220, 100);
    float pk;
    crossings(4800, &pk);
    CHECK(pk > 0.1f); // зашитый набор звучит сразу
    // Снаружи пришла таблица, потом панель пропала: через 0.5 с снова зашитые.
    sine(&F, 0.1f, 0);
    acid_petri_post(&P, &F);
    run(4800);
    CHECK(P.w[P.b].t[0][64] == 1600);
    run(48000);
    CHECK(P.w[P.b].t[0][64] != 1600);
}

TEST(dc_removed)
{
    start();
    sine(&F, 0.5f, 0.4f);
    acid_petri_post(&P, &F);
    acid_petri_note_on(&P, 57, 220, 100);
    run(48000);
    double s = 0;
    for (int i = 0; i < 4800; i++) {
        s += acid_petri_osc(&P);
    }
    CHECK(fabs(s / 4800) < 0.02);
}

TEST(jump_reported_once)
{
    start();
    sine(&F, 1, 0);
    F.jumps = 0;
    acid_petri_post(&P, &F);
    run(4800);
    acid_petri_jumped(&P);
    F.jumps = 1;
    acid_petri_post(&P, &F);
    run(4800);
    CHECK(acid_petri_jumped(&P) == 1);
    CHECK(acid_petri_jumped(&P) == 0);
}

int main(void)
{
    RUN(silent_without_notes);
    RUN(plays_table_pitch);
    RUN(crossfade_no_jump);
    RUN(latest_table_wins);
    RUN(four_voices_then_steal);
    RUN(mip_by_pitch);
    RUN(wave_build_averages);
    RUN(freeze_holds);
    RUN(baked_after_silence);
    RUN(dc_removed);
    RUN(jump_reported_once);
    TEST_MAIN_END();
}
