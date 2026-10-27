#include "acid_seq.h"

#define A ACID_ACCENT
#define S ACID_SLIDE
#define R ACID_REST

// Оригинальный паттерн около ля минора. Слайд на шаге 15 уводит обратно в шаг 0.
const acid_step_t acid_pattern_default[16] = {
    {45, A}, {45, 0}, {48, S}, {52, 0},
    {0, R},  {45, A}, {43, 0}, {57, S},
    {45, 0}, {0, R},  {50, A}, {48, 0},
    {40, 0}, {45, A}, {43, 0}, {47, S},
};
