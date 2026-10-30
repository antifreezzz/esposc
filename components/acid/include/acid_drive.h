#pragma once
#include "acid_noise.h"

// Перегруз синта: от тёплого кислотного до бензопилы. Низ ниже ~120 Гц обходит перегруз и остаётся чистым.
// Цепочка: асимметричная первая ступень, резонансный полосовой на гармонике ноты (тон), жёсткая вторая ступень
// - обе на двойной частоте, обратная связь вокруг полосового (вой). Свист сидит на гармонике ноты,
// поэтому едет за ней на слайде. Без деления на каждом сэмпле.
#define ACID_DRIVE_OS_HIST 8

typedef struct {
    float sr;
    int drive, tone, howl;  // ручки 0..127; drive 0 - выключен
    float pre, post;        // усиление первой и второй ступени
    float harm;             // гармоника ноты для полосового
    float howl_g;           // обратная связь
    float makeup;
    float g_x;              // коэффициент раздела саба
    float g_bp;             // коэффициент полосового на управляющей частоте
    acid_svf_t xover, bp, post_hp;
    float fb;               // ограниченный выход полосового прошлого сэмпла двойной частоты (петля воя)
    float env, env_rel;     // огибающая входа: вой глохнет после ноты
    float t_bias;           // tanh смещения первой ступени: тишина на входе - ноль на выходе
    float up[ACID_DRIVE_OS_HIST]; // история входа повышения частоты
    float dn[ACID_DRIVE_OS_HIST]; // нечётные сэмплы на двойной частоте
    float dn_even[3];       // чётные сэмплы на двойной частоте (задержка)
} acid_drive_t;

void acid_drive_init(acid_drive_t *d, float sr);
// Ручки 0..127 как на экране BSP. Деление тут можно: зовётся при повороте ручки.
void acid_drive_set(acid_drive_t *d, int drive, int tone, int howl);
static inline int acid_drive_on(const acid_drive_t *d)
{
    return d->drive > 0;
}
// Высота ноты, Гц: на управляющей частоте.
void acid_drive_pitch(acid_drive_t *d, float note_hz);
float acid_drive_run(acid_drive_t *d, float x);
