#include "acid_kick.h"

#include <math.h>

#define PI_F 3.14159265f
#define KICK_LOW_HZ 50.0f
#define KICK_SWEEP_HZ 100.0f // старт 150 Гц
#define KICK_PITCH_S 0.03f
#define KICK_AMP_S 0.08f

void acid_kick_init(acid_kick_t *k, float sr)
{
    k->phase = 0.0f;
    k->pitch_env = 0.0f;
    k->amp_env = 0.0f;
    k->sr = sr;
    acid_kick_set(k, KICK_LOW_HZ, KICK_SWEEP_HZ, KICK_PITCH_S, KICK_AMP_S);
}

void acid_kick_set(acid_kick_t *k, float low_hz, float sweep_hz, float pitch_s, float amp_s)
{
    k->low_hz = low_hz;
    k->sweep_hz = sweep_hz;
    k->pitch_coef = expf(-1.0f / (pitch_s * k->sr));
    k->amp_coef = expf(-1.0f / (amp_s * k->sr));
}

void acid_kick_trigger(acid_kick_t *k)
{
    k->phase = 0.0f;
    k->pitch_env = 1.0f;
    k->amp_env = 1.0f;
}

float acid_kick_next(acid_kick_t *k)
{
    if (k->amp_env < 1e-5f) {
        k->amp_env = 0.0f;
        return 0.0f;
    }
    float y = k->amp_env * sinf(2.0f * PI_F * k->phase);
    float hz = k->low_hz + k->sweep_hz * k->pitch_env;
    k->phase += hz / k->sr;
    if (k->phase >= 1.0f) {
        k->phase -= 1.0f;
    }
    k->pitch_env *= k->pitch_coef;
    k->amp_env *= k->amp_coef;
    return y;
}
