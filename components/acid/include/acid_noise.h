#pragma once
#include <stdint.h>

// xorshift32: state != 0.
uint32_t acid_rand(uint32_t *state);
// Белый шум в [-1, 1].
float acid_noise(uint32_t *state);

// TPT state variable filter. g = tan(pi*fc/sr), k = 1/Q. Коэффициенты помнит для последних g, k:
// деление на каждом сэмпле было дорогим, а g и k меняются только ручками. Нулевое состояние валидно.
typedef struct {
    float ic1, ic2;
    float g, k, a1, a2, a3;
} acid_svf_t;

void acid_svf_bp_hp(acid_svf_t *f, float in, float g, float k, float *bp, float *hp);
