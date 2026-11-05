#pragma once
#include <math.h>
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

#define PETRI_TABLE 256 // точек таблицы волны
#define PETRI_MODS 6    // модуляции: pop, dk, vort, energy, jump, dens
enum { PETRI_HEIGHT, PETRI_EDGE, PETRI_FOLD, PETRI_VOID, PETRI_MODES };
enum { PETRI_CIRCLE, PETRI_EIGHT, PETRI_LISS32, PETRI_ROSE, PETRI_EPI, PETRI_SHAPES };

typedef struct {
    float x, y, h, energy; // клетки, курс в радианах
    uint8_t kind;
    int8_t sign; // мутаген и вихрь: знак действия
    uint16_t t;  // кадров с рождения
} petri_agent_t;

typedef struct {
    // Сетки PETRI_N x PETRI_N - память вызывающего (petri_bind): на панели внутренняя RAM фрагментирована,
    // одним куском вся чашка (~51 КБ) не выделяется, по отдельности (18 + 18 + 9 КБ) - да.
    int16_t *u, *v; // Q15
    int8_t *dk;     // поправка к k от мутагенов, шаг 0.012/127
    // Рабочие строки шага (u, v): старые копии строк y-1 и 0, текущая строка с краями тора, суммы столбцов 3x1,
    // лапласиан. Простые проходы по строке вместо одного цикла с 9 указателями: тот не влезал в регистры Xtensa.
    int16_t prev[2][PETRI_N], first[2][PETRI_N], mid[2][PETRI_N + 2];
    int32_t col[2][PETRI_N + 2], lap[2][PETRI_N];
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
    // Орбита и рельеф.
    uint8_t mode, shape;
    float contrast; // 0..1, c = 40^x
    float radius;   // 2..48 клеток
    float drift;    // скорость дрейфа центра, клеток в секунду
    uint8_t follow; // центр едет за первым пожирателем
    float cx, cy, ang;
} petri_t;

// Перенос координаты на тор в [0, PETRI_N). Без поправки -1e-6 округляется ровно до 96.
static inline float petri_wrapf(float a)
{
    float r = a - PETRI_N * floorf(a * (1.0f / PETRI_N));
    return r >= PETRI_N ? 0 : r;
}

uint32_t petri_rand(uint32_t *s); // xorshift32
float petri_randf(uint32_t *s);   // [0, 1)

// Привязать сетки: u и v по PETRI_N * PETRI_N int16, dk - int8. До petri_init; init их сохраняет.
void petri_bind(petri_t *p, int16_t *u, int16_t *v, int8_t *dk);
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
// Кадр чашки (20 Гц): steps шагов реакции, жители, затухание Δk, сдвиг центра орбиты на 50 мс.
void petri_frame(petri_t *p, int steps);

// Точка орбиты для угла a (0..2pi), без переноса через край.
void petri_orbit_point(const petri_t *p, float a, float *x, float *y);
// Центр орбиты: дрейф по эллипсу вокруг середины чашки или за первым пожирателем (постоянная 0.33 с).
void petri_move_center(petri_t *p, float dt);
// Один оборот орбиты: таблица рельефа без постоянной составляющей и модуляции ±127 = ±1.
void petri_capture(const petri_t *p, int16_t table[PETRI_TABLE], int8_t mods[PETRI_MODS]);
