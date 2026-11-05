#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "petri.h"
#include "test.h"

static int16_t U1[PETRI_N * PETRI_N], V1[PETRI_N * PETRI_N];
static int8_t DK1[PETRI_N * PETRI_N];

#define NN (PETRI_N * PETRI_N)
static petri_t P;
static int16_t T[PETRI_TABLE];
static int8_t M[PETRI_MODS];

static void empty(petri_t *p)
{
    for (int i = 0; i < NN; i++) {
        p->u[i] = PETRI_Q;
        p->v[i] = 0;
        p->dk[i] = 0;
    }
}

static void grown(petri_t *p)
{
    petri_init(p, 2);
    for (int s = 0; s < 600; s++) {
        petri_step(p);
    }
}

TEST(empty_dish_is_silent)
{
    petri_init(&P, 1);
    empty(&P);
    for (int m = 0; m < PETRI_MODES; m++) {
        if (m == PETRI_VOID) {
            continue;
        }
        P.mode = (uint8_t)m;
        P.contrast = 0.7f;
        petri_capture(&P, T, M);
        int nz = 0;
        for (int j = 0; j < PETRI_TABLE; j++) {
            nz += T[j] != 0;
        }
        CHECK(nz == 0);
        CHECK(M[5] == 0);
    }
}

TEST(void_mode_sounds_in_empty)
{
    petri_init(&P, 1);
    empty(&P);
    P.mode = PETRI_VOID;
    P.contrast = 0.5f;
    petri_capture(&P, T, M);
    int loud = 0;
    for (int j = 0; j < PETRI_TABLE; j++) {
        loud += abs(T[j]) > 1000;
    }
    CHECK(loud > 0);
}

TEST(mean_removed)
{
    grown(&P);
    P.mode = PETRI_HEIGHT;
    petri_capture(&P, T, M);
    long sum = 0;
    int nz = 0;
    for (int j = 0; j < PETRI_TABLE; j++) {
        sum += T[j];
        nz += T[j] != 0;
    }
    CHECK(labs(sum) / 256.0 < 2);
    CHECK(nz > 100);
}

// Эталон Края и Фолда во float: знак таблицы не должен перевернуться от переполнения int16.
static float ref_h(const petri_t *p, int mode, float x, float y, float c)
{
    float v = petri_v(p, x, y);
    if (mode == PETRI_FOLD) {
        return sinf(4 * c * (v - 0.12f));
    }
    float gx = petri_v(p, x + 1, y) - petri_v(p, x - 1, y), gy = petri_v(p, x, y + 1) - petri_v(p, x, y - 1);
    return tanhf(4 * c * sqrtf(gx * gx + gy * gy));
}

TEST(table_in_range_at_max_contrast)
{
    grown(&P);
    int modes[2] = {PETRI_FOLD, PETRI_EDGE};
    for (int k = 0; k < 2; k++) {
        P.mode = (uint8_t)modes[k];
        P.contrast = 1;
        petri_capture(&P, T, M);
        float h[PETRI_TABLE], mean = 0;
        for (int j = 0; j < PETRI_TABLE; j++) {
            float x, y;
            petri_orbit_point(&P, 6.2831853f * j / PETRI_TABLE, &x, &y);
            h[j] = ref_h(&P, modes[k], x, y, 40);
            mean += h[j] / PETRI_TABLE;
        }
        int flipped = 0;
        for (int j = 0; j < PETRI_TABLE; j++) {
            float r = h[j] - mean;
            flipped += fabsf(r) > 0.05f && (r > 0) != (T[j] > 0);
        }
        CHECK(flipped == 0);
    }
}

// Одно пятно на пустой орбите, Высота на максимуме контраста: фон -1, пятно +1, разница x1.5 выходит за int16
// и должна упереться в край, а не перевернуть знак.
TEST(edge_spike_clamped)
{
    petri_init(&P, 1);
    empty(&P);
    P.cx = P.cy = 48;
    P.radius = 18;
    P.mode = PETRI_HEIGHT;
    P.contrast = 1;
    for (int y = 46; y <= 50; y++) {
        for (int x = 64; x <= 68; x++) {
            P.v[y * PETRI_N + x] = PETRI_Q;
        }
    }
    petri_capture(&P, T, M);
    int hi = 0, neg_big = 0;
    for (int j = 0; j < PETRI_TABLE; j++) {
        hi = T[j] > hi ? T[j] : hi;
        neg_big += T[j] < -20000;
    }
    CHECK(hi == 32767);
    CHECK(neg_big == 0);
}

TEST(circle_orbit_points)
{
    petri_init(&P, 1);
    P.cx = P.cy = 48;
    P.radius = 10;
    P.shape = PETRI_CIRCLE;
    float x, y;
    petri_orbit_point(&P, 0, &x, &y);
    CHECK_NEAR(x, 58, 1e-3);
    CHECK_NEAR(y, 48, 1e-3);
    petri_orbit_point(&P, 1.5707963f, &x, &y);
    CHECK_NEAR(x, 48, 1e-3);
    CHECK_NEAR(y, 58, 1e-3);
}

TEST(rose_returns_to_center)
{
    petri_init(&P, 1);
    P.cx = P.cy = 48;
    P.radius = 20;
    P.shape = PETRI_ROSE;
    float x, y;
    petri_orbit_point(&P, 3.14159265f / 6, &x, &y);
    CHECK_NEAR(x, 48, 1e-3);
    CHECK_NEAR(y, 48, 1e-3);
}

TEST(pop_mod)
{
    petri_init(&P, 1);
    petri_set_count(&P, PETRI_GRAZER, 8);
    petri_capture(&P, T, M);
    CHECK(M[0] >= 63 && M[0] <= 64);
}

TEST(follow_tracks_grazer)
{
    petri_init(&P, 1);
    for (int k = 0; k < PETRI_KINDS; k++) {
        petri_set_count(&P, k, k == PETRI_GRAZER);
    }
    P.a[0].x = P.a[0].y = 20;
    P.cx = P.cy = 48;
    P.follow = 1;
    for (int i = 0; i < 60; i++) {
        petri_move_center(&P, 0.05f);
    }
    CHECK_NEAR(P.cx, 20, 1);
    CHECK_NEAR(P.cy, 20, 1);
}

TEST(init_defaults)
{
    petri_init(&P, 1);
    CHECK(P.mode == PETRI_HEIGHT && P.shape == PETRI_CIRCLE);
    CHECK_NEAR(P.radius, 18, 1e-6);
    CHECK_NEAR(P.drift, 2, 1e-6);
    CHECK_NEAR(P.cx, 48, 1e-6);
}

int main(void)
{
    petri_bind(&P, U1, V1, DK1);
    RUN(empty_dish_is_silent);
    RUN(void_mode_sounds_in_empty);
    RUN(mean_removed);
    RUN(table_in_range_at_max_contrast);
    RUN(edge_spike_clamped);
    RUN(circle_orbit_points);
    RUN(rose_returns_to_center);
    RUN(pop_mod);
    RUN(follow_tracks_grazer);
    RUN(init_defaults);
    TEST_MAIN_END();
}
