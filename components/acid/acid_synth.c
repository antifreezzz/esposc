#include "acid_synth.h"
#include "acid_prof.h"

#ifdef ACID_PROFILE
uint32_t acid_prof[ACID_PROF_N];
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "acid_math.h"
#include "acid_mem.h"

#define RES 0.85f
#define FENV_NORMAL_HZ 3000.0f
#define FENV_ACCENT_HZ 5000.0f
#define FENV_NORMAL_S 0.25f
#define FENV_ACCENT_RATIO 0.48f // спад акцента относительно обычной ноты (0.12 с при 0.25)
#define ACCENT_GAIN 1.4f
#define GLIDE_S 0.06f
#define CUTOFF_HZ 500.0f // база среза 303, пока ручку не трогали
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
#define CYM_GAIN_MAX 1.5f
// Громкость тарелок после включения (крэш, райд, купол): пик удара ~0.8 пика закрытого хэта на Naked Drums.
static const uint8_t CYM_KNOB[ACID_CYM_N] = {28, 35, 40};
#define SYNTH_GAIN 0.9f // общая громкость синтов: на 10% тише прежнего (просьба пользователя)
// Панорамы после включения: синты в центре, ударка немного разведена.
static const uint8_t PAN_DEFAULT[ACID_PANS] = {64, 64, 80, 48, 64};
#define PAGE_DRUMS2 2 // ударка 2: тарелки
#define PAGE_TOMS 3   // перкуссия: томы, рим, ковбелл
#define PAGE_COW 5
#define PAGE_FX 8
#define PAGE_TONAL 9
#define EARLY_NOTE 480 // нота моложе 10 мс на границе шага - на новый шаг сразу (джиттер MIDI)
#define PAGE_SYNTH1 6
#define MIX_CHUNK ACID_SPLIT_MAX // кусок буфера слота 1 в общем рендере
#define FADE_N 192     // 4 мс ухода и столько же прихода при смене типа синтеза
#define PLAITS_GAIN 1.0f // уровень Plaits относительно 303
#define IDLE_BLOCKS 400  // 100 мс точной тишины без гейта: молчащий Plaits не считаем
#define SEQ2_TYPE 11     // синт 2 по умолчанию: FM 2-op (модель Plaits 10)
#define PLAITS_ACCENT 0.4f // обычная нота Plaits тише акцентной на столько
#define KNOB_NONE 0xFF
#define RES_KNEE 112   // резонанс 303 выше - свист: самовозбуждение до ACID_LADDER_RES_MAX
#define DRIVE_ENC 13   // энкодеры 14-16 страницы синта: перегруз, тон, вой (у 303 и Plaits одни)
#define DRIVE_TONE 64  // тон и вой, пока их не трогали
#define TYPE_ENC 1       // энкодер 2 на странице синта
static const uint8_t BLOB_MAGIC[4] = {'A', 'S', '5', 3}; // 2: + ручки слоя тональности, 3: + слои синтов
static const uint8_t BLOB_MAGIC_V2[4] = {'A', 'S', '5', 2};
#define BLOB_V2 (4 + ACID_SLOTS * (1 + ACID_TYPES * ACID_KNOBS) + ACID_TONAL_KNOBS)
#define LAYER_GAIN_RAW 100 // громкость слоя после включения (FX 13-14)

// Сырые значения ручек после init для панели: CC, ближайшие к умолчаниям (обратные к lin/expo, округлено).
// По индексу энкодера с 0; страницы 3-7 и 9 берут значения из своих knob[], здесь не используются.
static const uint8_t RAW_DEFAULT[ACID_PAGES][ACID_KNOBS] = {
    [0] = {0, 127, 54, 85, 85, 92, 53, 64, 68, 0, 60, 64, 0, 64, 64, 0},
    [1] = {0, 55, 42, 76, 35, 78, 64, 64, 55, 59, 48, 76, 54, 80, 48, 64},
    [2] = {0, 28, 127, 64, 64, 35, 127, 64, 64, 40, 100, 0, 0, 0, 0, 0},
    [8] = {0, 0, 0, 0, 0, 0, 2, 53, 83, 0, 0, 127, LAYER_GAIN_RAW, LAYER_GAIN_RAW, 0, 0},
    [9] = {0, 0, 4, 0, 0, 1, 0, 0, 64, 64, 64, 64, 64, 64, 64, 64},
};
// Нетронутые ручки синта по типу: 303 и Plaits (перегруз 0, тон 64, вой 0 у обоих).
static const uint8_t RAW_303[ACID_KNOBS] = {0, 0, 69, 111, 64, 60, 51, 0, 67, 64, 0, 0, 0, 0, 64, 0};
static const uint8_t RAW_PLAITS[ACID_KNOBS] = {0, 0, 64, 64, 64, 38, 64, 67, 64, 64, 64, 64, 68, 0, 64, 0};

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
    acid_drive_init(&v->drv, sr);
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
    v->cut_base = CUTOFF_HZ;
    v->env_hz = FENV_NORMAL_HZ;
    v->decay_s = FENV_NORMAL_S;
    v->accent_amt = ACCENT_GAIN;
    v->gain = SYNTH_GAIN;
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
        s->slot[k].idx = k;
        type_defaults(s, &s->slot[k]);
        slot_init(&s->layer[k], sr, ACID_LAYER_OFF);
        s->layer[k].idx = k;
        type_defaults(s, &s->layer[k]);
        s->layer_gain[k] = (float)LAYER_GAIN_RAW / 127.0f;
    }
    s->save = 0;
    acid_kick_init(&s->kick, sr);
    acid_hat_init(&s->hat, sr);
    acid_clap_init(&s->clap, sr);
    acid_snare_init(&s->snare, sr);

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
    acid_clock_init(&s->clock, sr);
    acid_tonal_init(&s->tonal);
    s->clock_now = 0;
    acid_crush_init(&s->crush);
    if (!s->fx_mem) {
        // Дилэй читается каждый сэмпл: во внутреннюю память (в PSRAM он поднимал худший рендер).
        // Статтер работает, только пока включён: в PSRAM.
        s->fx_mem = acid_mem_big(2 * ACID_DELAY_LEN * sizeof *s->fx_mem, 1);
        s->st_mem = acid_mem_big(2 * ACID_STUTTER_LEN * sizeof *s->st_mem, 0);
        if (!s->fx_mem || !s->st_mem) {
            abort(); // без буферов эффектов играть нечем: лучше перезагрузка, чем запись по NULL
        }
    }
    acid_delay_init(&s->delay, sr, s->fx_mem);
    acid_stutter_init(&s->stutter, sr, s->st_mem);
    s->send1 = s->send2 = s->send_drums = 0.0f;
    for (int k = 0; k < ACID_PANS; k++) {
        acid_pan((float)PAN_DEFAULT[k], &s->pan_l[k], &s->pan_r[k]);
    }
    s->dl_time = s->delay.time_knob;
    s->dl_fb = 53;
    s->dl_tone = 83;
    acid_cym_init(&s->cym, sr);
    acid_perc_init(&s->perc, sr);
    s->tight = s->calm = s->hot = s->was2 = 0;
    s->calm_need = ACID_LOAD_CALM;
    for (int k = 0; k < ACID_SLOTS; k++) {
        s->layer_duck[k] = 1.0f;
    }
    s->load_avg = 0.0f;
    s->bank = NULL;
    for (int k = 0; k < 2; k++) {
        s->cym_tail[k] = 127;
        s->cym_tune[k] = 64;
        s->cym_pan[k] = 64;
    }
    for (int w = 0; w < ACID_CYM_N; w++) {
        s->cym.gain[w] = CYM_GAIN_MAX * (float)CYM_KNOB[w] / 127.0f;
    }
    memcpy(s->raw, RAW_DEFAULT, sizeof s->raw);
    s->page = PAGE_SYNTH1;
}

