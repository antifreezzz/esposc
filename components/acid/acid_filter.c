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
    return tanf(PI_F * cutoff_hz / sr);
}

float acid_ladder_run(acid_ladder_t *f, float in, float g, float res)
{
    res = res < 0.0f ? 0.0f : (res > 1.0f ? 1.0f : res);
    float G = g / (1.0f + g);
    float k = 4.0f * res;

    // Решаем мгновенную обратную связь: y4 = G^4 * u + S.
    float G2 = G * G;
    float S = (G2 * G * f->s[0] + G2 * f->s[1] + G * f->s[2] + f->s[3]) / (1.0f + g);
    // Частичная компенсация провала в полосе пропускания на высоком резонансе.
    float x = in * (1.0f + 0.5f * k);
    float u = acid_tanh((x - k * S) / (1.0f + k * G2 * G2));

    for (int i = 0; i < 4; i++) {
        float v = (u - f->s[i]) * G;
        float y = v + f->s[i];
        f->s[i] = y + v;
        u = y;
    }
    return u;
}

float acid_ladder_process(acid_ladder_t *f, float in, float cutoff_hz, float res, float sr)
{
    return acid_ladder_run(f, in, acid_ladder_coef(cutoff_hz, sr), res);
}
