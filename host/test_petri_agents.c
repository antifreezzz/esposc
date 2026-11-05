#include <string.h>

#include "petri.h"
#include "test.h"

#define NN (PETRI_N * PETRI_N)
static petri_t P;

static void only(petri_t *p, int kind, int n)
{
    for (int k = 0; k < PETRI_KINDS; k++) {
        petri_set_count(p, k, k == kind ? n : 0);
    }
}

TEST(start_population)
{
    petri_init(&P, 1);
    CHECK(petri_count(&P, PETRI_GRAZER) == 3);
    CHECK(petri_count(&P, PETRI_SEEDER) == 1);
    CHECK(petri_count(&P, PETRI_MUTAGEN) == 2);
    CHECK(petri_count(&P, PETRI_VORTEX) == 1);
}

TEST(set_count_clamps)
{
    petri_init(&P, 1);
    petri_set_count(&P, PETRI_VORTEX, 10);
    CHECK(petri_count(&P, PETRI_VORTEX) == 3);
    petri_set_count(&P, PETRI_SEEDER, 0);
    CHECK(petri_count(&P, PETRI_SEEDER) == 0);
    CHECK(petri_count(&P, PETRI_GRAZER) == 3);
    CHECK(petri_count(&P, PETRI_MUTAGEN) == 2);
    petri_set_count(&P, PETRI_GRAZER, 40);
    CHECK(petri_count(&P, PETRI_GRAZER) == 16);
    CHECK(P.n == 16 + 2 + 3);
}

TEST(grazers_eat)
{
    petri_init(&P, 1);
    only(&P, PETRI_GRAZER, 1);
    for (int i = 0; i < NN; i++) {
        P.u[i] = PETRI_Q / 2;
        P.v[i] = (int16_t)(0.3f * PETRI_Q);
    }
    P.a[0].x = P.a[0].y = 48;
    P.a[0].h = 0;
    petri_frame(&P, 0);
    CHECK(P.v[48 * PETRI_N + 48] < (int16_t)(0.3f * PETRI_Q));
    CHECK(P.u[48 * PETRI_N + 48] > PETRI_Q / 2);
    CHECK(P.v[10 * PETRI_N + 10] == (int16_t)(0.3f * PETRI_Q));
}

TEST(immigrant_returns)
{
    petri_init(&P, 1);
    only(&P, PETRI_GRAZER, 3);
    for (int i = 0; i < P.n; i++) {
        P.a[i].energy = -1;
    }
    petri_frame(&P, 0); // все гибнут, первый кадр без пожирателей
    CHECK(petri_count(&P, PETRI_GRAZER) == 0);
    for (int f = 0; f < 98; f++) {
        petri_frame(&P, 0);
    }
    CHECK(petri_count(&P, PETRI_GRAZER) == 0);
    petri_frame(&P, 0); // сотый кадр без пожирателей
    CHECK(petri_count(&P, PETRI_GRAZER) == 1);
}

TEST(no_immigrant_when_off)
{
    petri_init(&P, 1);
    only(&P, PETRI_GRAZER, 0);
    for (int f = 0; f < 200; f++) {
        petri_frame(&P, 0);
    }
    CHECK(petri_count(&P, PETRI_GRAZER) == 0);
}

TEST(agents_wrap)
{
    petri_init(&P, 1);
    only(&P, PETRI_VORTEX, 1);
    petri_set_count(&P, PETRI_GRAZER, 1);
    for (int i = 0; i < P.n; i++) {
        P.a[i].x = 95.9f;
        P.a[i].y = 0.1f;
        P.a[i].h = -0.785f;
    }
    int bad = 0;
    for (int f = 0; f < 10; f++) {
        petri_frame(&P, 1);
        for (int i = 0; i < P.n; i++) {
            bad += !(P.a[i].x >= 0 && P.a[i].x < PETRI_N && P.a[i].y >= 0 && P.a[i].y < PETRI_N);
        }
    }
    CHECK(bad == 0);
}

TEST(mutagen_marks_dk)
{
    petri_init(&P, 1);
    only(&P, PETRI_MUTAGEN, 1);
    for (int f = 0; f < 20; f++) {
        petri_frame(&P, 0);
    }
    int marked = 0;
    for (int i = 0; i < NN; i++) {
        marked += P.dk[i] != 0;
    }
    CHECK(marked > 20);
    // Без мутагенов след гаснет.
    only(&P, PETRI_MUTAGEN, 0);
    for (int f = 0; f < 6000; f++) { // экспонента: последние единицы держатся сотни кадров
        petri_frame(&P, 0);
    }
    marked = 0;
    for (int i = 0; i < NN; i++) {
        marked += P.dk[i] != 0;
    }
    CHECK(marked == 0);
}

TEST(seeder_jumps_count)
{
    petri_init(&P, 1);
    only(&P, PETRI_SEEDER, 1);
    float jmax = 0;
    for (int f = 0; f < 1000; f++) {
        petri_frame(&P, 0);
        jmax = P.jump > jmax ? P.jump : jmax;
        CHECK(P.jump >= 0 && P.jump <= 1);
    }
    printf("  jumps %d\n", P.jumps);
    CHECK(P.jumps >= 10 && P.jumps <= 100);
    CHECK(jmax == 1.0f);
}

TEST(vortex_keeps_range)
{
    petri_init(&P, 1);
    only(&P, PETRI_VORTEX, 3);
    for (int s = 0; s < 300; s++) {
        petri_step(&P);
    }
    int bad = 0;
    for (int f = 0; f < 100; f++) {
        petri_frame(&P, 2);
        for (int i = 0; i < NN; i++) {
            bad += P.u[i] < 0 || P.v[i] < 0;
        }
    }
    CHECK(bad == 0);
}

TEST(ecology_survives)
{
    petri_init(&P, 5);
    int alive = 0;
    for (int f = 0; f < 2000; f++) {
        petri_frame(&P, 4);
        alive += petri_count(&P, PETRI_GRAZER) > 0;
    }
    printf("  grazers alive in %d of 2000 frames, now %d\n", alive, petri_count(&P, PETRI_GRAZER));
    CHECK(alive >= 1800);
}

int main(void)
{
    RUN(start_population);
    RUN(set_count_clamps);
    RUN(grazers_eat);
    RUN(immigrant_returns);
    RUN(no_immigrant_when_off);
    RUN(agents_wrap);
    RUN(mutagen_marks_dk);
    RUN(seeder_jumps_count);
    RUN(vortex_keeps_range);
    RUN(ecology_survives);
    TEST_MAIN_END();
}
