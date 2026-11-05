#pragma once
#include <stdint.h>

// Чашка Петри: реакция Грея-Скотта на торе 96x96 и её жители. Чистый C без выделений памяти: общий для панели
// (считает чашку) и хост-тестов. Спека: docs/superpowers/specs/2026-11-05-petri-design.md.

#define PETRI_N 96
#define PETRI_Q 32767 // 1.0 в Q15
#define PETRI_PRESETS 6

enum { PETRI_GRAZER, PETRI_SEEDER, PETRI_MUTAGEN, PETRI_VORTEX, PETRI_KINDS };
#define PETRI_AGENTS 27 // 16 пожирателей + 4 сеятеля + 4 мутагена + 3 вихря
#define PETRI_VR 9      // радиус вихря
#define PETRI_VP (2 * PETRI_VR + 1)

typedef struct {
    float x, y, h, energy; // клетки, курс в радианах
    uint8_t kind;
    int8_t sign; // мутаген и вихрь: знак действия
    uint16_t t;  // кадров с рождения
} petri_agent_t;

typedef struct {
    int16_t u[PETRI_N * PETRI_N], v[PETRI_N * PETRI_N]; // Q15
    int8_t dk[PETRI_N * PETRI_N];                       // поправка к k от мутагенов, шаг 0.012/127
    int16_t row[3][2][PETRI_N];                         // старые строки y-1, y и 0 на время шага (u, v)
    int16_t f_q, k_q;                                   // F и k в Q15
    uint32_t rng;
    // Жители.
    petri_agent_t a[PETRI_AGENTS];
    int n;
    uint8_t want[PETRI_KINDS]; // ручки численности (у пожирателей - потолок)
    float appetite, mutation;
    int immigrant_t; // кадров без пожирателей
    float jump;      // импульс прыжка сеятеля, x0.68 за кадр
    uint8_t jumps;   // счётчик прыжков (растёт по кругу)
    int8_t flip;
    float patch[2][PETRI_VP * PETRI_VP]; // повёрнутый патч вихря (u, v)
} petri_t;

uint32_t petri_rand(uint32_t *s); // xorshift32
float petri_randf(uint32_t *s);   // [0, 1)

// U = 1, V = 0, Δk = 0, пресет 0, капли посева; жители 3, 1, 2, 1, аппетит 0.15, сила мутагенов 1.
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

// Численность типа: зажать в максимум, лишних убрать с конца, недостающих родить в случайном месте.
void petri_set_count(petri_t *p, int kind, int n);
int petri_count(const petri_t *p, int kind);
// Кадр чашки (20 Гц): steps шагов реакции, потом жители и затухание Δk.
void petri_frame(petri_t *p, int steps);
