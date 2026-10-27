#include "acid_synth.h"

#include <math.h>
#include <string.h>

#include "acid_math.h"

#define BPM 130.0f
#define RES 0.85f
#define FENV_NORMAL_HZ 3000.0f
#define FENV_ACCENT_HZ 5000.0f
#define FENV_NORMAL_S 0.25f
#define FENV_ACCENT_S 0.12f
#define ACCENT_GAIN 1.4f
#define GLIDE_S 0.06f
#define FIXED_CUTOFF_HZ 500.0f // база среза в режиме фиксированного паттерна
#define HAT_GAIN 0.25f
#define CLAP_GAIN 0.6f
#define DRIVE 0.8f
#define OUT_PEAK 0.5f // -6 dBFS
#define KICK_GAIN 1.45f // +10% пика бочки на выходе после tanh
#define CTRL_BLOCK 16 // управляющая частота sr/16: высота, автоматизация, коэффициент фильтра

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
    s->ctrl_left = 0;
    s->g = 0.0f;
    s->last_flags = 0;
    acid_kick_init(&s->kick, sr);
    acid_hat_init(&s->hat, sr);
    acid_clap_init(&s->clap, sr);
    s->kick_on = 1;

    s->arr_on = 1;
    s->started = 0;
    s->bar = 0;
    s->cycle = 0;
    for (int p = 0; p < ACID_PAT_COUNT; p++) {
        memcpy(s->mut[p], acid_patterns[p], sizeof s->mut[p]);
    }
    s->sec = acid_arr_section(0);
    memcpy(s->bar_steps, s->mut[acid_arr_pattern(0)], sizeof s->bar_steps);
    acid_seq_init(&s->seq, s->bar_steps, 16, BPM, sr);
}

void acid_synth_set_pattern(acid_synth_t *s, const acid_step_t *steps, int len)
{
    s->arr_on = 0;
    acid_seq_init(&s->seq, steps, len, BPM, s->sr);
}

// Начало такта в режиме трека: следующий такт, круг, мутация, параметры секции.
static void on_bar(acid_synth_t *s)
{
    if (s->started) {
        s->bar++;
        if (s->bar == ACID_TRACK_BARS) {
            s->bar = 0;
            s->cycle++;
            // Первый круг играет оригинал, дальше каждый круг со своей мутацией.
            for (int p = 0; p < ACID_PAT_COUNT; p++) {
                acid_mutate(s->mut[p], acid_patterns[p], 16, (uint32_t)s->cycle);
            }
        }
    }
    s->started = 1;
    s->sec = acid_arr_section(s->bar);
    memcpy(s->bar_steps, s->mut[acid_arr_pattern(s->bar)], sizeof s->bar_steps);
    s->wave = s->sec->wave;
}

// Ударные шага по битам секции.
static void drums_step(acid_synth_t *s, int step)
{
    uint8_t d = s->sec->drums;
    if ((d & ACID_DR_KICK) && step % 4 == 0) {
        acid_kick_trigger(&s->kick);
    }
    if (d & ACID_DR_HAT) {
        if ((d & ACID_DR_OHAT) && step % 4 == 2) {
            acid_hat_trigger(&s->hat, 1);
        } else if (step % 2 == 0) {
            acid_hat_trigger(&s->hat, 0);
        }
    }
    if ((d & ACID_DR_CLAP) && (step == 4 || step == 12)) {
        acid_clap_trigger(&s->clap);
    }
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
    // Слайд только если предыдущий шаг реально звучит: на самом первом шаге amp = 0.
    int slide_in = (s->last_flags & ACID_SLIDE) && !(s->last_flags & ACID_REST) &&
                   s->amp.value > 0.0f;
    int note = cur->note + (s->arr_on ? s->sec->transpose : 0);
    s->target_log2 = log2f(acid_midi_to_hz(note));
    int accent = cur->flags & ACID_ACCENT;
    // Акцент громкости действует и на слайде, огибающие при этом не перезапускаются.
    s->accent_gain = accent ? ACCENT_GAIN : 1.0f;
    if (slide_in) {
        s->gliding = 1;
        return;
    }
    s->gliding = 0;
    s->pitch_log2 = s->target_log2;
    acid_decay_set(&s->fenv, accent ? FENV_ACCENT_S : FENV_NORMAL_S, s->sr);
    acid_decay_trigger(&s->fenv, 1.0f);
    s->fenv_amount = (accent ? FENV_ACCENT_HZ : FENV_NORMAL_HZ) * (s->arr_on ? s->sec->env_scale : 1.0f);
}

// Пересчёт дорогих величин раз в CTRL_BLOCK сэмплов.
static float control_update(acid_synth_t *s)
{
    acid_osc_set_freq(&s->osc, exp2f(s->pitch_log2), s->sr);

    float base = FIXED_CUTOFF_HZ;
    if (s->arr_on) {
        // Автоматизация секции: срез по экспоненте, резонанс линейно.
        const acid_section_t *sec = s->sec;
        float p = acid_arr_progress(s->bar, s->seq.index < 0 ? 0 : s->seq.index);
        p += s->seq.pos / s->seq.samples_per_step / (float)(ACID_SECTION_BARS * 16);
        base = sec->cut_from * exp2f(p * log2f(sec->cut_to / sec->cut_from));
        s->res = sec->res_from + (sec->res_to - sec->res_from) * p;
    }
    float cutoff = base + s->fenv_amount * s->fenv.value;
    return acid_ladder_coef(cutoff, s->sr);
}

void acid_synth_render(acid_synth_t *s, float *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int step = acid_seq_tick(&s->seq);
        if (step >= 0) {
            if (s->arr_on) {
                if (step == 0) {
                    on_bar(s);
                }
                drums_step(s, step);
            } else if (s->kick_on && step % 4 == 0) {
                acid_kick_trigger(&s->kick);
            }
            on_step(s, step);
            s->last_flags = s->seq.steps[step].flags;
            s->ctrl_left = 0; // новая нота: пересчитать сразу
        }
        acid_amp_gate(&s->amp, acid_seq_gate(&s->seq));

        if (s->gliding) {
            s->pitch_log2 = s->target_log2 + (s->pitch_log2 - s->target_log2) * s->glide_coef;
        }
        if (s->ctrl_left == 0) {
            s->g = control_update(s);
            s->ctrl_left = CTRL_BLOCK;
        }
        s->ctrl_left--;

        float x = s->wave ? acid_osc_square(&s->osc) : acid_osc_saw(&s->osc);
        acid_decay_next(&s->fenv);
        float y = acid_ladder_run(&s->flt, x, s->g, s->res);
        y *= acid_amp_next(&s->amp) * s->accent_gain;
        y += KICK_GAIN * acid_kick_next(&s->kick);
        y += HAT_GAIN * acid_hat_next(&s->hat) + CLAP_GAIN * acid_clap_next(&s->clap);
        out[i] = OUT_PEAK * acid_tanh(DRIVE * y);
    }
}
