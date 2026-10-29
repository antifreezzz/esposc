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
#define BASS_CH 4      // MIDI канал 5: SEQ1, синт 1
#define SEQ2_CH 5      // MIDI канал 6: SEQ2, синт 2
#define DRUM_CH 1      // MIDI канал 2: барабаны и пэды Control mode
#define HELD_MAX 8
#define BOUNCE_S 0.025f    // нота короче этого - возможно дребезг пэда BSP
#define MERGE_S 0.05f      // повтор той же ноты в этом окне от нажатия склеивается с ней
#define DRUM_VEL_FLOOR 0.3f // громкость самого слабого удара пэда
#define SNARE_GAIN 0.8f
#define PAGES 4
#define PAGE_SYNTH1 2
#define MIX_CHUNK 64 // кусок буфера слота 1 в общем рендере
#define FADE_N 192     // 4 мс ухода и столько же прихода при смене типа синтеза
#define PLAITS_GAIN 1.0f // уровень Plaits относительно 303
#define IDLE_BLOCKS 400  // 100 мс точной тишины без гейта: молчащий Plaits не считаем
#define SEQ2_TYPE 11     // синт 2 по умолчанию: FM 2-op (модель Plaits 10)
#define PLAITS_ACCENT 0.4f // обычная нота Plaits тише акцентной на столько
#define KNOB_NONE 0xFF
#define TYPE_ENC 1       // энкодер 2 на странице синта
static const uint8_t BLOB_MAGIC[4] = {'A', 'S', '5', 1};

static void type_defaults(acid_synth_t *s, acid_slot_t *v);

static void slot_init(acid_slot_t *v, float sr, int type)
{
    v->type = v->sel_type = v->next_type = type;
    v->fade = 0;
    memset(v->knob, KNOB_NONE, sizeof v->knob);
    if (!v->plaits) {
        v->plaits = acid_plaits_create(); // один раз: s обнулён до первого init
    }
    v->pp = (acid_plaits_params_t){0};
    v->ppos = ACID_PLAITS_BLOCK;
    v->retrig = 0;
    v->accented = 0;
    v->idle = IDLE_BLOCKS;
    v->osc = (acid_osc_t){0};
    acid_ladder_reset(&v->flt);
    acid_decay_init(&v->fenv, FENV_NORMAL_S, sr);
    acid_amp_init(&v->amp, 0.003f, 0.01f, sr);
    v->glide_coef = expf(-1.0f / (GLIDE_S * sr));
    v->pitch_log2 = v->target_log2 = log2f(110.0f);
    v->gliding = 0;
    v->accent_gain = 1.0f;
    v->fenv_amount = FENV_NORMAL_HZ;
    v->res = RES;
    v->wave = 0;
    v->ctrl_left = 0;
    v->g = 0.0f;
    v->cut_base = FIXED_CUTOFF_HZ;
    v->env_hz = FENV_NORMAL_HZ;
    v->decay_s = FENV_NORMAL_S;
    v->accent_amt = ACCENT_GAIN;
    v->gain = 1.0f;
    v->transpose = 0;
    v->nheld = 0;
    v->now = 0;
    v->pend_note = -1;
}

void acid_synth_init(acid_synth_t *s, float sr)
{
    s->sr = sr;
    for (int k = 0; k < ACID_SLOTS; k++) {
        slot_init(&s->slot[k], sr, k == 1 ? SEQ2_TYPE : 0);
        type_defaults(s, &s->slot[k]);
    }
    s->save = 0;
    s->last_flags = 0;
    acid_kick_init(&s->kick, sr);
    acid_hat_init(&s->hat, sr);
    acid_clap_init(&s->clap, sr);
    acid_snare_init(&s->snare, sr);
    s->kick_on = 1;

    s->kick_gain = KICK_GAIN;
    s->hat_gain = HAT_GAIN;
    s->clap_gain = CLAP_GAIN;
    s->snare_gain = SNARE_GAIN;
    s->drive = DRIVE;
    s->master = 1.0f;
    s->kick_vel = s->hat_vel = s->clap_vel = s->snare_vel = 1.0f;
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
    acid_pump_init(&s->pump, sr);
    acid_djf_init(&s->djf, sr);
    s->page = PAGE_SYNTH1;
    s->live = 0;

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
    s->slot[0].wave = s->sec->wave;
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
    return exp2f(s->slot[0].pitch_log2);
}

