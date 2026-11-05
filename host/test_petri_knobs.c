#include <math.h>
// Ручки синта -> параметры чашки на панели: синт типа PETRI (синт 1 главнее), страница «Чашка».
#include <string.h>

#include "petri.h"
#include "petri_knobs.h"
#include "test.h"

static int16_t U1[PETRI_N * PETRI_N], V1[PETRI_N * PETRI_N];
static int8_t DK1[PETRI_N * PETRI_N];
static petri_t P;
static petri_knobs_t K;
static uint8_t raw[PL_PAGES][PL_KNOBS];

static void defaults(void)
{
    static const uint8_t PAGE[16] = {0, 0, 64, 64, 4, 3, 1, 2, 1, 38, 42, 26, 108, 102, 0, 0};
    memset(raw, 0, sizeof raw);
    memcpy(raw[PL_PAGE_PETRI], PAGE, 16);
    raw[6][1] = 0;  // синт 1 - 303
    raw[7][1] = 11; // синт 2 - FM
    petri_bind(&P, U1, V1, DK1);
    petri_init(&P, 1);
    petri_knobs_init(&K);
}

TEST(none_when_no_petri)
{
    defaults();
    CHECK(petri_knobs_apply(&P, &K, raw) == -1);
}

TEST(params_from_synth1)
{
    defaults();
    raw[6][1] = 25;
    raw[7][1] = 25;
    raw[6][8] = 2;   // Фолд
    raw[6][9] = 127; // контраст 1
    raw[6][10] = 3;  // Роза
    raw[6][11] = 127; // радиус 48
    raw[6][12] = 127; // за пожирателем
    raw[7][8] = 1;
    CHECK(petri_knobs_apply(&P, &K, raw) == 0);
    CHECK(P.mode == PETRI_FOLD && P.shape == PETRI_ROSE && P.follow == 1);
    CHECK_NEAR(P.contrast, 1, 1e-6);
    CHECK_NEAR(P.radius, 48, 1e-4);
    raw[6][1] = 0;
    CHECK(petri_knobs_apply(&P, &K, raw) == 1);
    CHECK(P.mode == PETRI_EDGE);
    raw[7][12] = 63;
    petri_knobs_apply(&P, &K, raw);
    CHECK(P.follow == 0);
    CHECK_NEAR(P.drift, 7.5, 0.01);
}

TEST(page_params)
{
    defaults();
    raw[6][1] = 25;
    raw[PL_PAGE_PETRI][1] = 3;  // Черви
    raw[PL_PAGE_PETRI][2] = 127; // F +0.01
    raw[PL_PAGE_PETRI][4] = 6;
    raw[PL_PAGE_PETRI][9] = 127;
    petri_knobs_apply(&P, &K, raw);
    float f, k;
    petri_preset(3, &f, &k);
    CHECK(P.f_q == (int16_t)lrintf((f + 0.01f) * PETRI_Q));
    CHECK(P.k_q == (int16_t)lrintf(k * PETRI_Q));
    CHECK(K.steps == 6);
    CHECK_NEAR(P.appetite, 0.5, 1e-6);
}

TEST(counts_only_on_change)
{
    defaults();
    raw[6][1] = 25;
    petri_knobs_apply(&P, &K, raw);
    CHECK(petri_count(&P, PETRI_GRAZER) == 3);
    petri_set_count(&P, PETRI_GRAZER, 0); // вымерли
    petri_set_count(&P, PETRI_GRAZER, 3);
    P.n = 0; // и все жители пропали
    petri_knobs_apply(&P, &K, raw);
    CHECK(P.n == 0); // ручки не менялись: не воскрешаем
    raw[PL_PAGE_PETRI][7] = 4;
    petri_knobs_apply(&P, &K, raw);
    CHECK(petri_count(&P, PETRI_MUTAGEN) == 4);
    CHECK(petri_count(&P, PETRI_GRAZER) == 0);
}

TEST(reseed_edge)
{
    defaults();
    raw[6][1] = 25;
    petri_knobs_apply(&P, &K, raw);
    for (int i = 0; i < PETRI_N * PETRI_N; i++) {
        P.v[i] = 0;
    }
    petri_knobs_apply(&P, &K, raw);
    int seeded = 0;
    for (int i = 0; i < PETRI_N * PETRI_N; i++) {
        seeded += P.v[i] > 0;
    }
    CHECK(seeded == 0);
    raw[PL_PAGE_PETRI][15] = 1;
    petri_knobs_apply(&P, &K, raw);
    for (int i = 0; i < PETRI_N * PETRI_N; i++) {
        seeded += P.v[i] > 0;
    }
    CHECK(seeded > 0);
}

int main(void)
{
    RUN(none_when_no_petri);
    RUN(params_from_synth1);
    RUN(page_params);
    RUN(counts_only_on_change);
    RUN(reseed_edge);
    TEST_MAIN_END();
}
