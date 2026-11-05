#include <stdlib.h>
#include <string.h>

#include "petri.h"
#include "test.h"

static int16_t U1[PETRI_N * PETRI_N], V1[PETRI_N * PETRI_N], U2[PETRI_N * PETRI_N], V2[PETRI_N * PETRI_N];
static int8_t DK1[PETRI_N * PETRI_N], DK2[PETRI_N * PETRI_N];

#define NN (PETRI_N * PETRI_N)
#define GOLDEN 0xf3c39945u // первая версия шага (9 указателей в одном цикле)
static petri_t P, P2;

static void clear(petri_t *p)
{
    for (int i = 0; i < NN; i++) {
        p->u[i] = PETRI_Q;
        p->v[i] = 0;
        p->dk[i] = 0;
    }
}

TEST(uniform_stays_uniform)
{
    petri_init(&P, 1);
    clear(&P);
    for (int s = 0; s < 50; s++) {
        petri_step(&P);
    }
    int bad = 0;
    for (int i = 0; i < NN; i++) {
        bad += P.u[i] != PETRI_Q || P.v[i] != 0;
    }
    CHECK(bad == 0);
}

TEST(values_stay_in_range)
{
    petri_init(&P, 1);
    petri_set_fk(&P, 0.09f, 0.045f);
    memset(P.dk, 127, NN);
    int bad = 0;
    for (int s = 0; s < 500; s++) {
        petri_step(&P);
        for (int i = 0; i < NN; i++) {
            bad += P.u[i] < 0 || P.v[i] < 0;
        }
    }
    CHECK(bad == 0);
    // Обратный край: всё на максимуме.
    for (int i = 0; i < NN; i++) {
        P.u[i] = P.v[i] = PETRI_Q;
        P.dk[i] = -127;
    }
    for (int s = 0; s < 50; s++) {
        petri_step(&P);
        for (int i = 0; i < NN; i++) {
            bad += P.u[i] < 0 || P.v[i] < 0;
        }
    }
    CHECK(bad == 0);
}

#define AT(p, x, y) ((p)->v[(y) * PETRI_N + (x)])

TEST(drop_wraps_torus)
{
    petri_init(&P, 1);
    clear(&P);
    petri_drop(&P, 0, 0, 2);
    CHECK(AT(&P, 0, 0) > 0);
    CHECK(AT(&P, 95, 0) > 0);
    CHECK(AT(&P, 0, 95) > 0);
    CHECK(AT(&P, 95, 95) > 0);
    CHECK(AT(&P, 48, 48) == 0);
    clear(&P);
    petri_drop(&P, -1, 96.5f, 1);
    CHECK(AT(&P, 95, 0) > 0);
}

TEST(same_seed_same_dish)
{
    petri_init(&P, 7);
    petri_init(&P2, 7);
    for (int s = 0; s < 100; s++) {
        petri_step(&P);
        petri_step(&P2);
    }
    CHECK(memcmp(P.u, P2.u, NN * sizeof *P.u) == 0);
    CHECK(memcmp(P.v, P2.v, NN * sizeof *P.v) == 0);
    petri_init(&P2, 8);
    for (int s = 0; s < 100; s++) {
        petri_step(&P2);
    }
    CHECK(memcmp(P.v, P2.v, NN * sizeof *P.v) != 0);
}

// Эталон тех же формул во float с двойным буфером.
static float RU[NN], RV[NN], RU2[NN], RV2[NN];

static void ref_step(float F, float K)
{
    const int N = PETRI_N;
    for (int y = 0; y < N; y++) {
        int ym = ((y + N - 1) % N) * N, y0 = y * N, yp = ((y + 1) % N) * N;
        for (int x = 0; x < N; x++) {
            int xm = (x + N - 1) % N, xp = (x + 1) % N, i = y0 + x;
            float u = RU[i], v = RV[i];
            float lu = 0.2f * (RU[y0 + xm] + RU[y0 + xp] + RU[ym + x] + RU[yp + x]) +
                       0.05f * (RU[ym + xm] + RU[ym + xp] + RU[yp + xm] + RU[yp + xp]) - u;
            float lv = 0.2f * (RV[y0 + xm] + RV[y0 + xp] + RV[ym + x] + RV[yp + x]) +
                       0.05f * (RV[ym + xm] + RV[ym + xp] + RV[yp + xm] + RV[yp + xp]) - v;
            float uvv = u * v * v;
            RU2[i] = u + (lu - uvv + F * (1 - u));
            RV2[i] = v + (0.5f * lv + uvv - (F + K) * v);
        }
    }
    memcpy(RU, RU2, sizeof RU);
    memcpy(RV, RV2, sizeof RV);
}