// Новая нота голоса: со слайдом только глайд, без него перезапуск огибающих.
static void voice_note(acid_synth_t *s, acid_slot_t *v, int note, int accent, int slide)
{
    v->target_log2 = log2f(acid_midi_to_hz(note));
    // Акцент громкости действует и на слайде, огибающие при этом не перезапускаются.
    v->accent_gain = accent ? v->accent_amt : 1.0f;
    v->accented = accent != 0;
    if (slide) {
        v->gliding = 1;
        return;
    }
    v->gliding = 0;
    v->pitch_log2 = v->target_log2;
    v->retrig = 1;
    acid_decay_set(&v->fenv, accent ? v->decay_s * FENV_ACCENT_RATIO : v->decay_s, s->sr);
    acid_decay_trigger(&v->fenv, 1.0f);
    float env = accent ? v->env_hz * (FENV_ACCENT_HZ / FENV_NORMAL_HZ) : v->env_hz;
    v->fenv_amount = env * (s->arr_on ? s->sec->env_scale : 1.0f);
}

static void on_step(acid_synth_t *s, int index)
{
    acid_slot_t *v = &s->slot[0];
    const acid_step_t *cur = &s->seq.steps[index];
    if (cur->flags & ACID_REST) {
        v->gliding = 0;
        return;
    }
    // Слайд только если предыдущий шаг реально звучит: на самом первом шаге amp = 0.
    int slide_in = (s->last_flags & ACID_SLIDE) && !(s->last_flags & ACID_REST) &&
                   v->amp.value > 0.0f;
    int note = cur->note + (s->arr_on ? s->sec->transpose : 0);
    voice_note(s, v, note, cur->flags & ACID_ACCENT, slide_in);
}

void acid_synth_set_live(acid_synth_t *s, int on)
{
    if (on) {
        // Секвенсор и автоматизация замолкают, звучащие хвосты доигрывают.
        s->live = 1;
        s->arr_on = 0;
        s->kick_on = 0;
        for (int k = 0; k < ACID_SLOTS; k++) {
            acid_slot_t *v = &s->slot[k];
            v->nheld = 0;
            v->pend_note = -1;
            v->gliding = 0;
            // Выбор с BSP: тип и его ручки (автопилот мог поменять срез и форму волны 303).
            v->type = v->next_type = v->sel_type;
            v->fade = 0;
            type_defaults(s, v);
        }
        return;
    }
    // Трек с начала, но состояние голосов сохраняем, чтобы не щёлкнуть.
    acid_slot_t keep[ACID_SLOTS];
    memcpy(keep, s->slot, sizeof keep);
    acid_kick_t kick = s->kick;
    acid_hat_t hat = s->hat;
    acid_clap_t clap = s->clap;
    acid_snare_t snare = s->snare;
    acid_synth_init(s, s->sr);
    for (int k = 0; k < ACID_SLOTS; k++) {
        acid_slot_t *v = &s->slot[k];
        if (k == 1) {
            // Автопилот слот 1 не играет: доигрывает хвост своим типом, стек пуст.
            *v = keep[k];
            v->nheld = 0;
            v->pend_note = -1;
            v->gliding = 0;
            v->type = v->next_type;
            v->fade = 0;
            continue;
        }
        v->osc = keep[k].osc;
        v->flt = keep[k].flt;
        v->amp = keep[k].amp;
        v->fenv = keep[k].fenv;
        v->pitch_log2 = v->target_log2 = keep[k].pitch_log2;
        // Автопилот играет 303, выбор и ручки ждут BSP.
        v->sel_type = keep[k].sel_type;
        memcpy(v->knob, keep[k].knob, sizeof v->knob);
    }
    s->kick = kick;
    s->hat = hat;
    s->clap = clap;
    s->snare = snare;
    // Хвосты доигрывают, но дальше ударка звучит как в треке.
    acid_kick_set(&s->kick, s->kick_tune, s->kick_sweep, s->kick_pitch_s, s->kick_amp_s);
    acid_hat_set(&s->hat, s->hat_tone, s->hat_closed_s, s->hat_open_s);
    acid_clap_set(&s->clap, s->clap_tone, s->clap_tail_s);
    acid_snare_set(&s->snare, s->snare_tune, s->snare_snappy, s->snare_decay_s);
}

