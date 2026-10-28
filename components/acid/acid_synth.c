#include "acid_synth.h"

#include <math.h>
#include <string.h>

#include "acid_math.h"

#define BPM 130.0f
#define RES 0.85f
#define FENV_NORMAL_HZ 3000.0f
#define FENV_ACCENT_HZ 5000.0f
#define FENV_NORMAL_S 0.25f
#define FENV_ACCENT_RATIO 0.48f // спад акцента относительно обычной ноты (0.12 с при 0.25)
#define ACCENT_GAIN 1.4f
#define GLIDE_S 0.06f
#define FIXED_CUTOFF_HZ 500.0f // база среза в режиме фиксированного паттерна
#define HAT_GAIN 0.25f
#define CLAP_GAIN 0.6f
#define DRIVE 0.8f
#define OUT_PEAK 0.5f // -6 dBFS
#define KICK_GAIN 1.45f // +10% пика бочки на выходе после tanh
#define CTRL_BLOCK 16 // управляющая частота sr/16: высота, автоматизация, коэффициент фильтра
#define ACCENT_VEL 100 // velocity выше этого - акцент (у шагов BSP по умолчанию 100)
#define BASS_CH 4      // MIDI канал 5: SEQ1
#define DRUM_CH 1      // MIDI канал 2: барабаны и пэды Control mode
#define HELD_MAX 8
#define BOUNCE_S 0.025f    // нота короче этого - возможно дребезг пэда BSP
#define MERGE_S 0.05f      // повтор той же ноты в этом окне от нажатия склеивается с ней
#define DRUM_VEL_FLOOR 0.3f // громкость самого слабого удара пэда
#define SNARE_GAIN 0.8f
#define PAGES 3
#define PAGE_BASS 2

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
    acid_snare_init(&s->snare, sr);
    s->kick_on = 1;

    s->cut_base = FIXED_CUTOFF_HZ;
    s->env_hz = FENV_NORMAL_HZ;
    s->decay_s = FENV_NORMAL_S;
    s->accent_amt = ACCENT_GAIN;
    s->bass_gain = 1.0f;
    s->kick_gain = KICK_GAIN;
    s->hat_gain = HAT_GAIN;
    s->clap_gain = CLAP_GAIN;
    s->snare_gain = SNARE_GAIN;
    s->drive = DRIVE;
    s->master = 1.0f;
    s->kick_vel = s->hat_vel = s->clap_vel = s->snare_vel = 1.0f;
    s->transpose = 0;
    s->kick_tune = ACID_KICK_LOW_HZ;
    s->kick_sweep = ACID_KICK_SWEEP_HZ;
    s->kick_pitch_s = ACID_KICK_PITCH_S;
    s->kick_amp_s = ACID_KICK_AMP_S;
    s->hat_tone = ACID_HAT_HZ;
    s->hat_closed_s = ACID_HAT_CLOSED_S;
    s->hat_open_s = ACID_HAT_OPEN_S;
    s->clap_tone = ACID_CLAP_HZ;
    s->clap_tail_s = ACID_CLAP_TAIL_S;
    s->snare_tune = ACID_SNARE_TUNE_HZ;
    s->snare_snappy = ACID_SNARE_SNAPPY;
    s->snare_decay_s = ACID_SNARE_DECAY_S;
    s->page = PAGE_BASS;
    s->live = 0;
    s->nheld = 0;
    s->now = 0;
    s->pend_note = -1;

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

// Новая нота голоса: со слайдом только глайд, без него перезапуск огибающих.
static void voice_note(acid_synth_t *s, int note, int accent, int slide)
{
    s->target_log2 = log2f(acid_midi_to_hz(note));
    // Акцент громкости действует и на слайде, огибающие при этом не перезапускаются.
    s->accent_gain = accent ? s->accent_amt : 1.0f;
    if (slide) {
        s->gliding = 1;
        return;
    }
    s->gliding = 0;
    s->pitch_log2 = s->target_log2;
    acid_decay_set(&s->fenv, accent ? s->decay_s * FENV_ACCENT_RATIO : s->decay_s, s->sr);
    acid_decay_trigger(&s->fenv, 1.0f);
    float env = accent ? s->env_hz * (FENV_ACCENT_HZ / FENV_NORMAL_HZ) : s->env_hz;
    s->fenv_amount = env * (s->arr_on ? s->sec->env_scale : 1.0f);
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
    voice_note(s, note, cur->flags & ACID_ACCENT, slide_in);
}

