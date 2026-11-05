// Зашитые таблицы чашки Петри для синта без панели: та же библиотека components/petri, что на панели.
// Сборка вместе с хост-тестами (цель petri_bake), запуск:
//   host/build/petri_bake > components/acid/acid_petri_tables.c
#include <stdio.h>

#include "petri.h"

#define TABLES 32
#define EVERY 10 // кадров между таблицами: 0.5 с при 20 Гц
#define STEPS 4

static int16_t U[PETRI_N * PETRI_N], V[PETRI_N * PETRI_N];
static int8_t DK[PETRI_N * PETRI_N];
static petri_t P;

int main(void)
{
    static const uint8_t MODE[4] = {PETRI_HEIGHT, PETRI_FOLD, PETRI_EDGE, PETRI_VOID};
    static const uint8_t SHAPE[4] = {PETRI_CIRCLE, PETRI_ROSE, PETRI_EIGHT, PETRI_EPI};
    petri_bind(&P, U, V, DK);
    petri_init(&P, 1);
    for (int s = 0; s < 600; s++) {
        petri_step(&P);
    }
    P.contrast = 0.3f;
    printf("// Сгенерировано tools/petri_bake: %d таблиц живой чашки (Митоз, seed 1, раз в 0.5 с). Не править руками.\n",
           TABLES);
    printf("#include \"acid_petri.h\"\n\nconst acid_petri_frame_t acid_petri_baked[%d] = {\n", TABLES);
    for (int k = 0; k < TABLES; k++) {
        for (int f = 0; f < EVERY; f++) {
            petri_frame(&P, STEPS);
        }
        P.mode = MODE[k % 4];
        P.shape = SHAPE[k % 4];
        int16_t tab[PETRI_TABLE];
        int8_t mods[PETRI_MODS];
        petri_capture(&P, tab, mods);
        printf("    {{");
        for (int i = 0; i < PETRI_TABLE; i++) {
            printf("%s%d", i ? (i % 16 ? ", " : ",\n      ") : "", tab[i]);
        }
        printf("},\n     {%d, %d, %d, %d, %d, %d},\n     %d},\n", mods[0], mods[1], mods[2], mods[3], mods[4], mods[5],
               P.jumps);
    }
    printf("};\nconst int acid_petri_nbaked = %d;\n", TABLES);
    return 0;
}
