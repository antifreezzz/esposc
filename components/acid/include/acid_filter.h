#pragma once

#include "acid_math.h"

// 4-полюсный ladder ФНЧ (TPT / zero-delay feedback) с мягким насыщением.
typedef struct {
    float s[4];
} acid_ladder_t;

void acid_ladder_reset(acid_ladder_t *f);

// Коэффициент g = tan(pi*fc/sr), cutoff ограничен [20, 0.45*sr]. Дорогой: считать
// на управляющей частоте, а не на каждом сэмпле.
float acid_ladder_coef(float cutoff_hz, float sr);

// Один сэмпл с готовым коэффициентом. res в [0, ACID_LADDER_RES_MAX]: до 1 звенит и затухает, выше
// заводится сам (свист), амплитуду держит tanh на входе.
#define ACID_LADDER_RES_MAX 1.15f
float acid_ladder_run(acid_ladder_t *f, float in, float g, float res);

// Величины сэмпла, зависящие только от g и res: считать вместе с g на управляющей частоте, в сэмпле - только
// умножения (у FPU S3 нет деления, программный __divsf3 на каждом сэмпле стоил голосу 303 больше половины цены).
typedef struct {
    float G, inv1g, k, comp, invd;
} acid_ladder_coefs_t;

void acid_ladder_prep(acid_ladder_coefs_t *c, float g, float res);

// Сэмпл с готовыми величинами. Инлайн: вызов из другого файла на каждый сэмпл дорог.
static inline float acid_ladder_tick(acid_ladder_t *f, float in, const acid_ladder_coefs_t *c)
{
    float G = c->G, G2 = G * G;
    // Решаем мгновенную обратную связь: y4 = G^4 * u + S.
    float S = (G2 * G * f->s[0] + G2 * f->s[1] + G * f->s[2] + f->s[3]) * c->inv1g;
    // Частичная компенсация провала в полосе пропускания на высоком резонансе.
    float u = acid_tanh((in * c->comp - c->k * S) * c->invd);
    for (int i = 0; i < 4; i++) {
        float v = (u - f->s[i]) * G;
        float y = v + f->s[i];
        f->s[i] = y + v;
        u = y;
    }
    return u;
}

// То же с расчётом коэффициента на месте (удобно в тестах).
float acid_ladder_process(acid_ladder_t *f, float in, float cutoff_hz, float res, float sr);