int acid_page_from_bsp(int v)
{
    if (v >= 0 && v < ACID_BSP_USER_FIRST) {
        return v;
    }
    if (v >= ACID_BSP_USER_FIRST && v < ACID_BSP_LAYER1) {
        return ACID_PAGES + v - ACID_BSP_USER_FIRST;
    }
    if (v >= ACID_BSP_LAYER1 && v < ACID_BSP_LAYER1 + ACID_SLOTS) {
        return ACID_PAGE_LAYER1 + v - ACID_BSP_LAYER1;
    }
    return -1;
}

int acid_page_to_bsp(int page)
{
    if (page >= 0 && page < ACID_PAGE_LAYER1) {
        return page;
    }
    if (page >= ACID_PAGE_LAYER1 && page < ACID_PAGES) {
        return ACID_BSP_LAYER1 + page - ACID_PAGE_LAYER1;
    }
    if (page >= ACID_PAGES && page < ACID_PAGES + ACID_BSP_LAYER1 - ACID_BSP_USER_FIRST) {
        return ACID_BSP_USER_FIRST + page - ACID_PAGES;
    }
    return -1;
}

void acid_synth_set_bank(acid_synth_t *s, const acid_bank_t *b)
{
    s->bank = b;
}

static int layer_on(const acid_slot_t *lv);

