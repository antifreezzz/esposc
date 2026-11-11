#include "acid_env.h"

#include <math.h>

// Коэффициент однополюсника с постоянной времени tau_s.
static float pole(float tau_s, float sr)
{
    return expf(-1.0f / (tau_s * sr));
}

void acid_decay_init(acid_decay_t *e, float decay_s, float sr)
{
    e->value = 0.0f;
    acid_decay_set(e, decay_s, sr);
}

void acid_decay_set(acid_decay_t *e, float decay_s, float sr)
{
    e->coef = pole(decay_s, sr);
}

void acid_decay_trigger(acid_decay_t *e, float level)
{
    e->value = level;
}

void acid_amp_init(acid_amp_t *e, float attack_s, float release_s, float sr)
{
    e->value = 0.0f;
    e->att = pole(attack_s, sr);
    e->rel = pole(release_s, sr);
    e->gate = 0;
}

