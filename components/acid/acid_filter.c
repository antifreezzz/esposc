#include "acid_filter.h"

#include <math.h>

#include "acid_math.h"

#define PI_F 3.14159265f

void acid_ladder_reset(acid_ladder_t *f)
{
    for (int i = 0; i < 4; i++) {
        f->s[i] = 0.0f;
    }
}

float acid_ladder_coef(float cutoff_hz, float sr)
{
    float max_fc = 0.45f * sr;
    cutoff_hz = cutoff_hz < 20.0f ? 20.0f : (cutoff_hz > max_fc ? max_fc : cutoff_hz);
    return acid_tan_fine(PI_F * cutoff_hz / sr); // не tanf: зовётся каждые 16 сэмплов, libm на плате дорог
}

void acid_ladder_prep(acid_ladder_coefs_t *c, float g, float res)
{
    res = res < 0.0f ? 0.0f : (res > ACID_LADDER_RES_MAX ? ACID_LADDER_RES_MAX : res);
    c->inv1g = acid_rcp(1.0f + g);
    c->G = g * c->inv1g;
    c->k = 4.0f * res;
    c->comp = 1.0f + 0.5f * c->k;
    float G2 = c->G * c->G;
    c->invd = acid_rcp(1.0f + c->k * G2 * G2);
}

float acid_ladder_run(acid_ladder_t *f, float in, float g, float res)
{
    acid_ladder_coefs_t c;
    acid_ladder_prep(&c, g, res);
    return acid_ladder_tick(f, in, &c);
}

float acid_ladder_process(acid_ladder_t *f, float in, float cutoff_hz, float res, float sr)
{
    return acid_ladder_run(f, in, acid_ladder_coef(cutoff_hz, sr), res);
}