void acid_synth_set_live(acid_synth_t *s, int on)
{
    if (on) {
        // Секвенсор и автоматизация замолкают, звучащие хвосты доигрывают.
        s->live = 1;
        s->arr_on = 0;
        s->kick_on = 0;
        s->nheld = 0;
        s->pend_note = -1;
        s->gliding = 0;
        s->res = RES;
        s->wave = 0;
        return;
    }
    // Трек с начала, но состояние голосов сохраняем, чтобы не щёлкнуть.
    acid_osc_t osc = s->osc;
    acid_ladder_t flt = s->flt;
    acid_amp_t amp = s->amp;
    acid_decay_t fenv = s->fenv;
    acid_kick_t kick = s->kick;
    acid_hat_t hat = s->hat;
    acid_clap_t clap = s->clap;
    acid_snare_t snare = s->snare;
    float pitch = s->pitch_log2;
    acid_synth_init(s, s->sr);
    s->osc = osc;
    s->flt = flt;
    s->amp = amp;
    s->fenv = fenv;
    s->kick = kick;
    s->hat = hat;
    s->clap = clap;
    s->snare = snare;
    // Хвосты доигрывают, но дальше ударка звучит как в треке.
    acid_kick_set(&s->kick, s->kick_tune, s->kick_sweep, s->kick_pitch_s, s->kick_amp_s);
    acid_hat_set(&s->hat, s->hat_tone, s->hat_closed_s, s->hat_open_s);
    acid_clap_set(&s->clap, s->clap_tone, s->clap_tail_s);
    acid_snare_set(&s->snare, s->snare_tune, s->snare_snappy, s->snare_decay_s);
    s->pitch_log2 = s->target_log2 = pitch;
}

static void held_remove(acid_synth_t *s, int note)
{
    int j = 0;
    for (int i = 0; i < s->nheld; i++) {
        if (s->held[i] != note) {
            s->held_at[j] = s->held_at[i];
            s->held[j++] = s->held[i];
        }
    }
    s->nheld = j;
}

static void bass_release(acid_synth_t *s, int note);

static void bass_on(acid_synth_t *s, int note, int vel)
{
    if (s->pend_note == note) {
        // Дребезг: та же нота сразу после короткого нажатия - это продолжение той же ноты.
        s->pend_note = -1;
        return;
    }
    if (s->pend_note >= 0) {
        bass_release(s, s->pend_note); // короткая нота закончилась, новая не слайдит из неё
        s->pend_note = -1;
    }
    held_remove(s, note);
    int slide = s->nheld > 0;
    if (s->nheld == HELD_MAX) {
        memmove(s->held, s->held + 1, HELD_MAX - 1); // вытесняем самую старую
        memmove(s->held_at, s->held_at + 1, (HELD_MAX - 1) * sizeof s->held_at[0]);
        s->nheld--;
    }
    s->held_at[s->nheld] = s->now;
    s->held[s->nheld++] = (int8_t)note;
    voice_note(s, note + s->transpose, vel > ACCENT_VEL, slide);
}

static void bass_off(acid_synth_t *s, int note)
{
    for (int i = 0; i < s->nheld; i++) {
        if (s->held[i] == note && s->now - s->held_at[i] < (uint32_t)(BOUNCE_S * s->sr)) {
            // Слишком короткое нажатие: отпустим позже, если не придёт повтор.
            s->pend_note = note;
            s->pend_at = s->held_at[i] + (uint32_t)(MERGE_S * s->sr);
            return;
        }
    }
    bass_release(s, note);
}

static void bass_release(acid_synth_t *s, int note)
{
    int top = s->nheld > 0 && s->held[s->nheld - 1] == note;
    held_remove(s, note);
    if (top && s->nheld > 0) {
        // Отпустили верхнюю при зажатой нижней: глайд обратно, как в 303.
        s->target_log2 = log2f(acid_midi_to_hz(s->held[s->nheld - 1] + s->transpose));
        s->gliding = 1;
    }
}

static void drum_on(acid_synth_t *s, int note, int vel)
{
    // Пэды BSP при мягкой игре дают velocity 3-30: без пола такие удары не слышны.
    float v = DRUM_VEL_FLOOR + (1.0f - DRUM_VEL_FLOOR) * (float)vel / 127.0f;
    if (note == 36) {
        acid_kick_trigger(&s->kick);
        s->kick_vel = v;
    } else if (note == 38 || note == 40) {
        acid_snare_trigger(&s->snare);
        s->snare_vel = v;
    } else if (note == 39) {
        acid_clap_trigger(&s->clap);
        s->clap_vel = v;
    } else if (note == 42 || note == 44 || note == 46) {
        acid_hat_trigger(&s->hat, note == 46);
        s->hat_vel = v;
    }
}

static float lin(int v, float a, float b)
{
    return a + (b - a) * (float)v / 127.0f;
}

static float expo(int v, float a, float b)
{
    return a * exp2f((float)v / 127.0f * log2f(b / a));
}

// CC энкодеров BSP по порядку на панели (стандартная раскладка Arturia): 0 - энкодер 1.
static const uint8_t ENC_CC[16] = {10, 74, 71, 76, 77, 93, 73, 75, 114, 18, 19, 16, 17, 91, 79, 72};

static void page_mix(acid_synth_t *s, int slot, int v)
{
    switch (slot) {
    case 1: s->master = lin(v, 0.0f, 1.0f); break;
    case 2: s->drive = expo(v, 0.3f, 3.0f); break;
    case 3: s->bass_gain = lin(v, 0.0f, 1.5f); break;
    case 4: s->kick_gain = lin(v, 0.0f, 2.0f); break;
    case 5: s->hat_gain = lin(v, 0.0f, 0.6f); break;
    case 6: s->clap_gain = lin(v, 0.0f, 1.2f); break;
    case 7: s->snare_gain = lin(v, 0.0f, 1.5f); break;
    default: break;
    }
}

