#pragma once
#include <stdint.h>

// Чашка Петри: реакция Грея-Скотта на торе 96x96 и её жители. Чистый C без выделений памяти: общий для панели
// (считает чашку) и хост-тестов. Спека: docs/superpowers/specs/2026-11-05-petri-design.md.

#define PETRI_N 96
#define PETRI_Q 32767 // 1.0 в Q15
#define PETRI_PRESETS 6

typedef struct {
    int16_t u[PETRI_N * PETRI_N], v[PETRI_N * PETRI_N]; // Q15
    int8_t dk[PETRI_N * PETRI_N];                       // поправка к k от мутагенов, шаг 0.012/127
    int16_t row[3][2][PETRI_N];                         // старые строки y-1, y и 0 на время шага (u, v)
    int16_t f_q, k_q;                                   // F и k в Q15
    uint32_t rng;
} petri_t;

uint32_t petri_rand(uint32_t *s); // xorshift32
float petri_randf(uint32_t *s);   // [0, 1)

// U = 1, V = 0, Δk = 0, пресет 0 и капли посева.
void petri_init(petri_t *p, uint32_t seed);
// U = 1, V = 0 и 12 капель радиусом 2-5 в случайных местах.
void petri_reseed(petri_t *p);
void petri_set_fk(petri_t *p, float f, float k);
void petri_preset(int i, float *f, float *k);
// Капля реагента: U = 0.5, V = 0.25..0.30 в круге радиуса r, с переносом через край.
void petri_drop(petri_t *p, float cx, float cy, float r);
// Один шаг реакции (dt = 1) на месте.
void petri_step(petri_t *p);
// V билинейно в точке с переносом через край, 0..1.
float petri_v(const petri_t *p, float x, float y);
