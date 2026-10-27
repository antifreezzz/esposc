#include "acid_drums.h"

#include <math.h>

#define PI_F 3.14159265f
#define HAT_HZ 7000.0f
#define HAT_CLOSED_S 0.02f
#define HAT_OPEN_S 0.2f
#define CLAP_HZ 1200.0f
#define CLAP_Q 1.5f
#define CLAP_BURST_S 0.003f
#define CLAP_TAIL_S 0.1f
#define CLAP_BURSTS 3
#define SILENT 1e-5f

static float coef(float tau_s, float sr)
{
    return expf(-1.0f / (tau_s * sr));
}

void acid_hat_init(acid_hat_t *h, float sr)
{
    h->rng = 0x9e3779b9u;
    h->svf = (acid_svf_t){0};
    h->g = tanf(PI_F * HAT_HZ / sr);
    h->k = 1.4f;
    h->env = 0.0f;
    h->coef_closed = coef(HAT_CLOSED_S, sr);
    h->coef_open = coef(HAT_OPEN_S, sr);
    h->coef = h->coef_closed;
}

void acid_hat_trigger(acid_hat_t *h, int open)
{
    h->env = 1.0f;
    h->coef = open ? h->coef_open : h->coef_closed;
}

float acid_hat_next(acid_hat_t *h)
{
    if (h->env < SILENT) {
        h->env = 0.0f;
        return 0.0f;
    }
    float bp, hp;
    acid_svf_bp_hp(&h->svf, acid_noise(&h->rng), h->g, h->k, &bp, &hp);
    float y = hp * h->env;
    h->env *= h->coef;
    return y;
}

void acid_clap_init(acid_clap_t *c, float sr)
{
    c->rng = 0x85ebca6bu;
    c->svf = (acid_svf_t){0};
    c->g = tanf(PI_F * CLAP_HZ / sr);
    c->k = 1.0f / CLAP_Q;
    c->env = 0.0f;
    c->burst_coef = coef(CLAP_BURST_S, sr);
    c->tail_coef = coef(CLAP_TAIL_S, sr);
    c->t = -1;
    c->burst_step = (int)(0.01f * sr);
}

void acid_clap_trigger(acid_clap_t *c)
{
    c->t = 0;
}

float acid_clap_next(acid_clap_t *c)
{
    if (c->t < 0) {
        return 0.0f;
    }
    int last_burst = (CLAP_BURSTS - 1) * c->burst_step;
    if (c->t <= last_burst && c->t % c->burst_step == 0) {
        c->env = 1.0f;
    }
    float bp, hp;
    acid_svf_bp_hp(&c->svf, acid_noise(&c->rng), c->g, c->k, &bp, &hp);
    float y = bp * c->env;
    // Всплески гаснут быстро, последний переходит в длинный хвост.
    c->env *= c->t < last_burst ? c->burst_coef : c->tail_coef;
    c->t++;
    if (c->t > last_burst && c->env < SILENT) {
        c->env = 0.0f;
        c->t = -1;
    }
    return y;
}