static void page_drums(acid_synth_t *s, int slot, int v)
{
    switch (slot) {
    case 1: s->kick_tune = expo(v, 35.0f, 80.0f); break;
    case 2: s->kick_sweep = lin(v, 0.0f, 300.0f); break;
    case 3: s->kick_pitch_s = expo(v, 0.005f, 0.1f); break;
    case 4: s->kick_amp_s = expo(v, 0.04f, 0.5f); break;
    case 5: s->hat_tone = expo(v, 3000.0f, 12000.0f); break;
    case 6: s->hat_closed_s = expo(v, 0.005f, 0.08f); break;
    case 7: s->hat_open_s = expo(v, 0.05f, 0.8f); break;
    case 8: s->clap_tone = expo(v, 600.0f, 3000.0f); break;
    case 9: s->clap_tail_s = expo(v, 0.03f, 0.4f); break;
    case 10: s->snare_tune = expo(v, 120.0f, 350.0f); break;
    case 11: s->snare_snappy = lin(v, 0.0f, 1.0f); break;
    case 12: s->snare_decay_s = expo(v, 0.03f, 0.3f); break;
    default: return;
    }
    acid_kick_set(&s->kick, s->kick_tune, s->kick_sweep, s->kick_pitch_s, s->kick_amp_s);
    acid_hat_set(&s->hat, s->hat_tone, s->hat_closed_s, s->hat_open_s);
    acid_clap_set(&s->clap, s->clap_tone, s->clap_tail_s);
    acid_snare_set(&s->snare, s->snare_tune, s->snare_snappy, s->snare_decay_s);
}

static void page_bass(acid_synth_t *s, int slot, int v)
{
    switch (slot) {
    case 1: s->cut_base = expo(v, 60.0f, 3000.0f); break;
    case 2: s->res = lin(v, 0.0f, 0.97f); break;
    case 3: s->env_hz = lin(v, 0.0f, 6000.0f); break;
    case 4: s->decay_s = expo(v, 0.05f, 1.5f); break;
    case 5: s->accent_amt = lin(v, 1.0f, 2.0f); break;
    case 6: s->wave = v >= 64; break;
    case 7: s->glide_coef = expf(-1.0f / (expo(v, 0.01f, 0.3f) * s->sr)); break;
    case 8: s->transpose = (v * 25) / 128 - 12; break; // -12..+12, центр 0
    default: break;
    }
}

static void control_change(acid_synth_t *s, int num, int v)
{
    int slot = -1;
    for (int i = 0; i < 16; i++) {
        if (ENC_CC[i] == num) {
            slot = i;
        }
    }
    if (slot == 0) {
        s->page = v * PAGES / 128; // зоны хода энкодера 1
    } else if (s->page == 0) {
        page_mix(s, slot, v);
    } else if (s->page == 1) {
        page_drums(s, slot, v);
    } else {
        page_bass(s, slot, v);
    }
}

void acid_synth_midi(acid_synth_t *s, const acid_midi_event_t *ev)
{
    if (!s->live) {
        return;
    }
    switch (ev->type) {
    case ACID_MIDI_NOTE_ON:
        if (ev->ch == BASS_CH) {
            bass_on(s, ev->d1, ev->d2);
            s->ctrl_left = 0; // новая нота: пересчитать сразу
        } else if (ev->ch == DRUM_CH) {
            drum_on(s, ev->d1, ev->d2);
        }
        break;
    case ACID_MIDI_NOTE_OFF:
        if (ev->ch == BASS_CH) {
            bass_off(s, ev->d1);
        }
        break;
    case ACID_MIDI_CC:
        control_change(s, ev->d1, ev->d2);
        break;
    case ACID_MIDI_STOP:
        s->nheld = 0; // защита от залипших нот
        s->pend_note = -1;
        s->gliding = 0;
        break;
    default:
        break;
    }
}

// Пересчёт дорогих величин раз в CTRL_BLOCK сэмплов.
static float control_update(acid_synth_t *s)
{
    acid_osc_set_freq(&s->osc, exp2f(s->pitch_log2), s->sr);

    float base = s->cut_base;
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
        s->now++;
        if (s->pend_note >= 0 && (int32_t)(s->now - s->pend_at) >= 0) {
            bass_release(s, s->pend_note);
            s->pend_note = -1;
        }
        int step = s->live ? -1 : acid_seq_tick(&s->seq);
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
        acid_amp_gate(&s->amp, s->live ? s->nheld > 0 : acid_seq_gate(&s->seq));

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
        y *= acid_amp_next(&s->amp) * s->accent_gain * s->bass_gain;
        y += s->kick_gain * s->kick_vel * acid_kick_next(&s->kick);
        y += s->hat_gain * s->hat_vel * acid_hat_next(&s->hat);
        y += s->clap_gain * s->clap_vel * acid_clap_next(&s->clap);
        y += s->snare_gain * s->snare_vel * acid_snare_next(&s->snare);
        out[i] = s->master * OUT_PEAK * acid_tanh(s->drive * y);
    }
}
