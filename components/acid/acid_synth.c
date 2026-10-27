#include "acid_synth.h"

#include <math.h>

#define BPM 130.0f
#define RES 0.85f
#define FENV_NORMAL_HZ 3000.0f
#define FENV_ACCENT_HZ 5000.0f
#define FENV_NORMAL_S 0.25f
#define FENV_ACCENT_S 0.12f
#define ACCENT_GAIN 1.4f
#define GLIDE_S 0.06f
#define LFO_MIN_HZ 200.0f
#define LFO_MAX_HZ 1200.0f
#define LFO_BARS 12.0f
#define DRIVE 0.8f
#define OUT_PEAK 0.5f // -6 dBFS
#define PI_F 3.14159265f

void acid_synth_init(acid_synth_t *s, float sr)
{
    s->sr = sr;
    s->osc = (acid_osc_t){0};
    acid_ladder_reset(&s->flt);
    acid_decay_init(&s->fenv, FENV_NORMAL_S, sr);
    acid_amp_init(&s->amp, 0.003f, 0.01f, sr);
    s->glide_coef = expf(-1.0f / (GLIDE_S * sr));
    s->pitch_log2 = s->target_log2 = log2f(110.0f);
    s->gliding = 0;
    s->accent_gain = 1.0f;
    s->fenv_amount = FENV_NORMAL_HZ;
    s->res = RES;
    s->wave = 0;
    s->lfo_phase = 0.0f;
    float bar_s = 4.0f * 60.0f / BPM;
    s->lfo_inc = 1.0f / (LFO_BARS * bar_s * sr);
    acid_synth_set_pattern(s, acid_pattern_default, 16);
}

void acid_synth_set_pattern(acid_synth_t *s, const acid_step_t *steps, int len)
{
    acid_seq_init(&s->seq, steps, len, BPM, s->sr);
}

float acid_synth_pitch_hz(const acid_synth_t *s)
{
    return exp2f(s->pitch_log2);
}

static void on_step(acid_synth_t *s, int index)
{
    const acid_step_t *cur = &s->seq.steps[index];
    if (cur->flags & ACID_REST) {
        s->gliding = 0;
        return;
    }
    const acid_step_t *prev = &s->seq.steps[(index + s->seq.len - 1) % s->seq.len];
    // Слайд только если предыдущий шаг реально звучит: на самом первом шаге amp = 0.
    int slide_in = (prev->flags & ACID_SLIDE) &&
                   !(prev->flags & ACID_REST) && s->amp.value > 0.0f;
    s->target_log2 = log2f(acid_midi_to_hz(cur->note));
    if (slide_in) {
        s->gliding = 1;
        return;
    }
    s->gliding = 0;
    s->pitch_log2 = s->target_log2;
    int accent = cur->flags & ACID_ACCENT;
    acid_decay_set(&s->fenv, accent ? FENV_ACCENT_S : FENV_NORMAL_S, s->sr);
    acid_decay_trigger(&s->fenv, 1.0f);
    s->fenv_amount = accent ? FENV_ACCENT_HZ : FENV_NORMAL_HZ;
    s->accent_gain = accent ? ACCENT_GAIN : 1.0f;
}

void acid_synth_render(acid_synth_t *s, float *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int step = acid_seq_tick(&s->seq);
        if (step >= 0) {
            on_step(s, step);
        }
        acid_amp_gate(&s->amp, acid_seq_gate(&s->seq));

        if (s->gliding) {
            s->pitch_log2 = s->target_log2 + (s->pitch_log2 - s->target_log2) * s->glide_coef;
        }
        acid_osc_set_freq(&s->osc, exp2f(s->pitch_log2), s->sr);
        float x = s->wave ? acid_osc_square(&s->osc) : acid_osc_saw(&s->osc);

        // LFO по экспоненте между LFO_MIN_HZ и LFO_MAX_HZ, синусоидой.
        float lfo = 0.5f - 0.5f * cosf(2.0f * PI_F * s->lfo_phase);
        s->lfo_phase += s->lfo_inc;
        if (s->lfo_phase >= 1.0f) {
            s->lfo_phase -= 1.0f;
        }
        float base = LFO_MIN_HZ * powf(LFO_MAX_HZ / LFO_MIN_HZ, lfo);
        float cutoff = base + s->fenv_amount * acid_decay_next(&s->fenv);

        float y = acid_ladder_process(&s->flt, x, cutoff, s->res, s->sr);
        y *= acid_amp_next(&s->amp) * s->accent_gain;
        out[i] = OUT_PEAK * tanhf(DRIVE * y);
    }
}
