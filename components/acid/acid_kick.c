#include "acid_kick.h"

#include <math.h>

#include "acid_math.h"

#define PI_F 3.14159265f

void acid_kick_init(acid_kick_t *k, float sr)
{
    k->phase = 0.0f;
    k->pitch_env = 0.0f;
    k->amp_env = 0.0f;
    k->rnd = 0x1F123BB5u;
    k->sr = sr;
    k->inv_sr = 1.0f / sr;
    acid_kick_set(k, ACID_KICK_LOW_HZ, ACID_KICK_SWEEP_HZ, ACID_KICK_PITCH_S, ACID_KICK_AMP_S);
}

void acid_kick_set(acid_kick_t *k, float low_hz, float sweep_hz, float pitch_s, float amp_s)
{
    k->low_hz = low_hz;
    k->sweep_hz = sweep_hz;
    k->hit_low = low_hz;
    k->hit_sweep = sweep_hz;
    k->pitch_coef = expf(-1.0f / (pitch_s * k->sr));
    k->amp_coef = expf(-1.0f / (amp_s * k->sr));
}

void acid_kick_trigger(acid_kick_t *k)
{
    k->phase = 0.0f;
    k->pitch_env = 1.0f;
    k->amp_env = 1.0f;
    k->hit_low = k->low_hz;
    k->hit_sweep = k->sweep_hz;
}

// -1..1 из старших бит LCG.
static float jitter(acid_kick_t *k)
{
    k->rnd = k->rnd * 1664525u + 1013904223u;
    return (float)(int)(k->rnd >> 16 & 0xFFFF) * (1.0f / 32767.5f) - 1.0f;
}

#define KICK_SOFT_PITCH 0.45f // старт огибающей высоты у самого тихого удара
#define KICK_TUNE_JIT 0.005f
#define KICK_SWEEP_JIT 0.05f

void acid_kick_trigger_vel(acid_kick_t *k, float v)
{
    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    k->phase = 0.0f;
    k->pitch_env = KICK_SOFT_PITCH + (1.0f - KICK_SOFT_PITCH) * v;
    k->amp_env = 1.0f;
    k->hit_low = k->low_hz * (1.0f + KICK_TUNE_JIT * jitter(k));
    k->hit_sweep = k->sweep_hz * (1.0f + KICK_SWEEP_JIT * jitter(k));
}

float acid_kick_next(acid_kick_t *k)
{
    if (k->amp_env < 1e-5f) {
        k->amp_env = 0.0f;
        return 0.0f;
    }
    float y = k->amp_env * acid_sin2pi(k->phase); // без libm и деления: на плате они дороже всей бочки
    float hz = k->hit_low + k->hit_sweep * k->pitch_env;
    k->phase += hz * k->inv_sr;
    if (k->phase >= 1.0f) {
        k->phase -= 1.0f;
    }
    k->pitch_env *= k->pitch_coef;
    k->amp_env *= k->amp_coef;
    return y;
}

void acid_kick_run(acid_kick_t *k, float gain, float *out, int n)
{
    for (int i = 0; i < n; i++) {
        out[i] = gain * acid_kick_next(k);
    }
}