static void held_remove(acid_slot_t *v, int note)
{
    int j = 0;
    for (int i = 0; i < v->nheld; i++) {
        if (v->held[i] != note) {
            v->held_at[j] = v->held_at[i];
            v->held[j++] = v->held[i];
        }
    }
    v->nheld = j;
}

static void note_release(acid_slot_t *v, int note);

static void note_on(acid_synth_t *s, acid_slot_t *v, int note, int vel)
{
    if (v->pend_note == note) {
        // Дребезг: та же нота сразу после короткого нажатия - это продолжение той же ноты.
        v->pend_note = -1;
        return;
    }
    if (v->pend_note >= 0) {
        note_release(v, v->pend_note); // короткая нота закончилась, новая не слайдит из неё
        v->pend_note = -1;
    }
    held_remove(v, note);
    int slide = v->nheld > 0;
    if (v->nheld == HELD_MAX) {
        memmove(v->held, v->held + 1, HELD_MAX - 1); // вытесняем самую старую
        memmove(v->held_at, v->held_at + 1, (HELD_MAX - 1) * sizeof v->held_at[0]);
        v->nheld--;
    }
    v->held_at[v->nheld] = v->now;
    v->held[v->nheld++] = (int8_t)note;
    voice_note(s, v, note + v->transpose, vel > ACCENT_VEL, slide);
    v->ctrl_left = 0; // новая нота: пересчитать сразу
}

static void note_off(acid_synth_t *s, acid_slot_t *v, int note)
{
    for (int i = 0; i < v->nheld; i++) {
        if (v->held[i] == note && v->now - v->held_at[i] < (uint32_t)(BOUNCE_S * s->sr)) {
            // Слишком короткое нажатие: отпустим позже, если не придёт повтор.
            v->pend_note = note;
            v->pend_at = v->held_at[i] + (uint32_t)(MERGE_S * s->sr);
            return;
        }
    }
    note_release(v, note);
}

static void note_release(acid_slot_t *v, int note)
{
    int top = v->nheld > 0 && v->held[v->nheld - 1] == note;
    held_remove(v, note);
    if (top && v->nheld > 0) {
        // Отпустили верхнюю при зажатой нижней: глайд обратно, как в 303.
        v->target_log2 = log2f(acid_midi_to_hz(v->held[v->nheld - 1] + v->transpose));
        v->gliding = 1;
    }
}