static void metrics_q(const petri_t *p, double *mean, double *frac)
{
    double s = 0;
    int n = 0;
    for (int i = 0; i < NN; i++) {
        s += p->v[i] / (double)PETRI_Q;
        n += p->v[i] > PETRI_Q / 10;
    }
    *mean = s / NN;
    *frac = (double)n / NN;
}

TEST(matches_float_reference)
{
    petri_init(&P, 11);
    for (int i = 0; i < NN; i++) {
        RU[i] = P.u[i] / (float)PETRI_Q;
        RV[i] = P.v[i] / (float)PETRI_Q;
    }
    float F, K;
    petri_preset(0, &F, &K);
    for (int s = 0; s < 300; s++) {
        petri_step(&P);
        ref_step(F, K);
    }
    double mq, fq, mr = 0;
    int nr = 0;
    metrics_q(&P, &mq, &fq);
    for (int i = 0; i < NN; i++) {
        mr += RV[i];
        nr += RV[i] > 0.1f;
    }
    mr /= NN;
    CHECK(mr > 0.001);
    CHECK_NEAR(mq / mr, 1.0, 0.10);
    CHECK_NEAR(fq, (double)nr / NN, 0.05);
}

TEST(presets_stay_alive)
{
    for (int k = 0; k < PETRI_PRESETS; k++) {
        petri_init(&P, 3);
        float f, kk;
        petri_preset(k, &f, &kk);
        petri_set_fk(&P, f, kk);
        for (int s = 0; s < 600; s++) {
            petri_step(&P);
        }
        double m, fr;
        metrics_q(&P, &m, &fr);
        printf("  preset %d: frac %.3f mean %.4f\n", k, fr, m);
        CHECK(fr > 0.01 && fr < 0.9);
    }
}

// Сетки - память вызывающего (на панели внутренняя RAM фрагментирована, одним куском 51 КБ не выделить).
TEST(bind_uses_caller_grids)
{
    petri_t q;
    petri_bind(&q, U1, V1, DK1);
    petri_init(&q, 4);
    CHECK(q.u == U1 && q.v == V1 && q.dk == DK1);
    int ones = 0, seeded = 0;
    for (int i = 0; i < NN; i++) {
        ones += U1[i] == PETRI_Q;
        seeded += V1[i] > 0;
    }
    CHECK(ones > NN / 2);
    CHECK(seeded > 0);
}

static uint32_t dish_hash(const petri_t *p)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < NN; i++) {
        h = (h ^ (uint16_t)p->u[i]) * 16777619u;
        h = (h ^ (uint16_t)p->v[i]) * 16777619u;
    }
    return h;
}

// Шаг прибит к первой версии бит в бит: перестановки ради скорости не меняют арифметику.
TEST(step_golden)
{
    petri_init(&P, 9);
    memset(P.dk, 0, NN);
    for (int i = 0; i < NN; i += 7) {
        P.dk[i] = (int8_t)((i % 255) - 127);
    }
    for (int s = 0; s < 200; s++) {
        petri_step(&P);
    }
    printf("  hash %08x\n", (unsigned)dish_hash(&P));
    CHECK(dish_hash(&P) == GOLDEN);
}

TEST(bilinear_read)
{
    petri_init(&P, 1);
    clear(&P);
    AT(&P, 10, 10) = PETRI_Q;
    CHECK_NEAR(petri_v(&P, 10, 10), 1.0, 1e-4);
    CHECK_NEAR(petri_v(&P, 10.5f, 10), 0.5, 1e-4);
    CHECK_NEAR(petri_v(&P, 106, 10), 1.0, 1e-4);
    CHECK_NEAR(petri_v(&P, -86, 10), 1.0, 1e-4);
}

int main(void)
{
    petri_bind(&P, U1, V1, DK1);
    petri_bind(&P2, U2, V2, DK2);
    RUN(bind_uses_caller_grids);
    petri_bind(&P, U1, V1, DK1);
    RUN(uniform_stays_uniform);
    RUN(values_stay_in_range);
    RUN(drop_wraps_torus);
    RUN(same_seed_same_dish);
    RUN(matches_float_reference);
    RUN(presets_stay_alive);
    RUN(step_golden);
    RUN(bilinear_read);
    TEST_MAIN_END();
}