void acid_synth_set_load(acid_synth_t *s, float load)
{
    // Одиночный тяжёлый блок буфер выхода переживёт, лавину даёт устойчивый перегруз: смотрим на среднее.
    s->load_avg += (load - s->load_avg) * (1.0f / ACID_LOAD_AVG);
    load = s->load_avg;
    if (load >= ACID_LOAD_HIGH) {
        s->calm = 0;
        if (!s->tight) {
            s->tight = 1;
            s->hot = 0;
            acid_perc_set_limit(&s->perc, 1);
            acid_cym_set_limit(&s->cym, 1);
        }
        int layers = layer_on(&s->layer[0]) || layer_on(&s->layer[1]); // без слоёв ступени 2 нечего глушить
        if (s->tight == 1 && layers && (load >= ACID_LOAD_OVER || ++s->hot >= ACID_LOAD_STEP)) {
            // Одного голоса перкуссии не хватило (или и ждать нечего): слои приглушаются (layer_add).
            // Повтор без полного успокоения - слои возвращались зря: ждать вдвое дольше, без качелей.
            s->tight = 2;
            s->calm_need = s->was2 && s->calm_need < 16 * ACID_LOAD_CALM ? 2 * s->calm_need : s->calm_need;
            s->was2 = 1;
        }
    } else if (s->tight) {
        s->hot = 0;
        s->calm = load < ACID_LOAD_LOW ? s->calm + 1 : 0;
        if (s->calm >= (s->tight == 2 ? s->calm_need : ACID_LOAD_CALM)) {
            s->calm = 0;
            if (--s->tight == 0) { // назад по ступеням: сначала слои, потом перкуссия и тарелки
                acid_perc_set_limit(&s->perc, ACID_PERC_SOUNDING);
                acid_cym_set_limit(&s->cym, ACID_CYM_SLOTS - ACID_CYM_RESERVE);
                s->calm_need = ACID_LOAD_CALM;
                s->was2 = 0;
            }
        }
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
    v->fenv_amount = env;
}

void acid_synth_notes_off(acid_synth_t *s)
{
    // Звучащие ноты отпускаются, хвосты доигрывают без щелчка.
    for (int k = 0; k < 2 * ACID_SLOTS; k++) {
        acid_slot_t *v = k < ACID_SLOTS ? &s->slot[k] : &s->layer[k - ACID_SLOTS];
        v->nheld = 0;
        v->pend_note = -1;
        v->gliding = 0;
    }
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

static void note_release(acid_synth_t *s, acid_slot_t *v, int note);

// Нота BSP со сдвигом слоя тональности и транспозицией синта.
static int slot_note(const acid_synth_t *s, const acid_slot_t *v, int note)
{
    if (acid_tonal_moves(&s->tonal, v->idx)) {
        note = acid_tonal_note(&s->tonal, note);
    }
    return note + v->transpose;
}

static void note_on(acid_synth_t *s, acid_slot_t *v, int note, int vel)
{
    if (v->pend_note == note) {
        // Дребезг: та же нота сразу после короткого нажатия - это продолжение той же ноты.
        v->pend_note = -1;
        return;
    }
    if (v->pend_note >= 0) {
        note_release(s, v, v->pend_note); // короткая нота закончилась, новая не слайдит из неё
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
    voice_note(s, v, slot_note(s, v, note), vel > ACCENT_VEL, slide);
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
    note_release(s, v, note);
}

static void note_release(acid_synth_t *s, acid_slot_t *v, int note)
{
    int top = v->nheld > 0 && v->held[v->nheld - 1] == note;
    held_remove(v, note);
    if (top && v->nheld > 0) {
        // Отпустили верхнюю при зажатой нижней: глайд обратно, как в 303.
        v->target_log2 = log2f(acid_midi_to_hz(slot_note(s, v, v->held[v->nheld - 1])));
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
    } else if (acid_perc_inst_of(note, NULL) >= 0) {
        acid_perc_trigger(&s->perc, note, v);
    } else if (note == 49 || note == 51 || note == 53) {
        // Без пола: слои записанной динамики делит ручка чувствительности (страница 2).
        acid_cym_trigger(&s->cym, s->bank, note, (float)vel / 127.0f);
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

static void page_mix(acid_synth_t *s, int slot, int v)
{
    switch (slot) {
    case 1: s->master = lin(v, 0.0f, 1.0f); break;
    case 2: s->drive = expo(v, 0.3f, 3.0f); break;
    case 3:
    case 4: s->slot[slot - 3].gain = s->layer[slot - 3].gain = lin(v, 0.0f, 1.5f * SYNTH_GAIN); break; // и слою
    case 5: s->kick_gain = lin(v, 0.0f, 2.0f); break;
    case 6: s->hat_gain = lin(v, 0.0f, 0.6f); break;
    case 7: s->clap_gain = lin(v, 0.0f, 1.2f); break;
    case 8: s->snare_gain = lin(v, 0.0f, 1.5f); break;
    case 9: acid_pump_set_depth(&s->pump, lin(v, 0.0f, 1.0f)); break;
    case 10: acid_pump_set_release(&s->pump, expo(v, 0.03f, 0.9f)); break;
    case 11: acid_djf_set(&s->djf, v, s->djf.res_knob); break;
    case 12: acid_djf_set(&s->djf, s->djf.knob, v); break;
    case 13: acid_pan((float)v, &s->pan_l[ACID_PAN_SYNTH1], &s->pan_r[ACID_PAN_SYNTH1]); break;
    case 14: acid_pan((float)v, &s->pan_l[ACID_PAN_SYNTH2], &s->pan_r[ACID_PAN_SYNTH2]); break;
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
    case 13: acid_pan((float)v, &s->pan_l[ACID_PAN_HAT], &s->pan_r[ACID_PAN_HAT]); return;
    case 14: acid_pan((float)v, &s->pan_l[ACID_PAN_CLAP], &s->pan_r[ACID_PAN_CLAP]); return;
    case 15: acid_pan((float)v, &s->pan_l[ACID_PAN_SNARE], &s->pan_r[ACID_PAN_SNARE]); return;
    default: return;
    }
    acid_kick_set(&s->kick, s->kick_tune, s->kick_sweep, s->kick_pitch_s, s->kick_amp_s);
    acid_hat_set(&s->hat, s->hat_tone, s->hat_closed_s, s->hat_open_s);
    acid_clap_set(&s->clap, s->clap_tone, s->clap_tail_s);
    acid_snare_set(&s->snare, s->snare_tune, s->snare_snappy, s->snare_decay_s);
}

// Страница 2: тарелки. Крэш энкодеры 2-5, райд 6-9 (купол следом за райдом), 10 купол, 11 чувствительность.
static void page_drums2(acid_synth_t *s, int slot, int v)
{
    if (slot >= 1 && slot <= 8) {
        int k = (slot - 1) / 4, w = k == 0 ? ACID_CYM_CRASH : ACID_CYM_RIDE;
        float gain = s->cym.gain[w];
        switch ((slot - 1) % 4) {
        case 0: gain = lin(v, 0.0f, CYM_GAIN_MAX); break;
        case 1: s->cym_tail[k] = (uint8_t)v; break;
        case 2: s->cym_tune[k] = (uint8_t)v; break;
        default: s->cym_pan[k] = (uint8_t)v; break;
        }
        acid_cym_set(&s->cym, w, gain, s->cym_tail[k], s->cym_tune[k], s->cym_pan[k]);
    } else if (slot == 9) {
        acid_cym_set(&s->cym, ACID_CYM_BELL, lin(v, 0.0f, CYM_GAIN_MAX), 127, 64, 64);
    } else if (slot == 10) {
        acid_cym_set_sens(&s->cym, lin(v, 0.0f, 1.0f));
    }
}

static float bip(int v)
{
    float x = (float)(v - 64) / 63.0f;
    return x < -1.0f ? -1.0f : x;
}

// Транспозиция: на экране 64 - без сдвига, 52..76 - -12..+12 полутонов, остальное игнор.
static void transpose_set(acid_slot_t *sl, int v)
{
    if (v >= 52 && v <= 76) {
        sl->transpose = v - 64;
    }
}

// Резонанс 303: до RES_KNEE прежняя шкала (0.97 на 127), выше - до самовозбуждения.
static float res_knob(int v)
{
    if (v <= RES_KNEE) {
        return lin(v, 0.0f, 0.97f);
    }
    float k = lin(RES_KNEE, 0.0f, 0.97f);
    return k + (ACID_LADDER_RES_MAX - k) * (float)(v - RES_KNEE) / (float)(127 - RES_KNEE);
}

// Перегруз слота: общие энкодеры 303 и Plaits. Ручки перегруза помнятся по типу, как остальные.
static int drive_knob(acid_slot_t *sl, int slot, int v)
{
    acid_drive_t *d = &sl->drv;
    switch (slot) {
    case DRIVE_ENC: acid_drive_set(d, v, d->tone, d->howl); return 1;
    case DRIVE_ENC + 1: acid_drive_set(d, d->drive, v, d->howl); return 1;
    case DRIVE_ENC + 2: acid_drive_set(d, d->drive, d->tone, v); return 1;
    default: return 0;
    }
}

// Ручки 303, slot - индекс энкодера с 0.
static void knob_303(acid_synth_t *s, acid_slot_t *sl, int slot, int v)
{
    switch (slot) {
    case 2: sl->cut_base = expo(v, 60.0f, 3000.0f); break;
    case 3:
        sl->res = res_knob(v);
        acid_ladder_prep(&sl->lc, sl->g, sl->res); // резонанс - сразу, как раньше на каждом сэмпле
        break;
    case 4: sl->env_hz = lin(v, 0.0f, 6000.0f); break;
    case 5: sl->decay_s = expo(v, 0.05f, 1.5f); break;
    case 6: sl->accent_amt = lin(v, 1.0f, 2.0f); break;
    case 7: sl->wave = v <= 1 ? v : sl->wave; break; // 0 пила, 1 меандр
    case 8: sl->glide_coef = expf(-1.0f / (expo(v, 0.01f, 0.3f) * s->sr)); break;
    case 9: transpose_set(sl, v); break;
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
    case 8: transpose_set(sl, v); break;
    case 9: p->timbre_env = bip(v); break;
    case 10: p->morph_env = bip(v); break;
    case 11: p->fm_env = bip(v); break;
    case 12: sl->p_accent = lin(v, 0.0f, 0.75f); break;
    default: break;
    }
}

static void knob_apply(acid_synth_t *s, acid_slot_t *v, int slot, int val)
{
    if (v->type == ACID_LAYER_OFF || drive_knob(v, slot, val)) {
        return;
    }
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
    acid_drive_set(&v->drv, 0, DRIVE_TONE, 0);
    if (v->type == ACID_LAYER_OFF) {
        return; // слой выключен: ни модели, ни ручек
    }
    if (v->type == 0) {
        v->cut_base = CUTOFF_HZ;
        v->env_hz = FENV_NORMAL_HZ;
        v->decay_s = FENV_NORMAL_S;
        v->accent_amt = ACCENT_GAIN;
        v->res = RES;
        acid_ladder_prep(&v->lc, v->g, v->res);
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
        voice_note(s, v, slot_note(s, v, v->held[v->nheld - 1]), v->accented, 0);
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

// Страница эффектов: хруст, посылы и ручки дилэя, статтер.
static void page_fx(acid_synth_t *s, int slot, int v)
{
    switch (slot) {
    case 1: acid_crush_set(&s->crush, v, s->crush.rate_knob); break;
    case 2: acid_crush_set(&s->crush, s->crush.bits_knob, v); break;
    case 3: s->send1 = lin(v, 0.0f, 1.0f); break;
    case 4: s->send2 = lin(v, 0.0f, 1.0f); break;
    case 5: s->send_drums = lin(v, 0.0f, 1.0f); break;
    case 6: s->dl_time = v <= 4 ? v : s->dl_time; break;
    case 7: s->dl_fb = v; break;
    case 8: s->dl_tone = v; break;
    case 9: acid_stutter_set(&s->stutter, v); break;
    case 10: acid_delay_set_motion(&s->delay, v, s->delay.width); break;
    case 11: acid_delay_set_motion(&s->delay, s->delay.motion, v); break;
    case 12:
    case 13: s->layer_gain[slot - 12] = lin(v, 0.0f, 1.0f); break;
    case 14: s->layer_half = v >= 64; break;
    default: break;
    }
    if (slot >= 6 && slot <= 8) {
        acid_delay_set(&s->delay, s->dl_time, s->dl_fb, s->dl_tone);
    }
}

// Списки синта: значение вне списка звук не меняет и не запоминается (зеркало панели показывает действующее).
static int synth_knob_ok(int type, int slot, int v)
{
    if (slot == (type == 0 ? 9 : 8)) {
        return v >= 52 && v <= 76; // transpose
    }
    return !(type == 0 && slot == 7 && v > 1); // wave 303
}

// Страница синта: энкодер 2 - тип синтеза, дальше ручки типа (запоминаются по типам).
// Выключенный слой не считается и время его стоит: при включении он берёт стек нот и время у своего синта.
static void layer_sync(acid_slot_t *lv, const acid_slot_t *v)
{
    memcpy(lv->held, v->held, sizeof lv->held);
    memcpy(lv->held_at, v->held_at, sizeof lv->held_at);
    lv->nheld = v->nheld;
    lv->now = v->now;
    lv->pend_note = v->pend_note;
    lv->pend_at = v->pend_at;
}

// layer - страница слоя: у типа есть ещё значение ACID_LAYER_OFF.
static void page_synth(acid_synth_t *s, acid_slot_t *sl, int slot, int v, int layer)
{
    if (slot == TYPE_ENC) {
        if (layer && v < ACID_TYPES && sl->type == ACID_LAYER_OFF && sl->fade == 0) {
            layer_sync(sl, &s->slot[sl->idx]);
        }
        if (v < ACID_TYPES + layer) {
            type_request(sl, v); // значение на экране = номер типа, остальное игнор
        }
    } else if (slot > TYPE_ENC && sl->sel_type != ACID_LAYER_OFF && synth_knob_ok(sl->sel_type, slot, v)) {
        // Ручка - типу на экране. Если он ещё не зазвучал (уходит прежний), применится при переключении.
        sl->knob[sl->sel_type][slot] = (uint8_t)v;
        if (sl->sel_type == sl->type) {
            knob_apply(s, sl, slot, v);
        }
    }
}

// Списки страниц 0-2 и 8: значение вне списка ручку не меняет.
static int raw_ok(int page, int slot, int v)
{
    if (page == PAGE_FX) {
        return !((slot == 6 || slot == 9) && v > 4) && !(slot == 10 && v > 2);
    }
    return 1;
}

// Энкодер slot (с 1) на странице page, как будто BSP стоит на ней.
static void apply_page(acid_synth_t *s, int page, int slot, int v)
{
    if (page <= PAGE_DRUMS2 || page == PAGE_FX) {
        if (raw_ok(page, slot, v)) {
            s->raw[page][slot] = (uint8_t)v;
        }
    }
    if (page == 0) {
        page_mix(s, slot, v);
    } else if (page == 1) {
        page_drums(s, slot, v);
    } else if (page == PAGE_DRUMS2) {
        page_drums2(s, slot, v);
    } else if (page >= PAGE_TOMS && page <= PAGE_COW) {
        acid_perc_set(&s->perc, page - PAGE_TOMS, slot - 1, v); // энкодер 2 - первая ручка
    } else if (page == PAGE_FX) {
        page_fx(s, slot, v);
    } else if (page == PAGE_TONAL) {
        acid_tonal_set(&s->tonal, slot, v);
        s->save = 1; // квадрат - часть композиции, переживает перезагрузку
    } else if (page == PAGE_SYNTH1 || page == PAGE_SYNTH1 + 1) {
        page_synth(s, &s->slot[page - PAGE_SYNTH1], slot, v, 0);
    } else if (page >= ACID_PAGE_LAYER1 && page < ACID_PAGE_LAYER1 + ACID_SLOTS) {
        page_synth(s, &s->layer[page - ACID_PAGE_LAYER1], slot, v, 1);
    }
}

static void control_change(acid_synth_t *s, int num, int v)
{
    int slot = -1;
    for (int i = 0; i < 16; i++) {
        if (acid_bsp_enc_cc[i] == num) {
            slot = i;
        }
    }
    if (slot == 0) {
        int p = acid_page_from_bsp(v);
        if (p >= 0 && p < ACID_PAGES) {
            s->page = p; // значение на экране BSP - номер страницы, остальное (и свои страницы) не наше
        }
    } else if (slot > 0) {
        apply_page(s, s->page, slot, v);
    }
}

void acid_synth_set_knob(acid_synth_t *s, int page, int enc, int v)
{
    if (page >= 0 && page < ACID_PAGES && enc > 0 && enc < ACID_KNOBS && v >= 0 && v <= 127) {
        apply_page(s, page, enc, v);
    }
}

int acid_synth_knob(const acid_synth_t *s, int page, int enc)
{
    if (page < 0 || page >= ACID_PAGES || enc < 0 || enc >= ACID_KNOBS) {
        return 0;
    }
    if (page >= PAGE_TOMS && page <= PAGE_COW) {
        return enc > 0 ? s->perc.inst[page - PAGE_TOMS].knob[enc - 1] : 0;
    }
    if (page == PAGE_TONAL) {
        return s->tonal.knob[enc] != KNOB_NONE ? s->tonal.knob[enc] : RAW_DEFAULT[PAGE_TONAL][enc];
    }
    int layer = page >= ACID_PAGE_LAYER1 && page < ACID_PAGE_LAYER1 + ACID_SLOTS;
    if (page == PAGE_SYNTH1 || page == PAGE_SYNTH1 + 1 || layer) {
        const acid_slot_t *sl = layer ? &s->layer[page - ACID_PAGE_LAYER1] : &s->slot[page - PAGE_SYNTH1];
        if (enc == TYPE_ENC) {
            return sl->sel_type;
        }
        if (sl->sel_type == ACID_LAYER_OFF) {
            return 0;
        }
        uint8_t k = sl->knob[sl->sel_type][enc];
        return k != KNOB_NONE ? k : (sl->sel_type == 0 ? RAW_303 : RAW_PLAITS)[enc];
    }
    return s->raw[page][enc];
}

// Граница шага слоя: звучащие ноты синтов по режиму энкодера 6, свежая нота - сразу на новый шаг.
static void tonal_boundary(acid_synth_t *s)
{
    for (int i = 0; i < 2 * ACID_SLOTS; i++) {
        acid_slot_t *v = i < ACID_SLOTS ? &s->slot[i] : &s->layer[i - ACID_SLOTS]; // слой - вслед за синтом
        if (v->nheld == 0 || !acid_tonal_moves(&s->tonal, v->idx)) {
            continue;
        }
        int young = v->now - v->held_at[v->nheld - 1] < EARLY_NOTE;
        int mode = young ? 2 : s->tonal.held_mode;
        if (mode == 0) {
            continue;
        }
        v->target_log2 = log2f(acid_midi_to_hz(slot_note(s, v, v->held[v->nheld - 1])));
        if (mode == 1) {
            v->gliding = 1;
        } else {
            v->pitch_log2 = v->target_log2; // прыжок без перезапуска огибающих
            v->gliding = 0;
            v->ctrl_left = 0;
        }
    }
}

void acid_synth_midi(acid_synth_t *s, const acid_midi_event_t *ev)
{
    switch (ev->type) {
    case ACID_MIDI_NOTE_ON:
        if (ev->ch == BASS_CH || ev->ch == SEQ2_CH) {
            note_on(s, &s->slot[ev->ch == SEQ2_CH], ev->d1, ev->d2);
            note_on(s, &s->layer[ev->ch == SEQ2_CH], ev->d1, ev->d2); // тот же стек нот, что у синта
        } else if (ev->ch == DRUM_CH) {
            drum_on(s, ev->d1, ev->d2);
        }
        break;
    case ACID_MIDI_NOTE_OFF:
        if (ev->ch == BASS_CH || ev->ch == SEQ2_CH) {
            note_off(s, &s->slot[ev->ch == SEQ2_CH], ev->d1);
            note_off(s, &s->layer[ev->ch == SEQ2_CH], ev->d1);
        }
        break;
    case ACID_MIDI_CC:
        control_change(s, ev->d1, ev->d2);
        break;
    case ACID_MIDI_CLOCK: {
        float spb = acid_clock_spb(&s->clock);
        acid_clock_tick(&s->clock, s->clock_now);
        if (acid_clock_spb(&s->clock) != spb) {
            acid_delay_set_spb(&s->delay, acid_clock_spb(&s->clock));
            acid_stutter_set_spb(&s->stutter, acid_clock_spb(&s->clock));
        }
        if (acid_tonal_tick(&s->tonal)) {
            tonal_boundary(s);
        }
        break;
    }
    case ACID_MIDI_START:
        acid_tonal_start(&s->tonal);
        break;
    case ACID_MIDI_STOP:
        acid_synth_notes_off(s); // защита от залипших нот
        break;
    default:
        break;
    }
}

// Частота, на которой считается слот: у слоя на половинной частоте вдвое ниже.
static inline float slot_sr(const acid_synth_t *s, const acid_slot_t *v)
{
    return v->half ? 0.5f * s->sr : s->sr;
}

// Пересчёт дорогих величин раз в CTRL_BLOCK сэмплов.
static float control_update(acid_synth_t *s, acid_slot_t *v)
{
    float hz = exp2f(v->pitch_log2);
    acid_osc_set_freq(&v->osc, hz, slot_sr(s, v));
    if (acid_drive_on(&v->drv)) {
        acid_drive_pitch(&v->drv, hz);
    }

    float cutoff = v->cut_base + v->fenv_amount * v->fenv.value;
    return acid_ladder_coef(cutoff, slot_sr(s, v));
}

// Время слота: отложенное отпускание короткой ноты.
static void slot_tick(acid_synth_t *s, acid_slot_t *v)
{
    v->now++;
    if (v->pend_note >= 0 && (int32_t)(v->now - v->pend_at) >= 0) {
        note_release(s, v, v->pend_note);
        v->pend_note = -1;
    }
}

// Сэмпл голоса Plaits с громкостью слота: блоки по 12, молчащий голос не считаем.
static float plaits_next(acid_slot_t *v, int gate)
{
    if (v->gliding) {
        float gc = v->half ? v->glide_coef * v->glide_coef : v->glide_coef; // полюс за два сэмпла
        v->pitch_log2 = v->target_log2 + (v->pitch_log2 - v->target_log2) * gc;
    }
    if (v->ppos == ACID_PLAITS_BLOCK) {
        v->ppos = 0;
        if (acid_drive_on(&v->drv)) {
            acid_drive_pitch(&v->drv, acid_exp2(v->pitch_log2));
        }
        if (!gate && v->idle >= IDLE_BLOCKS) {
            memset(v->pbuf, 0, sizeof v->pbuf);
        } else {
            acid_plaits_params_t *p = &v->pp;
            p->note = 12.0f * (v->pitch_log2 - log2f(440.0f)) + 69.0f + (v->half ? 12.0f : 0.0f); // Plaits на 48 кГц
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
    float y = v->pbuf[v->ppos++];
    if (acid_drive_on(&v->drv)) {
        return acid_drive_run(&v->drv, y * PLAITS_GAIN) * v->gain; // громкость слота после: не меняет характер
    }
    return y * (PLAITS_GAIN * v->gain);
}

// Сэмпл голоса 303 с громкостью слота.
static float voice_next(acid_synth_t *s, acid_slot_t *v, int gate)
{
    acid_amp_gate(&v->amp, gate);
    if (v->gliding) {
        float gc = v->half ? v->glide_coef * v->glide_coef : v->glide_coef; // полюс за два сэмпла
        v->pitch_log2 = v->target_log2 + (v->pitch_log2 - v->target_log2) * gc;
    }
    if (v->ctrl_left == 0) {
        v->g = control_update(s, v);
        acid_ladder_prep(&v->lc, v->g, v->res);
        v->ctrl_left = CTRL_BLOCK;
    }
    v->ctrl_left--;

    float x = v->wave ? acid_osc_square(&v->osc) : acid_osc_saw(&v->osc);
    float amp;
    if (v->half) {
        acid_decay_next2(&v->fenv);
        amp = acid_amp_next2(&v->amp);
    } else {
        acid_decay_next(&v->fenv);
        amp = acid_amp_next(&v->amp);
    }
    float y = acid_ladder_tick(&v->flt, x, &v->lc);
    amp *= v->accent_gain;
    if (acid_drive_on(&v->drv)) {
        return acid_drive_run(&v->drv, y * amp) * v->gain; // акцент бьёт в перегруз сильнее, громкость слота после
    }
    return y * (amp * v->gain);
}

// Громкость смены типа: уход, переключение, приход.
static float fade_step(acid_synth_t *s, acid_slot_t *v)
{
    int step = v->half ? 2 : 1; // на половинной частоте сэмпл слоя - за два
    if (v->fade > 0) {
        float g = (float)v->fade / FADE_N;
        v->fade -= step;
        if (v->fade <= 0) {
            v->fade = 0;
            type_switch(s, v);
        }
        return g;
    }
    float g = (float)(FADE_N + v->fade) / FADE_N;
    v->fade = v->fade + step > 0 ? 0 : v->fade + step;
    return g;
}

// Сэмпл слота с громкостью: голос его типа, при смене типа - с уходом и приходом.
static float slot_next(acid_synth_t *s, acid_slot_t *v, int gate)
{
    float y = v->type == 0 ? voice_next(s, v, gate) : v->type == ACID_LAYER_OFF ? 0.0f : plaits_next(v, gate);
    if (v->fade != 0) {
        y *= fade_step(s, v);
    }
    return y;
}

// Слой звучит: тип не OFF или ещё идёт уход в OFF. Выключенный не считается и в смесь не попадает.
static int layer_on(const acid_slot_t *lv)
{
    return lv->type != ACID_LAYER_OFF || lv->fade != 0;
}

// Слой на половинную частоту или обратно: перегруз под новую частоту, высота и фильтр - на ближайшем сэмпле.
static void layer_rate(acid_synth_t *s, acid_slot_t *lv, int half)
{
    lv->half = half;
    lv->hph = 0;
    acid_drive_set_rate(&lv->drv, slot_sr(s, lv));
    lv->ctrl_left = 0;
}

// Сэмпл слоя синта k, добавить к y. Время слоя идёт всегда: стек нот совпадает с синтом.
static inline float layer_add(acid_synth_t *s, int k, float y)
{
    acid_slot_t *lv = &s->layer[k];
    if (!layer_on(lv)) {
        return y; // выключенный слой не трогаем вовсе: стек нот возьмёт у синта при включении
    }
    slot_tick(s, lv);
    // Защита на ступени 2: слой плавно уходит и, затихнув, не считается. Своя громкость у каждого слоя:
    // слои на разных ядрах.
    float *d = &s->layer_duck[k];
    if (s->tight == 2) {
        if (*d == 0.0f) {
            return y;
        }
        *d = *d > ACID_DUCK_DOWN ? *d - ACID_DUCK_DOWN : 0.0f;
    } else if (*d < 1.0f) {
        *d = *d < 1.0f - ACID_DUCK_UP ? *d + ACID_DUCK_UP : 1.0f;
    }
    if (s->layer_half != lv->half) {
        layer_rate(s, lv, s->layer_half);
    }
    float out;
    if (!lv->half) {
        out = slot_next(s, lv, lv->nheld > 0);
        lv->hlast = out;
    } else if ((lv->hph ^= 1) != 0) {
        // Половинная частота: считаем каждый второй сэмпл, между ними середина.
        float n = slot_next(s, lv, lv->nheld > 0);
        out = 0.5f * (lv->hlast + n);
        lv->hlast = n;
    } else {
        out = lv->hlast;
    }
    return y + s->layer_gain[k] * *d * out;
}

void acid_synth_render_slot(acid_synth_t *s, int k, float *out, size_t n)
{
    acid_slot_t *v = &s->slot[k];
    for (size_t i = 0; i < n; i++) {
        slot_tick(s, v);
        out[i] = layer_add(s, k, slot_next(s, v, v->nheld > 0));
    }
}

void acid_synth_render_layer(acid_synth_t *s, int k, float *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        out[i] = layer_add(s, k, 0.0f);
    }
}

void acid_synth_render_side(acid_synth_t *s, acid_synth_side_t *side, size_t n)
{
    // Тарелки и перкуссия кусками: голос за голосом быстрее.
    ACID_PROF_T(t);
    acid_cym_render(&s->cym, side->cyl, side->cyr, (int)n);
    ACID_PROF_ADD(ACID_PROF_CYM, t);
    acid_perc_render(&s->perc, side->pcl, side->pcr, (int)n);
    ACID_PROF_ADD(ACID_PROF_PERC, t);
}

void acid_synth_render_src(acid_synth_t *s, acid_synth_src_t *src, size_t n)
{
    acid_slot_t *v = &s->slot[0];
    ACID_PROF_T(t);
    for (size_t i = 0; i < n; i++) {
        slot_tick(s, v);
        src->y[i] = slot_next(s, v, v->nheld > 0);
        s->clock_now++;
        ACID_PROF_ADD(ACID_PROF_SYNTH1, t);
        src->kick[i] = s->kick_gain * s->kick_vel * acid_kick_next(&s->kick);
        src->hat[i] = s->hat_gain * s->hat_vel * acid_hat_next(&s->hat);
        src->clap[i] = s->clap_gain * s->clap_vel * acid_clap_next(&s->clap);
        src->snare[i] = s->snare_gain * s->snare_vel * acid_snare_next(&s->snare);
        ACID_PROF_ADD(ACID_PROF_DRUMS, t);
    }
}

void acid_synth_render_mix_lr(acid_synth_t *s, float *l, float *r, size_t n, const float *slot1, const float *layer0,
                              const acid_synth_src_t *src, const acid_synth_side_t *side)
{
    ACID_PROF_T(t);
    // Стерео: панорамы по источникам, бочка в центре.
    const float *pl = s->pan_l, *pr = s->pan_r;
    for (size_t i = 0; i < n; i++) {
        float y = layer0 ? src->y[i] + layer0[i] : src->y[i], kick = src->kick[i], hat = src->hat[i], clap = src->clap[i], snare = src->snare[i];
        float s1 = slot1[i];
        float cl = side->cyl[i], cr = side->cyr[i]; // уже стерео с балансом
        float ql = side->pcl[i], qr = side->pcr[i]; // перкуссия, уже с панорамой
        // Посылы до пампинга и панорамы: повторы не качаются вместе с сухим звуком.
        float send = s->send1 * y + s->send2 * s1 + s->send_drums * (hat + clap + snare + 0.5f * (cl + cr + ql + qr));
        float pump = acid_pump_next(&s->pump);
        float yl = kick + pump * (pl[ACID_PAN_SYNTH1] * y + pl[ACID_PAN_SYNTH2] * s1 + pl[ACID_PAN_HAT] * hat +
                                  pl[ACID_PAN_CLAP] * clap + pl[ACID_PAN_SNARE] * snare + cl + ql);
        float yr = kick + pump * (pr[ACID_PAN_SYNTH1] * y + pr[ACID_PAN_SYNTH2] * s1 + pr[ACID_PAN_HAT] * hat +
                                  pr[ACID_PAN_CLAP] * clap + pr[ACID_PAN_SNARE] * snare + cr + qr);
        if (send != 0.0f || acid_delay_active(&s->delay)) {
            float wl, wr;
            acid_delay_next2(&s->delay, send, &wl, &wr);
            yl += wl;
            yr += wr;
        }
        if (acid_stutter_active(&s->stutter)) {
            acid_stutter_next2(&s->stutter, &yl, &yr);
        }
        if (acid_crush_active(&s->crush)) {
            acid_crush_next2(&s->crush, &yl, &yr);
        }
        acid_djf_next2(&s->djf, &yl, &yr);
        l[i] = s->master * OUT_PEAK * acid_tanh(s->drive * yl);
        r[i] = s->master * OUT_PEAK * acid_tanh(s->drive * yr);
    }
    ACID_PROF_ADD(ACID_PROF_FX, t);
}

void acid_synth_render_with_lr(acid_synth_t *s, float *l, float *r, size_t n, const float *slot1)
{
    acid_synth_side_t side;
    acid_synth_src_t src;
    float layer0[ACID_SPLIT_MAX];
    for (size_t off = 0; off < n; off += ACID_SPLIT_MAX) {
        size_t m = n - off < ACID_SPLIT_MAX ? n - off : ACID_SPLIT_MAX;
        acid_synth_render_layer(s, 0, layer0, m);
        acid_synth_render_src(s, &src, m);
        acid_synth_render_side(s, &side, m);
        acid_synth_render_mix_lr(s, l + off, r + off, m, slot1 + off, layer0, &src, &side);
    }
}

void acid_synth_render_lr(acid_synth_t *s, float *l, float *r, size_t n)
{
    float slot1[MIX_CHUNK];
    for (size_t off = 0; off < n; off += MIX_CHUNK) {
        size_t m = n - off < MIX_CHUNK ? n - off : MIX_CHUNK;
        acid_synth_render_slot(s, 1, slot1, m);
        acid_synth_render_with_lr(s, l + off, r + off, m, slot1);
    }
}

// Моно: среднее каналов.
void acid_synth_render_with(acid_synth_t *s, float *out, size_t n, const float *slot1)
{
    float l[MIX_CHUNK], r[MIX_CHUNK];
    for (size_t off = 0; off < n; off += MIX_CHUNK) {
        size_t m = n - off < MIX_CHUNK ? n - off : MIX_CHUNK;
        acid_synth_render_with_lr(s, l, r, m, slot1 + off);
        for (size_t i = 0; i < m; i++) {
            out[off + i] = 0.5f * (l[i] + r[i]);
        }
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
    for (int k = 0; k < 2 * ACID_SLOTS; k++) {
        const acid_slot_t *v = k < ACID_SLOTS ? &s->slot[k] : &s->layer[k - ACID_SLOTS];
        *b++ = (uint8_t)v->sel_type;
        memcpy(b, v->knob, sizeof v->knob);
        b += sizeof v->knob;
    }
    memcpy(b, s->tonal.knob, sizeof s->tonal.knob);
    return ACID_SYNTH_BLOB;
}

int acid_synth_import(acid_synth_t *s, const void *buf, size_t len)
{
    const uint8_t *b = buf;
    // Версия 2 (до слоёв): только синты, слои остаются как после init.
    int v2 = len == BLOB_V2 && memcmp(b, BLOB_MAGIC_V2, sizeof BLOB_MAGIC_V2) == 0;
    if (!v2 && (len != ACID_SYNTH_BLOB || memcmp(b, BLOB_MAGIC, sizeof BLOB_MAGIC) != 0)) {
        return 0;
    }
    b += sizeof BLOB_MAGIC;
    for (int k = 0; k < (v2 ? 1 : 2) * ACID_SLOTS; k++) {
        acid_slot_t *v = k < ACID_SLOTS ? &s->slot[k] : &s->layer[k - ACID_SLOTS];
        int top = k < ACID_SLOTS ? ACID_TYPES : ACID_TYPES + 1; // у слоя есть OFF
        v->sel_type = b[0] < top ? b[0] : (k < ACID_SLOTS ? 0 : ACID_LAYER_OFF);
        memcpy(v->knob, b + 1, sizeof v->knob);
        b += 1 + sizeof v->knob;
        v->type = v->next_type = v->sel_type;
        v->fade = 0;
        type_defaults(s, v);
    }
    for (int i = 0; i < ACID_TONAL_KNOBS; i++) {
        if (b[i] != KNOB_NONE) {
            acid_tonal_set(&s->tonal, i, b[i]);
        }
    }
    return 1;
}