static void drum_on(acid_synth_t *s, int note, int vel)
{
    // Пэды BSP при мягкой игре дают velocity 3-30: без пола такие удары не слышны.
    float v = DRUM_VEL_FLOOR + (1.0f - DRUM_VEL_FLOOR) * (float)vel / 127.0f;
    if (note == 36) {
        acid_kick_trigger(&s->kick);
        s->kick_vel = v;
        acid_pump_hit(&s->pump, v); // от ноты: при бочке в нуле тоже качает
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
    case 3: s->slot[0].gain = lin(v, 0.0f, 1.5f); break;
    case 4: s->slot[1].gain = lin(v, 0.0f, 1.5f); break;
    case 5: s->kick_gain = lin(v, 0.0f, 2.0f); break;
    case 6: s->hat_gain = lin(v, 0.0f, 0.6f); break;
    case 7: s->clap_gain = lin(v, 0.0f, 1.2f); break;
    case 8: s->snare_gain = lin(v, 0.0f, 1.5f); break;
    case 9: acid_pump_set_depth(&s->pump, lin(v, 0.0f, 1.0f)); break;
    case 10: acid_pump_set_release(&s->pump, expo(v, 0.03f, 0.9f)); break;
    case 11: acid_djf_set(&s->djf, v, s->djf.res_knob); break;
    case 12: acid_djf_set(&s->djf, s->djf.knob, v); break;
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

static float bip(int v)
{
    float x = (float)(v - 64) / 63.0f;
    return x < -1.0f ? -1.0f : x;
}

// Ручки 303, slot - индекс энкодера с 0.
static void knob_303(acid_synth_t *s, acid_slot_t *sl, int slot, int v)
{
    switch (slot) {
    case 2: sl->cut_base = expo(v, 60.0f, 3000.0f); break;
    case 3: sl->res = lin(v, 0.0f, 0.97f); break;
    case 4: sl->env_hz = lin(v, 0.0f, 6000.0f); break;
    case 5: sl->decay_s = expo(v, 0.05f, 1.5f); break;
    case 6: sl->accent_amt = lin(v, 1.0f, 2.0f); break;
    case 7: sl->wave = v >= 64; break;
    case 8: sl->glide_coef = expf(-1.0f / (expo(v, 0.01f, 0.3f) * s->sr)); break;
    case 9: sl->transpose = (v * 25) / 128 - 12; break; // -12..+12, центр 0
    default: break;
    }
}

static void knob_plaits(acid_synth_t *s, acid_slot_t *sl, int slot, int v)
{
    acid_plaits_params_t *p = &sl->pp;
    switch (slot) {
    case 2: p->harmonics = lin(v, 0.0f, 1.0f); break;
    case 3: p->timbre = lin(v, 0.0f, 1.0f); break;
    case 4: p->morph = lin(v, 0.0f, 1.0f); break;
    case 5: p->decay = lin(v, 0.0f, 1.0f); break;
    case 6: p->lpg_colour = lin(v, 0.0f, 1.0f); break;
    case 7: sl->glide_coef = expf(-1.0f / (expo(v, 0.01f, 0.3f) * s->sr)); break;
    case 8: sl->transpose = (v * 25) / 128 - 12; break;
    case 9: p->timbre_env = bip(v); break;
    case 10: p->morph_env = bip(v); break;
    case 11: p->fm_env = bip(v); break;
    case 12: sl->p_accent = lin(v, 0.0f, 0.75f); break;
    default: break;
    }
}

static void knob_apply(acid_synth_t *s, acid_slot_t *v, int slot, int val)
{
    if (v->type == 0) {
        knob_303(s, v, slot, val);
    } else {
        knob_plaits(s, v, slot, val);
    }
}

// Умолчания звучащего типа и поверх них запомненные ручки этого типа.
static void type_defaults(acid_synth_t *s, acid_slot_t *v)
{
    v->glide_coef = expf(-1.0f / (GLIDE_S * s->sr));
    v->transpose = 0;
    if (v->type == 0) {
        v->cut_base = FIXED_CUTOFF_HZ;
        v->env_hz = FENV_NORMAL_HZ;
        v->decay_s = FENV_NORMAL_S;
        v->accent_amt = ACCENT_GAIN;
        v->res = RES;
        v->wave = 0;
    } else {
        acid_plaits_params_t *p = &v->pp;
        p->model = v->type - 1;
        p->harmonics = p->timbre = p->morph = 0.5f;
        p->decay = 0.3f; // хвост ~0.1 с: под секвенсор
        p->lpg_colour = 0.5f;
        p->timbre_env = p->morph_env = p->fm_env = 0.0f;
        v->p_accent = PLAITS_ACCENT;
    }
    for (int i = TYPE_ENC + 1; i < ACID_KNOBS; i++) {
        if (v->knob[v->type][i] != KNOB_NONE) {
            knob_apply(s, v, i, v->knob[v->type][i]);
        }
    }
}

// Звук уже ушёл: переключаем тип, звучащая нота продолжается новым голосом.
static void type_switch(acid_synth_t *s, acid_slot_t *v)
{
    v->type = v->next_type;
    s->save = 1;
    type_defaults(s, v);
    if (v->nheld > 0) {
        voice_note(s, v, v->held[v->nheld - 1] + v->transpose, v->accented, 0);
        v->ctrl_left = 0;
    }
    v->ppos = ACID_PLAITS_BLOCK;
    v->fade = -FADE_N;
}

static void type_request(acid_slot_t *v, int t)
{
    v->sel_type = t;
    v->next_type = t;
    if (v->fade > 0 || t == v->type) {
        return; // уже уходим (тип возьмём последний) или он и так звучит
    }
    // Уход с текущей громкости: если ещё шёл приход, без скачка.
    v->fade = v->fade < 0 ? FADE_N + v->fade : FADE_N;
    if (v->fade == 0) {
        v->fade = 1;
    }
}

// Страница синта: энкодер 2 - тип синтеза, дальше ручки типа (запоминаются по типам).
static void page_synth(acid_synth_t *s, acid_slot_t *sl, int slot, int v)
{
    if (slot == TYPE_ENC) {
        type_request(sl, v * ACID_TYPES / 128);
    } else if (slot > TYPE_ENC) {
        sl->knob[sl->type][slot] = (uint8_t)v;
        knob_apply(s, sl, slot, v);
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
        if (v < PAGES) {
            s->page = v; // значение на экране BSP - номер страницы, остальное не страница
        }
    } else if (s->page == 0) {
        page_mix(s, slot, v);
    } else if (s->page == 1) {
        page_drums(s, slot, v);
    } else {
        page_synth(s, &s->slot[s->page - PAGE_SYNTH1], slot, v);
    }
}

void acid_synth_midi(acid_synth_t *s, const acid_midi_event_t *ev)
{
    if (!s->live) {
        return;
    }
    switch (ev->type) {
    case ACID_MIDI_NOTE_ON:
        if (ev->ch == BASS_CH || ev->ch == SEQ2_CH) {
            note_on(s, &s->slot[ev->ch == SEQ2_CH], ev->d1, ev->d2);
        } else if (ev->ch == DRUM_CH) {
            drum_on(s, ev->d1, ev->d2);
        }
        break;
    case ACID_MIDI_NOTE_OFF:
        if (ev->ch == BASS_CH || ev->ch == SEQ2_CH) {
            note_off(s, &s->slot[ev->ch == SEQ2_CH], ev->d1);
        }
        break;
    case ACID_MIDI_CC:
        control_change(s, ev->d1, ev->d2);
        break;
    case ACID_MIDI_STOP:
        for (int k = 0; k < ACID_SLOTS; k++) {
            s->slot[k].nheld = 0; // защита от залипших нот
            s->slot[k].pend_note = -1;
            s->slot[k].gliding = 0;
        }
        break;
    default:
        break;
    }
}

// Пересчёт дорогих величин раз в CTRL_BLOCK сэмплов.
static float control_update(acid_synth_t *s, acid_slot_t *v)
{
    acid_osc_set_freq(&v->osc, exp2f(v->pitch_log2), s->sr);

    float base = v->cut_base;
    if (s->arr_on && v == &s->slot[0]) {
        // Автоматизация секции: срез по экспоненте, резонанс линейно.
        const acid_section_t *sec = s->sec;
        float p = acid_arr_progress(s->bar, s->seq.index < 0 ? 0 : s->seq.index);
        p += s->seq.pos / s->seq.samples_per_step / (float)(ACID_SECTION_BARS * 16);
        base = sec->cut_from * exp2f(p * log2f(sec->cut_to / sec->cut_from));
        v->res = sec->res_from + (sec->res_to - sec->res_from) * p;
    }
    float cutoff = base + v->fenv_amount * v->fenv.value;
    return acid_ladder_coef(cutoff, s->sr);
}

// Время слота: отложенное отпускание короткой ноты.
static void slot_tick(acid_slot_t *v)
{
    v->now++;
    if (v->pend_note >= 0 && (int32_t)(v->now - v->pend_at) >= 0) {
        note_release(v, v->pend_note);
        v->pend_note = -1;
    }
}

// Сэмпл голоса Plaits с громкостью слота: блоки по 12, молчащий голос не считаем.
static float plaits_next(acid_slot_t *v, int gate)
{
    if (v->gliding) {
        v->pitch_log2 = v->target_log2 + (v->pitch_log2 - v->target_log2) * v->glide_coef;
    }
    if (v->ppos == ACID_PLAITS_BLOCK) {
        v->ppos = 0;
        if (!gate && v->idle >= IDLE_BLOCKS) {
            memset(v->pbuf, 0, sizeof v->pbuf);
        } else {
            acid_plaits_params_t *p = &v->pp;
            p->note = 12.0f * (v->pitch_log2 - log2f(440.0f)) + 69.0f;
            p->trigger = gate && !v->retrig;
            v->retrig = 0;
            p->level = gate ? (v->accented ? 1.0f : 1.0f - v->p_accent) : 0.0f;
            acid_plaits_render(v->plaits, p, v->pbuf, ACID_PLAITS_BLOCK);
            int quiet = !gate;
            for (int i = 0; i < ACID_PLAITS_BLOCK && quiet; i++) {
                quiet = v->pbuf[i] == 0.0f;
            }
            v->idle = quiet ? v->idle + 1 : 0;
        }
    }
    return v->pbuf[v->ppos++] * (PLAITS_GAIN * v->gain);
}

// Сэмпл голоса 303 с громкостью слота.
static float voice_next(acid_synth_t *s, acid_slot_t *v, int gate)
{
    acid_amp_gate(&v->amp, gate);
    if (v->gliding) {
        v->pitch_log2 = v->target_log2 + (v->pitch_log2 - v->target_log2) * v->glide_coef;
    }
    if (v->ctrl_left == 0) {
        v->g = control_update(s, v);
        v->ctrl_left = CTRL_BLOCK;
    }
    v->ctrl_left--;

    float x = v->wave ? acid_osc_square(&v->osc) : acid_osc_saw(&v->osc);
    acid_decay_next(&v->fenv);
    float y = acid_ladder_run(&v->flt, x, v->g, v->res);
    return y * (acid_amp_next(&v->amp) * v->accent_gain * v->gain);
}

// Громкость смены типа: уход, переключение, приход.
static float fade_step(acid_synth_t *s, acid_slot_t *v)
{
    if (v->fade > 0) {
        float g = (float)v->fade / FADE_N;
        if (--v->fade == 0) {
            type_switch(s, v);
        }
        return g;
    }
    float g = (float)(FADE_N + v->fade) / FADE_N;
    v->fade++;
    return g;
}

// Сэмпл слота с громкостью: голос его типа, при смене типа - с уходом и приходом.
static float slot_next(acid_synth_t *s, acid_slot_t *v, int gate)
{
    float y = v->type == 0 ? voice_next(s, v, gate) : plaits_next(v, gate);
    if (v->fade != 0) {
        y *= fade_step(s, v);
    }
    return y;
}

void acid_synth_render_slot(acid_synth_t *s, int k, float *out, size_t n)
{
    acid_slot_t *v = &s->slot[k];
    for (size_t i = 0; i < n; i++) {
        slot_tick(v);
        out[i] = slot_next(s, v, v->nheld > 0);
    }
}

void acid_synth_render_with(acid_synth_t *s, float *out, size_t n, const float *slot1)
{
    acid_slot_t *v = &s->slot[0];
    for (size_t i = 0; i < n; i++) {
        slot_tick(v);
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
            v->ctrl_left = 0; // новая нота: пересчитать сразу
        }
        float y = slot_next(s, v, s->live ? v->nheld > 0 : acid_seq_gate(&s->seq));
        if (acid_pump_active(&s->pump) || acid_djf_active(&s->djf)) {
            float kick = s->kick_gain * s->kick_vel * acid_kick_next(&s->kick);
            y += s->hat_gain * s->hat_vel * acid_hat_next(&s->hat);
            y += s->clap_gain * s->clap_vel * acid_clap_next(&s->clap);
            y += s->snare_gain * s->snare_vel * acid_snare_next(&s->snare);
            y += slot1[i];
            y = acid_djf_next(&s->djf, kick + acid_pump_next(&s->pump) * y);
        } else {
            // Нейтральные эффекты: прежний порядок сложения, автопилот бит в бит.
            y += s->kick_gain * s->kick_vel * acid_kick_next(&s->kick);
            y += s->hat_gain * s->hat_vel * acid_hat_next(&s->hat);
            y += s->clap_gain * s->clap_vel * acid_clap_next(&s->clap);
            y += s->snare_gain * s->snare_vel * acid_snare_next(&s->snare);
            if (slot1[i] != 0.0f) {
                y += slot1[i]; // молчащий слот 1 не трогает y: автопилот бит в бит прежний
            }
        }
        out[i] = s->master * OUT_PEAK * acid_tanh(s->drive * y);
    }
}

void acid_synth_render(acid_synth_t *s, float *out, size_t n)
{
    float slot1[MIX_CHUNK];
    for (size_t off = 0; off < n; off += MIX_CHUNK) {
        size_t m = n - off < MIX_CHUNK ? n - off : MIX_CHUNK;
        acid_synth_render_slot(s, 1, slot1, m);
        acid_synth_render_with(s, out + off, m, slot1);
    }
}


int acid_synth_take_save(acid_synth_t *s)
{
    int r = s->save;
    s->save = 0;
    return r;
}

size_t acid_synth_export(const acid_synth_t *s, void *buf, size_t cap)
{
    if (cap < ACID_SYNTH_BLOB) {
        return 0;
    }
    uint8_t *b = buf;
    memcpy(b, BLOB_MAGIC, sizeof BLOB_MAGIC);
    b += sizeof BLOB_MAGIC;
    for (int k = 0; k < ACID_SLOTS; k++) {
        *b++ = (uint8_t)s->slot[k].sel_type;
        memcpy(b, s->slot[k].knob, sizeof s->slot[k].knob);
        b += sizeof s->slot[k].knob;
    }
    return ACID_SYNTH_BLOB;
}

int acid_synth_import(acid_synth_t *s, const void *buf, size_t len)
{
    const uint8_t *b = buf;
    if (len != ACID_SYNTH_BLOB || memcmp(b, BLOB_MAGIC, sizeof BLOB_MAGIC) != 0) {
        return 0;
    }
    b += sizeof BLOB_MAGIC;
    for (int k = 0; k < ACID_SLOTS; k++) {
        acid_slot_t *v = &s->slot[k];
        v->sel_type = b[0] < ACID_TYPES ? b[0] : 0;
        memcpy(v->knob, b + 1, sizeof v->knob);
        b += 1 + sizeof v->knob;
        if (s->live || k == 1) {
            // Автопилот играет слотом 0 как 303: его выбор применится с BSP.
            v->type = v->next_type = v->sel_type;
            v->fade = 0;
            type_defaults(s, v);
        }
    }
    return 1;
}
