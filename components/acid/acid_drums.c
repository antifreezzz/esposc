#include "acid_drums.h"

#include <math.h>

#include "acid_math.h"

#define PI_F 3.14159265f
#define HAT_HZ ACID_HAT_HZ
#define HAT_CLOSED_S ACID_HAT_CLOSED_S
#define HAT_OPEN_S ACID_HAT_OPEN_S
#define CLAP_HZ ACID_CLAP_HZ
#define CLAP_Q 1.5f
#define CLAP_BURST_S 0.003f
#define CLAP_TAIL_S ACID_CLAP_TAIL_S
#define CLAP_BURSTS 3
#define SILENT 1e-5f
#define SNARE_TUNE_HZ ACID_SNARE_TUNE_HZ
#define SNARE_SNAPPY ACID_SNARE_SNAPPY
#define SNARE_DECAY_S ACID_SNARE_DECAY_S
#define SNARE_NOISE_HZ 2000.0f
#define SNARE_PITCH_S 0.02f

static float coef(float tau_s, float sr)
{
    return expf(-1.0f / (tau_s * sr));
}

void acid_hat_init(acid_hat_t *h, float sr)
{
    h->rng = 0x9e3779b9u;
    h->svf = (acid_svf_t){0};
    h->k = 1.4f;
    h->env = 0.0f;
    h->sr = sr;
    h->coef = h->coef_closed = 0.0f;
    h->coef_open = 1.0f;
    acid_hat_set(h, HAT_HZ, HAT_CLOSED_S, HAT_OPEN_S);
    h->coef = h->coef_closed;
}

void acid_hat_set(acid_hat_t *h, float tone_hz, float closed_s, float open_s)
{
    int was_open = h->coef == h->coef_open;
    h->g = tanf(PI_F * tone_hz / h->sr);
    h->coef_closed = coef(closed_s, h->sr);
    h->coef_open = coef(open_s, h->sr);
    h->coef = was_open ? h->coef_open : h->coef_closed;
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
    c->k = 1.0f / CLAP_Q;
    c->env = 0.0f;
    c->burst_coef = coef(CLAP_BURST_S, sr);
    c->t = -1;
    c->burst_step = (int)(0.01f * sr);
    c->sr = sr;
    acid_clap_set(c, CLAP_HZ, CLAP_TAIL_S);
}

void acid_clap_set(acid_clap_t *c, float tone_hz, float tail_s)
{
    c->g = tanf(PI_F * tone_hz / c->sr);
    c->tail_coef = coef(tail_s, c->sr);
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

void acid_snare_init(acid_snare_t *s, float sr)
{
    s->rng = 0x27d4eb2du;
    s->svf = (acid_svf_t){0};
    s->g = tanf(PI_F * SNARE_NOISE_HZ / sr);
    s->k = 1.4f;
    s->phase = 0.0f;
    s->pitch_env = 0.0f;
    s->pitch_coef = coef(SNARE_PITCH_S, sr);
    s->body_env = s->noise_env = 0.0f;
    s->sr = sr;
    s->inv_sr = 1.0f / sr;
    acid_snare_set(s, SNARE_TUNE_HZ, SNARE_SNAPPY, SNARE_DECAY_S);
}

void acid_snare_set(acid_snare_t *s, float tune_hz, float snappy, float decay_s)
{
    s->tune_hz = tune_hz;
    s->snappy = snappy;
    s->noise_coef = coef(decay_s, s->sr);
    s->body_coef = coef(decay_s * 0.5f, s->sr); // корпус короче пружин
}

void acid_snare_trigger(acid_snare_t *s)
{
    s->phase = 0.0f;
    s->pitch_env = 1.0f;
    s->body_env = s->noise_env = 1.0f;
}

float acid_snare_next(acid_snare_t *s)
{
    if (s->body_env < SILENT && s->noise_env < SILENT) {
        s->body_env = s->noise_env = 0.0f;
        return 0.0f;
    }
    float body = acid_sin2pi(s->phase) * s->body_env; // без libm и деления
    s->phase += s->tune_hz * (1.0f + 0.5f * s->pitch_env) * s->inv_sr;
    if (s->phase >= 1.0f) {
        s->phase -= 1.0f;
    }
    float bp, hp;
    acid_svf_bp_hp(&s->svf, acid_noise(&s->rng), s->g, s->k, &bp, &hp);
    float y = body * (1.0f - 0.7f * s->snappy) + hp * s->noise_env * s->snappy;
    s->pitch_env *= s->pitch_coef;
    s->body_env *= s->body_coef;
    s->noise_env *= s->noise_coef;
    return y;
}

// Куски голосов: те же сэмплы, что n вызовов next, но без вызова из другого файла на каждый.
void acid_hat_run(acid_hat_t *h, float gain, float *out, int n)
{
    for (int i = 0; i < n; i++) {
        out[i] = gain * acid_hat_next(h);
    }
}

void acid_clap_run(acid_clap_t *c, float gain, float *out, int n)
{
    for (int i = 0; i < n; i++) {
        out[i] = gain * acid_clap_next(c);
    }
}

void acid_snare_run(acid_snare_t *s, float gain, float *out, int n)
{
    for (int i = 0; i < n; i++) {
        out[i] = gain * acid_snare_next(s);
    }
}
