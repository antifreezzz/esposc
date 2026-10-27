#include "acid_filter.h"

#include <math.h>

#define PI_F 3.14159265f

void acid_ladder_reset(acid_ladder_t *f)
{
    for (int i = 0; i < 4; i++) {
        f->s[i] = 0.0f;
    }
}

float acid_ladder_process(acid_ladder_t *f, float in, float cutoff_hz, float res, float sr)
{
    float max_fc = 0.45f * sr;
    cutoff_hz = cutoff_hz < 20.0f ? 20.0f : (cutoff_hz > max_fc ? max_fc : cutoff_hz);
    res = res < 0.0f ? 0.0f : (res > 1.0f ? 1.0f : res);

    float g = tanf(PI_F * cutoff_hz / sr);
    float G = g / (1.0f + g);
    float k = 4.0f * res;

    // Решаем мгновенную обратную связь: y4 = G^4 * u + S.
    float S = (G * G * G * f->s[0] + G * G * f->s[1] + G * f->s[2] + f->s[3]) / (1.0f + g);
    // Частичная компенсация провала в полосе пропускания на высоком резонансе.
    float x = in * (1.0f + 0.5f * k);
    float u = tanhf((x - k * S) / (1.0f + k * G * G * G * G));

    for (int i = 0; i < 4; i++) {
        float v = (u - f->s[i]) * G;
        float y = v + f->s[i];
        f->s[i] = y + v;
        u = y;
    }
    return u;
}
