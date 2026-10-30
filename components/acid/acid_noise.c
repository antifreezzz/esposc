#include "acid_noise.h"

uint32_t acid_rand(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

float acid_noise(uint32_t *state)
{
    // Старшие 24 бита -> [-1, 1].
    return (float)(acid_rand(state) >> 8) * (2.0f / 16777215.0f) - 1.0f;
}

void acid_svf_bp_hp(acid_svf_t *f, float in, float g, float k, float *bp, float *hp)
{
    if (g != f->g || k != f->k || f->a1 == 0.0f) { // a1 == 0 - ещё не считали (a1 всегда > 0)
        f->g = g;
        f->k = k;
        f->a1 = 1.0f / (1.0f + g * (g + k));
        f->a2 = g * f->a1;
        f->a3 = g * f->a2;
    }
    float a1 = f->a1, a2 = f->a2, a3 = f->a3;
    float v3 = in - f->ic2;
    float v1 = a1 * f->ic1 + a2 * v3;
    float v2 = f->ic2 + a2 * f->ic1 + a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    *bp = v1;
    *hp = in - k * v1 - v2;
}
