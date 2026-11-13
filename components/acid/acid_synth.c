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
#include "acid_mod.h"

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
#define IDLE_BLOCKS 400  // 100 мс тишины без гейта: молчащий Plaits не считаем
// Тишина Plaits: его выход после ноты не ноль, а +1 LSB (Clip16(1 + x)), у six-op хвост ±6 LSB. 8 LSB ~ -72 дБFS.
#define PLAITS_QUIET (8.0f / 32768.0f)
#define AMP_SLEEP 1e-5f // 303 без гейта тише -100 дБ: голос не считаем
#define SEQ2_TYPE 11     // синт 2 по умолчанию: FM 2-op (модель Plaits 10)
#define PLAITS_ACCENT 0.4f // обычная нота Plaits тише акцентной на столько
#define KNOB_NONE 0xFF
#define RES_KNEE 112   // резонанс 303 выше - свист: самовозбуждение до ACID_LADDER_RES_MAX
#define DRIVE_ENC 13   // энкодеры 14-16 страницы синта: перегруз, тон, вой (у 303 и Plaits одни)
#define DRIVE_TONE 64  // тон и вой, пока их не трогали
#define TYPE_ENC 1       // энкодер 2 на странице синта
// 2: + ручки слоя тональности, 3: + слои синтов, 4: + PETRI, 5: + страницы микса, ударки, эффектов, Grids, чашки,
// 6: + пять ручек реверба (wet на FX 15 вместо LYR HALF), 7: LYR HALF на FX 15, wet на FX 16,
// 8: + ширина реверба, 9: + PITCH (шаг слайда S1/S2, строй), 10: + GATE S1/S2 на PITCH.
static const uint8_t BLOB_MAGIC[4] = {'A', 'S', '5', 10};
static const uint8_t RV_DEFAULT[ACID_REVERB_KNOBS] = {64, 64, 64, 64, 50, 127};
// Строи: отклонение ступени (полутонов от тоники страницы 9) от равномерного, в центах. Чистый - 5-предельный,
// пифагоров и среднетоновый (1/4 коммы) - цепью квинт от Eb до G#; RAST и BAYATI - четвертьтона макамов.
static const float TUNING_CENTS[ACID_TUNINGS][12] = {
    [ACID_TUNING_JUST] = {0, 11.73f, 3.91f, 15.64f, -13.69f, -1.96f, -17.49f, 1.96f, 13.69f, -15.64f, 17.60f, -11.73f},
    [ACID_TUNING_PYTHAG] = {0, -9.78f, 3.91f, -5.87f, 7.82f, -1.96f, 11.73f, 1.96f, -7.82f, 5.87f, -3.91f, 9.78f},
    [ACID_TUNING_MEANTONE] = {0, -23.95f, -6.84f, 10.26f, -13.69f, 3.42f, -20.53f, -3.42f, -27.37f, -10.26f, 6.84f,
                              -17.11f},
    [ACID_TUNING_RAST] = {[4] = -50, [11] = -50}, // E и B на четверть тона ниже
    [ACID_TUNING_BAYATI] = {[2] = -50},           // вторая ступень на четверть тона ниже
};
#define OLD_TYPES 25 // версии 2-3: без PETRI, у слоя «выкл» = 25
#define BLOB_OLD(slots) (4 + (slots) * (1 + OLD_TYPES * ACID_KNOBS) + ACID_TONAL_KNOBS)
#define LAYER_GAIN_RAW 100 // громкость слоя после включения (FX 13-14)
#define PETRI_MODE_ENC 8   // энкодер 9 типа PETRI: режим рельефа 0-3 (список)
#define PETRI_SHAPE_ENC 10 // энкодер 11: форма орбиты 0-4 (список)
#define PETRI_GAIN 1.0f    // уровень PETRI относительно 303 (подобрать на слух)
#define PETRI_MOD_K 0.022f // сглаживание модуляций за CTRL_BLOCK: ~15 мс

// Сырые значения ручек после init для панели: CC, ближайшие к умолчаниям (обратные к lin/expo, округлено).
// По индексу энкодера с 0; страницы 3-7 и 9 берут значения из своих knob[], здесь не используются.
static const uint8_t RAW_DEFAULT[ACID_PAGES][ACID_KNOBS] = {
    [0] = {0, 127, 54, 85, 85, 92, 53, 64, 68, 0, 60, 64, 0, 64, 64, 0},
    [1] = {0, 55, 42, 76, 35, 78, 64, 64, 55, 59, 48, 76, 54, 80, 48, 64},
    [2] = {0, 28, 127, 64, 64, 35, 127, 64, 64, 40, 100, 0, 0, 0, 0, 0},
    [8] = {0, 0, 0, 0, 0, 0, 2, 53, 83, 0, 0, 127, LAYER_GAIN_RAW, LAYER_GAIN_RAW, 0, 0},
    [9] = {0, 0, 4, 0, 0, 1, 0, 0, 64, 64, 64, 64, 64, 64, 64, 64},
    // Чашка: Митоз, F и k без сдвига, 4 шага, жители 3/1/2/1, аппетит 0.15, мутагены x1, маршруты -0.6/+0.7/+0.6.
    [ACID_PAGE_PETRI] = {0, 0, 64, 64, 4, 3, 1, 2, 1, 38, 42, 26, 108, 102, 0, 0},
};
// Страницы без своих knob[] синтов: их ручки в blob после тональности (версия 5).
static const uint8_t SAVED_PAGES[ACID_SAVED_PAGES] = {0, 1, 2, 3, 4, 5, 8, ACID_PAGE_GRIDS, ACID_PAGE_PETRI};
// Списки страницы «Чашка» (индекс энкодера с 0): пресет, шаги, численности. 0 - не список.
static const uint8_t PETRI_PAGE_MAX[ACID_KNOBS] = {0, 5, 0, 0, 6, 16, 4, 4, 3, 0, 0, 0, 0, 0, 0, 1};
#define PETRI_FREEZE_ENC 14 // энкодер 15: фриз (64 и выше вкл, каждый переход вверх - переснять)
#define FX_STUTTER_ENC 9    // энкодер 10 страницы эффектов: статтер

// Исполнительские ручки (статтер, фриз, сбивка Grids сейчас): из памяти не восстанавливаются, после включения
// не должны заиграть сами.
static int performance_knob(int page, int enc)
{
    return (page == PAGE_FX && enc == FX_STUTTER_ENC) || (page == ACID_PAGE_PETRI && enc == PETRI_FREEZE_ENC) ||
           (page == ACID_PAGE_GRIDS && enc == DRM_FILL_NOW);
}
// Нетронутые ручки синта по типу: 303 и Plaits (перегруз 0, тон 64, вой 0 у обоих).
static const uint8_t RAW_303[ACID_KNOBS] = {0, 0, 69, 111, 64, 60, 51, 0, 67, 64, 0, 0, 0, 0, 64, 0};
static const uint8_t RAW_PLAITS[ACID_KNOBS] = {0, 0, 64, 64, 64, 38, 64, 67, 64, 64, 64, 64, 68, 0, 64, 0};
// PETRI: срез ~540 Гц, рез 0.6, огибающая 0.5, спад 0.35 с, атака 10 мс, релиз 0.6 с, Высота, контраст 0, Круг,
// радиус 18 клеток, дрейф 2 клетки/с.
static const uint8_t RAW_PETRI[ACID_KNOBS] = {0, 0, 57, 79, 64, 74, 38, 86, 0, 0, 0, 44, 17, 0, 64, 0};

static void type_defaults(acid_synth_t *s, acid_slot_t *v);

static void slot_init(acid_slot_t *v, float sr, int type)
{
    v->type = v->sel_type = v->next_type = type;
    v->fade = 0;
    v->half = v->hph = 0;
    v->hlast = 0.0f;
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
    v->ctl_log2 = NAN; // первый пересчёт считает частоту
    acid_ladder_reset(&v->flt);
    acid_drive_init(&v->drv, sr);
    acid_decay_init(&v->fenv, FENV_NORMAL_S, sr);
    acid_amp_init(&v->amp, 0.003f, 0.01f, sr);
    v->gstep = v->ginv = 0.0f;
    v->gstep_inc = v->gphase = 0.0f;
    v->glide_s = GLIDE_S;
    v->glide_coef = expf(-1.0f / (GLIDE_S * sr));
    v->gramp = 0;
    v->gart = 0;
    v->ggate = 1.0f;
    v->rtg_gate_left = -1;
    v->seq_inc = v->seq_step_inc = 0.0f;
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
    s->seq = NULL;
    memset(s->bsp_held, 0, sizeof s->bsp_held);
    s->drone = 0;
    for (int k = 0; k < ACID_SLOTS; k++) {
        slot_init(&s->slot[k], sr, k == 1 ? SEQ2_TYPE : 0);
        s->slot[k].idx = k;
        acid_petri_init(&s->petri[k], sr);
        acid_petri_use_baked(&s->petri[k], acid_petri_baked, acid_petri_nbaked);
        s->slot[k].pet = &s->petri[k];
        type_defaults(s, &s->slot[k]);
        slot_init(&s->layer[k], sr, ACID_LAYER_OFF);
        s->layer[k].idx = k;
        s->layer[k].pet = NULL;
        type_defaults(s, &s->layer[k]);
        s->layer_gain[k] = (float)LAYER_GAIN_RAW / 127.0f;
    }
    s->layer_half = 0;
    s->petri_depth[0] = -0.6f;
    s->petri_depth[1] = 0.7f;
    s->petri_depth[2] = 0.6f;
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
    }
    if (!s->rv_mem) {
        s->rv_mem = acid_mem_big(ACID_REVERB_MEM * sizeof *s->rv_mem, 1);
    }
    if (!s->fx_mem || !s->st_mem || !s->rv_mem) {
        abort(); // без буферов эффектов играть нечем: лучше перезагрузка, чем запись по NULL
    }
    acid_delay_init(&s->delay, sr, s->fx_mem);
    acid_stutter_init(&s->stutter, sr, s->st_mem);
    acid_reverb_init(&s->reverb, s->rv_mem);
    memcpy(s->rv_raw, RV_DEFAULT, sizeof s->rv_raw);
    memset(s->pitch_raw, 0, sizeof s->pitch_raw);
    s->pitch_raw[3] = 127;
    s->pitch_raw[4] = 127;
    s->rv_wet = 0.0f;
    s->rv_send1 = s->rv_send2 = s->rv_send_drums = 64.0f / 127.0f;
    s->rv_duck = 1.0f;
    s->rv_narrow = 0.0f;
    memset(s->rv_prev, 0, sizeof s->rv_prev);
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
    acid_beat_init(&s->beat);
    acid_drummer_init(&s->drummer);
    memcpy(s->raw[ACID_PAGE_GRIDS], s->drummer.knob, ACID_KNOBS);
    s->grids_step = -1;
    s->grids_hits = 0;
    s->grids_swing = 0;
    s->page = PAGE_SYNTH1;
    memset(s->mod_base, KNOB_NONE, sizeof s->mod_base);
    memset(s->mod_mask, 0, sizeof s->mod_mask);
    memset(s->mod_unset, 0, sizeof s->mod_unset);
    s->trig = 0;
}

int acid_page_from_bsp(int v)
{
    if (v == ACID_BSP_PETRI) {
        return ACID_PAGE_PETRI;
    }
    if (v >= ACID_BSP_MOD_FIRST && v < ACID_BSP_MOD_FIRST + ACID_MODS) {
        return ACID_PAGE_MOD1 + v - ACID_BSP_MOD_FIRST;
    }
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
    if (page == ACID_PAGE_PETRI) {
        return ACID_BSP_PETRI;
    }
    if (page >= ACID_PAGE_MOD1 && page < ACID_PAGE_MOD1 + ACID_MODS) {
        return ACID_BSP_MOD_FIRST + page - ACID_PAGE_MOD1;
    }
    if (page >= 0 && page < ACID_PAGE_LAYER1) {
        return page;
    }
    if (page >= ACID_PAGE_LAYER1 && page < ACID_PAGE_LAYER1 + ACID_SLOTS) {
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
        int layers = layer_on(&s->layer[0]) || layer_on(&s->layer[1]); // без слоёв ступеням 2-3 нечего делать
        if (s->tight == 1 && layers && (load >= ACID_LOAD_OVER || ++s->hot >= ACID_LOAD_STEP)) {
            // Одного голоса перкуссии не хватило (или и ждать нечего): слои на половинную частоту, а если они
            // уже там ручкой - сразу приглушаются (layer_add).
            // Повтор без полного успокоения - слои возвращались зря: ждать вдвое дольше, без качелей.
            s->tight = s->layer_half ? 3 : 2;
            s->hot = 0;
            s->calm_need = s->was2 && s->calm_need < 16 * ACID_LOAD_CALM ? 2 * s->calm_need : s->calm_need;
            s->was2 = 1;
        } else if (s->tight == 2 && (++s->hot >= ACID_LOAD_STEP || (load >= ACID_LOAD_OVER && s->hot >= ACID_LOAD_SETTLE))) {
            s->tight = 3; // половинной частоты мало
        }
    } else if (s->tight) {
        s->hot = 0;
        s->calm = load < ACID_LOAD_LOW ? s->calm + 1 : 0;
        if (s->calm >= (s->tight >= 2 ? s->calm_need : ACID_LOAD_CALM)) {
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

static void slot_set_glide(acid_slot_t *sl, float sr, float t)
{
    sl->glide_s = t;
    sl->glide_coef = expf(-1.0f / (t * sr));
    sl->gstep_inc = sl->gstep > 0.0f ? 1.0f / (t * sr) : 0.0f;
}

// Звучащая высота: на лесенке pitch_log2 уже идёт ступенями;
// на полулесенке первую половину ступени держим, вторую едем к следующей.
static inline float pitch_out(const acid_slot_t *v)
{
    if (!v->gramp || v->gstep == 0.0f || !v->gliding || v->gphase <= 0.5f) {
        return v->pitch_log2;
    }
    float f = 2.0f * (v->gphase - 0.5f);
    float diff = v->target_log2 - v->pitch_log2;
    float step = fabsf(diff) <= v->gstep ? diff : (diff > 0.0f ? v->gstep : -v->gstep);
    return v->pitch_log2 + f * step;
}

float acid_slot_pitch(const acid_slot_t *v)
{
    return pitch_out(v);
}

// Отклонение строя для ноты, в октавах; у равномерного - 0 (и высота считается как раньше).
static float tune_oct(const acid_synth_t *s, int note)
{
    int t = s->pitch_raw[2];
    return t ? TUNING_CENTS[t][((note - s->tonal.tonic) % 12 + 12) % 12] * (1.0f / 1200.0f) : 0.0f;
}

static float note_log2(const acid_synth_t *s, int note)
{
    float x = log2f(acid_midi_to_hz(note));
    return s->pitch_raw[2] ? x + tune_oct(s, note) : x;
}

static float note_hz(const acid_synth_t *s, int note)
{
    float hz = acid_midi_to_hz(note);
    return s->pitch_raw[2] ? hz * exp2f(tune_oct(s, note)) : hz;
}

float acid_synth_pitch_hz(const acid_synth_t *s)
{
    return exp2f(pitch_out(&s->slot[0]));
}

// Новая нота голоса: со слайдом только глайд, без него перезапуск огибающих.
static void voice_note(acid_synth_t *s, acid_slot_t *v, int note, int accent, int slide)
{
    v->target_log2 = note_log2(s, note);
    // Акцент громкости действует и на слайде, огибающие при этом не перезапускаются.
    v->accent_gain = accent ? v->accent_amt : 1.0f;
    v->accented = accent != 0;
    if (slide) {
        v->gphase = 0.0f;
        v->gliding = 1;
        v->seq_inc = v->seq_step_inc = 0.0f;
        return;
    }
    v->gliding = 0;
    v->gphase = 0.0f;
    v->seq_inc = v->seq_step_inc = 0.0f;
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
    memset(s->bsp_held, 0, sizeof s->bsp_held);
    for (int k = 0; k < 2 * ACID_SLOTS; k++) {
        acid_slot_t *v = k < ACID_SLOTS ? &s->slot[k] : &s->layer[k - ACID_SLOTS];
        v->nheld = 0;
        v->pend_note = -1;
        v->gliding = 0;
        v->seq_inc = v->seq_step_inc = 0.0f;
        v->rtg_gate_left = -1;
        if (v->pet) {
            acid_petri_notes_off(v->pet);
        }
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
    if (s->drone) {
        if (s->tonal.target == 0 || s->tonal.target == v->idx + 1) {
            note = acid_tonal_drone_note(&s->tonal, note);
        }
    } else if (acid_tonal_moves(&s->tonal, v->idx)) {
        note = acid_tonal_note(&s->tonal, note);
    }
    return note + v->transpose;
}

static void note_on(acid_synth_t *s, acid_slot_t *v, int note, int vel)
{
    if (s->drone && v->nheld > 0) {
        if (v == &s->slot[v->idx]) {
            s->trig |= (v->idx ? ACID_TRIG_NOTE2 : ACID_TRIG_NOTE1) |
                       (vel > ACCENT_VEL ? (v->idx ? ACID_TRIG_ACC2 : ACID_TRIG_ACC1) : 0);
        }
        int old = v->held[v->nheld - 1];
        for (int i = 0; v->pet && i < v->nheld - 1; i++) {
            acid_petri_note_off(v->pet, v->held[i]);
        }
        v->pend_note = -1;
        v->held[0] = (int8_t)note;
        v->held_at[0] = v->now;
        v->nheld = 1;
        int sn = slot_note(s, v, note);
        voice_note(s, v, sn, vel > ACCENT_VEL, 1);
        if (v->type == ACID_TYPE_PETRI && v->pet) {
            acid_petri_move(v->pet, old, note, note_hz(s, sn));
        }
        v->ctrl_left = 0;
        return;
    }
    if (v->pend_note == note) {
        // Дребезг: та же нота сразу после короткого нажатия - это продолжение той же ноты.
        v->pend_note = -1;
        return;
    }
    if (v == &s->slot[v->idx]) {
        s->trig |= (v->idx ? ACID_TRIG_NOTE2 : ACID_TRIG_NOTE1) |
                   (vel > ACCENT_VEL ? (v->idx ? ACID_TRIG_ACC2 : ACID_TRIG_ACC1) : 0);
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
    if (v->type == ACID_TYPE_PETRI && v->pet) {
        // Парафония: у каждой ноты свой голос, огибающая фильтра бьёт на каждой (слайда нет).
        acid_petri_note_on(v->pet, note, note_hz(s, slot_note(s, v, note)), vel);
        acid_decay_trigger(&v->fenv, 1.0f);
    }
    v->ctrl_left = 0; // новая нота: пересчитать сразу
}

static void note_off(acid_synth_t *s, acid_slot_t *v, int note)
{
    for (int i = 0; i < v->nheld; i++) {
        if (v->held[i] == note && v->now - v->held_at[i] < (uint32_t)(BOUNCE_S * s->sr)) {
            // Слишком короткое нажатие: отпустим позже, если не придёт повтор. Отложенная - одна: прежнюю
            // другую ноту отпускаем сразу, иначе в коротком аккорде PETRI она висела бы до конца.
            // Время нажатия - до отпускания: оно сдвигает held_at.
            uint32_t at = v->held_at[i] + (uint32_t)(MERGE_S * s->sr);
            if (v->pend_note >= 0 && v->pend_note != note) {
                note_release(s, v, v->pend_note);
            }
            v->pend_note = note;
            v->pend_at = at;
            return;
        }
    }
    note_release(s, v, note);
}

static void note_release(acid_synth_t *s, acid_slot_t *v, int note)
{
    int top = v->nheld > 0 && v->held[v->nheld - 1] == note;
    held_remove(v, note);
    if (v->pet) {
        acid_petri_note_off(v->pet, note); // и после смены типа: голос не залипнет
    }
    if (top && v->nheld > 0) {
        // Отпустили верхнюю при зажатой нижней: глайд обратно, как в 303.
        v->target_log2 = note_log2(s, slot_note(s, v, v->held[v->nheld - 1]));
        v->gliding = 1;
    }
}

static void drum_on(acid_synth_t *s, int note, int vel)
{
    // Пэды BSP при мягкой игре дают velocity 3-30: без пола такие удары не слышны.
    float v = DRUM_VEL_FLOOR + (1.0f - DRUM_VEL_FLOOR) * (float)vel / 127.0f;
    if (note == 36) {
        s->trig |= ACID_TRIG_KICK;
        acid_kick_trigger_vel(&s->kick, v);
        s->kick_vel = v;
        acid_pump_hit(&s->pump, v); // от ноты: при бочке в нуле тоже качает
    } else if (note == 38 || note == 40) {
        s->trig |= ACID_TRIG_SNARE;
        acid_snare_trigger(&s->snare);
        s->snare_vel = v;
    } else if (note == 39) {
        s->trig |= ACID_TRIG_CLAP;
        acid_clap_trigger(&s->clap);
        s->clap_vel = v;
    } else if (note == 42 || note == 44 || note == 46) {
        s->trig |= ACID_TRIG_HAT;
        acid_hat_trigger(&s->hat, note == 46);
        s->hat_vel = v;
    } else if (acid_perc_inst_of(note, NULL) >= 0) {
        s->trig |= ACID_TRIG_PERC;
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
    case 8: slot_set_glide(sl, s->sr, expo(v, 0.01f, 0.3f)); break;
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
    case 7: slot_set_glide(sl, s->sr, expo(v, 0.01f, 0.3f)); break;
    case 8: transpose_set(sl, v); break;
    case 9: p->timbre_env = bip(v); break;
    case 10: p->morph_env = bip(v); break;
    case 11: p->fm_env = bip(v); break;
    case 12: sl->p_accent = lin(v, 0.0f, 0.75f); break;
    default: break;
    }
}

// Ручки PETRI на синте: фильтр и огибающие. Режим, контраст, орбита и центр считает панель (снимает таблицу).
static void knob_petri(acid_synth_t *s, acid_slot_t *sl, int slot, int v)
{
    const uint8_t *k = sl->knob[ACID_TYPE_PETRI];
    switch (slot) {
    case 2:
        sl->cut_base = expo(v, 60.0f, 8000.0f);
        sl->pet_cut_oct = log2f(sl->cut_base);
        break;
    case 3: sl->res = lin(v, 0.0f, 0.97f); break;
    case 4: sl->pet_env = lin(v, 0.0f, 1.0f); break;
    case 5: sl->decay_s = expo(v, 0.03f, 2.0f); break;
    case 6:
    case 7: {
        int a = slot == 6 ? v : (k[6] != KNOB_NONE ? k[6] : RAW_PETRI[6]);
        int r = slot == 7 ? v : (k[7] != KNOB_NONE ? k[7] : RAW_PETRI[7]);
        acid_petri_set_env(sl->pet, expo(a, 0.001f, 2.0f), expo(r, 0.02f, 3.0f));
        break;
    }
    default: break;
    }
    if (slot == 5) {
        acid_decay_set(&sl->fenv, sl->decay_s, s->sr);
    }
    sl->ctrl_left = 0;
}

static void knob_apply(acid_synth_t *s, acid_slot_t *v, int slot, int val)
{
    if (v->type == ACID_LAYER_OFF || drive_knob(v, slot, val)) {
        return;
    }
    if (v->type == 0) {
        knob_303(s, v, slot, val);
    } else if (v->type == ACID_TYPE_PETRI) {
        if (v->pet) {
            knob_petri(s, v, slot, val);
        }
    } else {
        knob_plaits(s, v, slot, val);
    }
}

// Умолчания звучащего типа и поверх них запомненные ручки этого типа.
static void type_defaults(acid_synth_t *s, acid_slot_t *v)
{
    slot_set_glide(v, s->sr, GLIDE_S);
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
    } else if (v->type == ACID_TYPE_PETRI) {
        for (int i = TYPE_ENC + 1; i < DRIVE_ENC; i++) {
            knob_apply(s, v, i, RAW_PETRI[i]); // умолчания - те же формулы, что у ручек
        }
        memset(v->pet_mod, 0, sizeof v->pet_mod);
    } else {
        acid_plaits_params_t *p = &v->pp;
        p->model = v->type - 1;
        p->sustained = s->drone;
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
    if (v->pet) {
        acid_petri_kill(v->pet); // звук уже ушёл: прежние голоса чашки не всплывут
    }
    type_defaults(s, v);
    if (v->nheld > 0) {
        int top = v->held[v->nheld - 1];
        voice_note(s, v, slot_note(s, v, top), v->accented, 0);
        for (int i = 0; v->type == ACID_TYPE_PETRI && v->pet && i < v->nheld; i++) {
            int n = v->held[i]; // весь аккорд, верхняя - последней (самый новый голос)
            acid_petri_note_on(v->pet, n, note_hz(s, slot_note(s, v, n)), 100);
        }
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
    case 15:
        s->rv_wet = lin(v, 0.0f, 1.0f);
        if (v == 0) {
            acid_reverb_init(&s->reverb, s->rv_mem);
            acid_reverb_set(&s->reverb, s->rv_raw[3], s->rv_raw[4]);
            memset(s->rv_prev, 0, sizeof s->rv_prev);
        }
        break;
    default: break;
    }
    if (slot >= 6 && slot <= 8) {
        acid_delay_set(&s->delay, s->dl_time, s->dl_fb, s->dl_tone);
    }
}

// Списки синта: значение вне списка звук не меняет и не запоминается (зеркало панели показывает действующее).
static int synth_knob_ok(int type, int slot, int v)
{
    if (type == ACID_TYPE_PETRI) {
        return !(slot == PETRI_MODE_ENC && v > 3) && !(slot == PETRI_SHAPE_ENC && v > 4);
    }
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
        if (layer && v < ACID_TYPES && v != ACID_TYPE_PETRI && sl->type == ACID_LAYER_OFF && sl->fade == 0) {
            layer_sync(sl, &s->slot[sl->idx]);
        }
        if (v < ACID_TYPES + layer && !(layer && v == ACID_TYPE_PETRI)) {
            if (v != sl->sel_type) {
                int p = (layer ? ACID_PAGE_LAYER1 : PAGE_SYNTH1) + sl->idx;
                for (int e = TYPE_ENC + 1; e < ACID_KNOBS; e++) {
                    if (s->mod_mask[p] & (1u << e)) {
                        acid_synth_mod_release(s, p, e);
                    }
                }
            }
            type_request(sl, v); // значение на экране = номер типа, остальное игнор; слой не бывает PETRI
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
    if (page == ACID_PAGE_PETRI) {
        return !PETRI_PAGE_MAX[slot] || v <= PETRI_PAGE_MAX[slot];
    }
    if (page == PAGE_FX) {
        return !((slot == 6 || slot == 9) && v > 4) && !(slot == 10 && v > 2);
    }
    return 1;
}

// Страница «Чашка»: синт берёт маршруты (11-13) и фриз (14), остальное - параметры чашки на панели (зеркало raw).
static void page_petri(acid_synth_t *s, int slot, int v)
{
    if (!raw_ok(ACID_PAGE_PETRI, slot, v)) {
        return;
    }
    int was = s->raw[ACID_PAGE_PETRI][slot];
    s->raw[ACID_PAGE_PETRI][slot] = (uint8_t)v;
    if (slot >= 11 && slot <= 13) {
        s->petri_depth[slot - 11] = bip(v);
    } else if (slot == PETRI_FREEZE_ENC && (was >= 64) != (v >= 64)) {
        for (int k = 0; k < ACID_SLOTS; k++) {
            acid_petri_freeze(&s->petri[k], v >= 64); // вверх - снимок звучащей таблицы
        }
    }
}

static void drone_retune(acid_synth_t *s);

static void page_reverb(acid_synth_t *s, int slot, int v)
{
    s->rv_raw[slot - 1] = (uint8_t)v;
    switch (slot) {
    case 1: s->rv_send1 = lin(v, 0.0f, 1.0f); break;
    case 2: s->rv_send2 = lin(v, 0.0f, 1.0f); break;
    case 3: s->rv_send_drums = lin(v, 0.0f, 1.0f); break;
    case 4:
    case 5: acid_reverb_set(&s->reverb, s->rv_raw[3], s->rv_raw[4]); break;
    case 6: s->rv_narrow = (float)(127 - v) * (1.0f / 254.0f); break;
    default: break;
    }
}

// Подэкран PITCH: шаг слайда синта 1 и 2 (у синта и его слоя один), строй, GATE синта 1 и 2. Вне списка - игнор.
static void page_pitch(acid_synth_t *s, int slot, int v)
{
    if (slot <= 2 && v < ACID_GLIDES) {
        s->pitch_raw[slot - 1] = (uint8_t)v;
        int art = v >= ACID_GLIDE_SEMI_ART;
        int ramp = !art && v >= ACID_GLIDE_SEMI_RAMP;
        int st = art  ? v - ACID_GLIDE_SEMI_ART + ACID_GLIDE_SEMI
               : ramp ? v - ACID_GLIDE_SEMI_RAMP + ACID_GLIDE_SEMI
                      : v;
        float inv = st == ACID_GLIDE_SEMI ? 12.0f : st == ACID_GLIDE_QUARTER ? 24.0f : 0.0f;
        acid_slot_t *vs[2] = {&s->slot[slot - 1], &s->layer[slot - 1]};
        for (int i = 0; i < 2; i++) {
            vs[i]->ginv = inv;
            vs[i]->gstep = inv > 0.0f ? 1.0f / inv : 0.0f;
            vs[i]->gstep_inc = vs[i]->gstep > 0.0f ? 1.0f / (vs[i]->glide_s * s->sr) : 0.0f;
            vs[i]->gramp = ramp;
            vs[i]->gart = art;
        }
    } else if (slot == 3 && v < ACID_TUNINGS) {
        s->pitch_raw[2] = (uint8_t)v;
        if (s->drone) {
            drone_retune(s);
        }
    } else if (slot == 4 || slot == 5) {
        s->pitch_raw[slot - 1] = (uint8_t)v;
        float g = v >= 127 ? 1.0f : (float)v * (1.0f / 127.0f);
        s->slot[slot - 4].ggate = g;
        s->layer[slot - 4].ggate = g;
    }
}

// Энкодер slot (с 1) на странице page, как будто BSP стоит на ней.
static void apply_page(acid_synth_t *s, int page, int slot, int v)
{
    if (page == ACID_PAGE_PITCH && slot > 0 && slot <= ACID_PITCH_KNOBS) {
        page_pitch(s, slot, v);
        return;
    }
    if (page == ACID_PAGE_REVERB && slot > 0 && slot <= ACID_REVERB_KNOBS) {
        page_reverb(s, slot, v);
        return;
    }
    if (page == ACID_PAGE_PETRI) {
        page_petri(s, slot, v); // своя проверка списков: фриз смотрит на прежнее значение
        return;
    }
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
        if (s->drone) {
            drone_retune(s);
        }
        s->save = 1; // квадрат - часть композиции, переживает перезагрузку
    } else if (page == PAGE_SYNTH1 || page == PAGE_SYNTH1 + 1) {
        page_synth(s, &s->slot[page - PAGE_SYNTH1], slot, v, 0);
        if (s->drone) {
            drone_retune(s);
        }
    } else if (page >= ACID_PAGE_LAYER1 && page < ACID_PAGE_LAYER1 + ACID_SLOTS) {
        page_synth(s, &s->layer[page - ACID_PAGE_LAYER1], slot, v, 1);
        if (s->drone) {
            drone_retune(s);
        }
    } else if (page == ACID_PAGE_GRIDS) {
        if (acid_drummer_set(&s->drummer, slot, v)) {
            s->raw[page][slot] = (uint8_t)v;
        }
        if (!s->drummer.knob[DRM_ON]) {
            s->grids_step = -1;
        }
    }
}

// Шаг автобарабанов: ноты барабанщика в ударку, как пэды BSP.
static void grids_play(acid_synth_t *s, int step)
{
    acid_hit_t hit[ACID_DRUMMER_MAX];
    int n = acid_drummer_step(&s->drummer, step, s->beat.bar, hit);
    for (int i = 0; i < n; i++) {
        drum_on(s, hit[i].note, hit[i].vel);
    }
    s->grids_hits += (uint32_t)n;
    s->grids_step = s->drummer.knob[DRM_ON] ? (int)(s->beat.bar & 1) * ACID_BEAT_STEPS + step : -1;
}

// Индекс слота синта или слоя (0..3) по странице, -1 - не синт и не слой.
static int synth_page_idx(int page)
{
    if (page == PAGE_SYNTH1 || page == PAGE_SYNTH1 + 1) {
        return page - PAGE_SYNTH1;
    }
    if (page >= ACID_PAGE_LAYER1 && page < ACID_PAGE_LAYER1 + ACID_SLOTS) {
        return ACID_SLOTS + page - ACID_PAGE_LAYER1;
    }
    return -1;
}

// Ручка от пользователя (BSP, панель, сцена, демо): поменялось действующее значение - память пора записать.
static void user_knob(acid_synth_t *s, int page, int slot, int v)
{
    if (page < ACID_PAGES && s->mod_base[page][slot] != KNOB_NONE) {
        int idx = synth_page_idx(page);
        if (idx >= 0) {
            s->mod_unset[idx] &= (uint16_t)~(1u << slot);
        }
        if (s->mod_base[page][slot] != v) {
            s->mod_base[page][slot] = (uint8_t)v;
            s->save = 1;
        }
        return;
    }
    int was = acid_synth_knob(s, page, slot);
    apply_page(s, page, slot, v);
    if (acid_synth_knob(s, page, slot) != was) {
        s->save = 1;
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
        user_knob(s, s->page, slot, v);
    }
}

void acid_synth_petri(acid_synth_t *s, const acid_petri_frame_t *f)
{
    for (int k = 0; k < ACID_SLOTS; k++) {
        acid_petri_post(&s->petri[k], f);
    }
}

void acid_synth_set_knob(acid_synth_t *s, int page, int enc, int v)
{
    int page_ok = (page >= 0 && page < ACID_PAGES) || page == ACID_PAGE_REVERB || page == ACID_PAGE_PITCH;
    int enc_ok = page == ACID_PAGE_REVERB  ? enc > 0 && enc <= ACID_REVERB_KNOBS
                 : page == ACID_PAGE_PITCH ? enc > 0 && enc <= ACID_PITCH_KNOBS
                                           : enc > 0 && enc < ACID_KNOBS;
    if (page_ok && enc_ok && v >= 0 && v <= 127) {
        user_knob(s, page, enc, v);
    }
}

static int raw_knob(const acid_synth_t *s, int page, int enc)
{
    if (page == ACID_PAGE_REVERB) {
        return enc > 0 && enc <= ACID_REVERB_KNOBS ? s->rv_raw[enc - 1] : 0;
    }
    if (page == ACID_PAGE_PITCH) {
        return enc > 0 && enc <= ACID_PITCH_KNOBS ? s->pitch_raw[enc - 1] : 0;
    }
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
        const uint8_t *def = sl->sel_type == 0 ? RAW_303 : sl->sel_type == ACID_TYPE_PETRI ? RAW_PETRI : RAW_PLAITS;
        return k != KNOB_NONE ? k : def[enc];
    }
    return s->raw[page][enc];
}

int acid_synth_knob(const acid_synth_t *s, int page, int enc)
{
    if (page == ACID_PAGE_REVERB || page == ACID_PAGE_PITCH) {
        return raw_knob(s, page, enc);
    }
    if (page < 0 || page >= ACID_PAGES || enc < 0 || enc >= ACID_KNOBS) {
        return 0;
    }
    if (s->mod_base[page][enc] != KNOB_NONE) {
        return s->mod_base[page][enc];
    }
    return raw_knob(s, page, enc);
}

unsigned acid_synth_take_trig(acid_synth_t *s)
{
    unsigned r = s->trig;
    s->trig = 0;
    return r;
}

uint32_t acid_synth_clock_pos(const acid_synth_t *s)
{
    const acid_beat_t *b = &s->beat;
    return (b->bar * ACID_BEAT_STEPS + (uint32_t)b->step) * ACID_BEAT_TICKS + (uint32_t)(b->tick > 0 ? b->tick - 1 : 0);
}

int acid_synth_knob_smooth(const acid_synth_t *s, int page, int enc)
{
    if (!acid_mod_page_ok(page, enc)) {
        return 0;
    }
    switch (page) {
    case 0: return enc <= 14;
    case 1: return 1;
    case PAGE_DRUMS2: return enc <= 10;
    case PAGE_TOMS: return enc >= 2;
    case PAGE_TOMS + 1:
    case PAGE_COW: return enc >= 2 && enc <= 13;
    case PAGE_FX: return enc != 6 && enc != 9 && enc != 10 && enc != 14;
    case ACID_PAGE_GRIDS: return (enc >= 2 && enc <= 7) || enc == DRM_ROLL;
    default: {
        int idx = synth_page_idx(page);
        if (idx < 0) {
            return 0;
        }
        const acid_slot_t *sl = idx < ACID_SLOTS ? &s->slot[idx] : &s->layer[idx - ACID_SLOTS];
        int t = sl->sel_type;
        if (t == ACID_LAYER_OFF || enc == TYPE_ENC) {
            return 0;
        }
        if (t == 0) {
            return (enc >= 2 && enc <= 6) || enc == 8 || enc >= DRIVE_ENC;
        }
        if (t == ACID_TYPE_PETRI) {
            return enc != PETRI_MODE_ENC && enc != PETRI_SHAPE_ENC;
        }
        return enc != 8;
    }
    }
}

int acid_synth_mod_apply(acid_synth_t *s, int page, int enc, int eff)
{
    if (!acid_synth_knob_smooth(s, page, enc)) {
        return 0;
    }
    eff = eff < 0 ? 0 : eff > 127 ? 127 : eff;
    if (s->mod_base[page][enc] == KNOB_NONE) {
        s->mod_base[page][enc] = (uint8_t)raw_knob(s, page, enc);
        s->mod_mask[page] |= (uint16_t)(1u << enc);
        int idx = synth_page_idx(page);
        if (idx >= 0) {
            const acid_slot_t *sl = idx < ACID_SLOTS ? &s->slot[idx] : &s->layer[idx - ACID_SLOTS];
            if (sl->sel_type < ACID_TYPES && sl->knob[sl->sel_type][enc] == KNOB_NONE) {
                s->mod_unset[idx] |= (uint16_t)(1u << enc);
            }
        }
    }
    if (raw_knob(s, page, enc) == eff) {
        return 0;
    }
    apply_page(s, page, enc, eff);
    return 1;
}

void acid_synth_mod_release(acid_synth_t *s, int page, int enc)
{
    if (page < 0 || page >= ACID_PAGES || enc <= 0 || enc >= ACID_KNOBS || s->mod_base[page][enc] == KNOB_NONE) {
        return;
    }
    int base = s->mod_base[page][enc];
    s->mod_base[page][enc] = KNOB_NONE;
    s->mod_mask[page] &= (uint16_t)~(1u << enc);
    apply_page(s, page, enc, base);
    int idx = synth_page_idx(page);
    if (idx >= 0 && (s->mod_unset[idx] & (1u << enc))) {
        s->mod_unset[idx] &= (uint16_t)~(1u << enc);
        acid_slot_t *sl = idx < ACID_SLOTS ? &s->slot[idx] : &s->layer[idx - ACID_SLOTS];
        if (sl->sel_type < ACID_TYPES) {
            sl->knob[sl->sel_type][enc] = KNOB_NONE;
        }
    }
}

void acid_synth_mod_clear(acid_synth_t *s)
{
    for (int p = 0; p < ACID_PAGES; p++) {
        uint16_t m = s->mod_mask[p];
        for (int e = 1; m && e < ACID_KNOBS; e++) {
            if (m & (1u << e)) {
                acid_synth_mod_release(s, p, e);
            }
        }
    }
}

// Перестройка дрона по 9 TONAL и TRANS: на границе такта и сразу при повороте ручки.
static void drone_retune(acid_synth_t *s)
{
    for (int i = 0; i < 2 * ACID_SLOTS; i++) {
        acid_slot_t *v = i < ACID_SLOTS ? &s->slot[i] : &s->layer[i - ACID_SLOTS];
        if (v->nheld == 0) {
            continue;
        }
        int top = v->held[v->nheld - 1];
        float target = note_log2(s, slot_note(s, v, top));
        for (int k = 0; v->type == ACID_TYPE_PETRI && v->pet && k < v->nheld; k++) {
            int n = v->held[k];
            acid_petri_retune(v->pet, n, note_hz(s, slot_note(s, v, n)));
        }
        if (target == v->target_log2) {
            if (s->tonal.held_mode != 1 && v->gliding) {
                v->pitch_log2 = target;
                v->gliding = 0;
                v->ctrl_left = 0;
            }
            continue;
        }
        v->target_log2 = target;
        if (s->tonal.held_mode == 1) {
            v->gliding = 1;
        } else {
            v->pitch_log2 = target;
            v->gliding = 0;
            v->ctrl_left = 0;
        }
    }
}

void acid_synth_set_drone(acid_synth_t *s, int on)
{
    s->drone = on != 0;
    for (int i = 0; i < 2 * ACID_SLOTS; i++) {
        acid_slot_t *v = i < ACID_SLOTS ? &s->slot[i] : &s->layer[i - ACID_SLOTS];
        v->pp.sustained = s->drone;
        if (s->drone) {
            v->pend_note = -1;
        }
    }
    if (!s->drone) {
        acid_synth_notes_off(s);
        return;
    }
    drone_retune(s);
    for (int k = 0; k < ACID_SLOTS; k++) {
        if (s->slot[k].nheld == 0) {
            note_on(s, &s->slot[k], ACID_DRONE_NOTE, 100);
        }
        if (s->layer[k].nheld == 0) {
            int n = s->slot[k].held[s->slot[k].nheld - 1];
            note_on(s, &s->layer[k], n, 100);
        }
    }
}

// Граница шага слоя: звучащие ноты синтов по режиму энкодера 6, свежая нота - сразу на новый шаг.
static void tonal_boundary(acid_synth_t *s)
{
    if (s->drone) {
        drone_retune(s);
        return;
    }
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
        v->target_log2 = note_log2(s, slot_note(s, v, v->held[v->nheld - 1]));
        for (int k = 0; v->type == ACID_TYPE_PETRI && v->pet && k < v->nheld; k++) {
            int n = v->held[k]; // у PETRI слайда нет: аккорд прыгает весь и в режиме глайда
            acid_petri_retune(v->pet, n, note_hz(s, slot_note(s, v, n)));
        }
        if (mode == 1) {
            v->gliding = 1;
        } else {
            v->pitch_log2 = v->target_log2; // прыжок без перезапуска огибающих
            v->gliding = 0;
            v->ctrl_left = 0;
        }
    }
}

// Нота синта idx (0 или 1): слот и слой с одним стеком нот. Путь общий у BSP и секвенсера.
static void synth_note(acid_synth_t *s, int idx, int on, int note, int vel)
{
    if (on) {
        note_on(s, &s->slot[idx], note, vel);
        note_on(s, &s->layer[idx], note, vel); // тот же стек нот, что у синта
    } else if (!s->drone) {
        note_off(s, &s->slot[idx], note);
        note_off(s, &s->layer[idx], note);
    }
}

// Отпускание без окна дребезга: у секвенсера коротких случайных нот нет, а удары ретриггера короче окна.
static void exact_off(acid_synth_t *s, acid_slot_t *v, int note)
{
    if (v->pend_note == note) {
        v->pend_note = -1;
    }
    note_release(s, v, note);
}

static void seq_rtg_gate(acid_synth_t *s, acid_slot_t *v, int rtg_ticks)
{
    if (rtg_ticks > 0 && v->ggate < 1.0f) {
        float scale = v->half ? 0.5f : 1.0f;
        v->rtg_gate_left = (int)((float)rtg_ticks * (acid_clock_spb(&s->clock) * (1.0f / 24.0f)) * scale * v->ggate + 0.5f);
    } else {
        v->rtg_gate_left = -1;
    }
}

static void seq_slide_to(acid_synth_t *s, acid_slot_t *v, int slide_note, int slide_steps)
{
    if (slide_steps <= 0) {
        return;
    }
    float target = note_log2(s, slot_note(s, v, slide_note));
    float diff = target - v->pitch_log2;
    if (fabsf(diff) < 1e-6f) {
        return;
    }
    float dur = (float)slide_steps * 0.25f * acid_clock_spb(&s->clock);
    if (dur < 1.0f) {
        dur = 1.0f;
    }
    v->target_log2 = target;
    v->gphase = 0.0f;
    v->gliding = 1;
    if (v->gstep > 0.0f) {
        float steps = ceilf(fabsf(diff) / v->gstep);
        v->seq_step_inc = steps / dur;
        v->seq_inc = 0.0f;
    } else {
        v->seq_inc = diff / dur;
        v->seq_step_inc = 0.0f;
    }
}

static void seq_notes(acid_synth_t *s, const acid_seq_ev_t *ev, int n)
{
    for (int i = 0; i < n; i++) {
        int note = ev[i].note;
        if (!ev[i].on && (s->bsp_held[ev[i].synth][note >> 5] >> (note & 31) & 1)) {
            continue; // ту же ноту держит BSP: отпустит он
        }
        if (ev[i].on == 2) {
            seq_slide_to(s, &s->slot[ev[i].synth], ev[i].slide_note, ev[i].slide_steps);
            seq_slide_to(s, &s->layer[ev[i].synth], ev[i].slide_note, ev[i].slide_steps);
            continue;
        }
        if (ev[i].on || s->drone) {
            synth_note(s, ev[i].synth, ev[i].on, note, ev[i].vel);
            if (ev[i].on) {
                seq_rtg_gate(s, &s->slot[ev[i].synth], ev[i].rtg_ticks);
                seq_rtg_gate(s, &s->layer[ev[i].synth], ev[i].rtg_ticks);
                if (ev[i].slide_steps > 0) {
                    seq_slide_to(s, &s->slot[ev[i].synth], ev[i].slide_note, ev[i].slide_steps);
                    seq_slide_to(s, &s->layer[ev[i].synth], ev[i].slide_note, ev[i].slide_steps);
                }
            }
        } else {
            exact_off(s, &s->slot[ev[i].synth], note);
            exact_off(s, &s->layer[ev[i].synth], note);
            s->slot[ev[i].synth].rtg_gate_left = -1;
            s->layer[ev[i].synth].rtg_gate_left = -1;
        }
    }
}

void acid_synth_use_grids(acid_synth_t *s, const pl_grids_pat_t *user)
{
    acid_drummer_use_user(&s->drummer, user);
}

void acid_synth_use_seq(acid_synth_t *s, acid_seq_t *q)
{
    s->seq = q;
    if (q) {
        acid_seq_rewind(q);
    }
}

void acid_synth_seq_run(acid_synth_t *s, int synth, int on)
{
    if (!s->seq) {
        return;
    }
    acid_seq_ev_t ev[2];
    seq_notes(s, ev, acid_seq_run(s->seq, synth, on, !s->beat.stopped, ev));
}

void acid_synth_midi(acid_synth_t *s, const acid_midi_event_t *ev)
{
    switch (ev->type) {
    case ACID_MIDI_NOTE_ON:
        if (ev->ch == BASS_CH || ev->ch == SEQ2_CH) {
            s->bsp_held[ev->ch == SEQ2_CH][(ev->d1 >> 5) & 3] |= 1u << (ev->d1 & 31);
            synth_note(s, ev->ch == SEQ2_CH, 1, ev->d1, ev->d2);
        } else if (ev->ch == DRUM_CH) {
            drum_on(s, ev->d1, ev->d2);
        }
        break;
    case ACID_MIDI_NOTE_OFF:
        if (ev->ch == BASS_CH || ev->ch == SEQ2_CH) {
            int idx = ev->ch == SEQ2_CH;
            s->bsp_held[idx][(ev->d1 >> 5) & 3] &= ~(1u << (ev->d1 & 31));
            if (!s->seq || s->seq->t[idx].sounding != ev->d1) { // ту же ноту держит секвенсер: отпустит он
                synth_note(s, idx, 0, ev->d1, 0);
            }
        }
        break;
    case ACID_MIDI_CC:
        control_change(s, ev->d1, ev->d2);
        break;
    case ACID_MIDI_KNOB:
        acid_synth_set_knob(s, ev->ch, ev->d1, ev->d2);
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
        int beat_step = -1;
        if (acid_beat_tick(&s->beat)) {
            beat_step = s->beat.step;
            s->grids_swing = s->drummer.knob[DRM_SWING];
            s->trig |= ACID_TRIG_STEP;
            if ((beat_step & 3) == 0) {
                s->trig |= ACID_TRIG_BEAT;
            }
            if (beat_step == 0) {
                s->trig |= ACID_TRIG_BAR | ((s->beat.bar & 3) == 0 ? ACID_TRIG_BAR4 : 0);
            }
        }
        // Шаг Grids - свой: свинг задерживает нечётные шестнадцатые, триольный рисунок идёт 12 шагами в такте.
        int grid_step = acid_beat_grid_due(&s->beat, acid_drummer_grid(&s->drummer), s->grids_swing);
        if (grid_step >= 0) {
            grids_play(s, grid_step);
        } else if (!s->beat.stopped) {
            acid_hit_t hit[ACID_DRUMMER_ROLLS]; // между шагами - повторы дроби
            int n = acid_drummer_tick(&s->drummer, hit);
            for (int i = 0; i < n; i++) {
                drum_on(s, hit[i].note, hit[i].vel);
            }
            s->grids_hits += (uint32_t)n;
        }
        if (s->seq) {
            acid_seq_ev_t sev[4];
            seq_notes(s, sev, acid_seq_clock(s->seq, beat_step, &s->tonal, sev));
        }
        break;
    }
    case ACID_MIDI_START:
        acid_tonal_start(&s->tonal);
        if (s->drone) {
            drone_retune(s);
        }
        acid_beat_start(&s->beat);
        acid_drummer_reset(&s->drummer);
        if (s->seq) {
            acid_seq_rewind(s->seq);
        }
        break;
    case ACID_MIDI_CONTINUE:
        acid_beat_continue(&s->beat);
        break;
    case ACID_MIDI_STOP:
        if (!s->drone) {
            acid_synth_notes_off(s); // защита от залипших нот
        }
        acid_beat_stop(&s->beat);
        acid_drummer_reset(&s->drummer);
        s->grids_step = -1;
        for (int k = 0; s->seq && k < ACID_SEQ_SYNTHS; k++) {
            acid_synth_seq_run(s, k, 0);
        }
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
    // exp2f из libm - сотни тактов на плате: пока высота стоит (нет слайда), берём прошлую частоту.
    float pitch = pitch_out(v);
    if (pitch != v->ctl_log2) {
        v->ctl_log2 = pitch;
        v->ctl_hz = exp2f(pitch);
    }
    float hz = v->ctl_hz;
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

// Следующий блок Plaits в pbuf: 12 сэмплов, молчащий голос не считаем.
static void plaits_refill(acid_slot_t *v, int gate)
{
    v->ppos = 0;
    if (acid_drive_on(&v->drv)) {
        acid_drive_pitch(&v->drv, acid_exp2(pitch_out(v)));
    }
    if (!gate && v->idle >= IDLE_BLOCKS) {
        memset(v->pbuf, 0, sizeof v->pbuf);
        return;
    }
    acid_plaits_params_t *p = &v->pp;
    p->note = 12.0f * (pitch_out(v) - log2f(440.0f)) + 69.0f + (v->half ? 12.0f : 0.0f); // Plaits на 48 кГц
    p->trigger = gate && !v->retrig;
    v->retrig = 0;
    p->level = gate ? (v->accented ? 1.0f : 1.0f - v->p_accent) : 0.0f;
    acid_plaits_render(v->plaits, p, v->pbuf, ACID_PLAITS_BLOCK);
    int quiet = !gate;
    for (int i = 0; i < ACID_PLAITS_BLOCK && quiet; i++) {
        quiet = fabsf(v->pbuf[i]) <= PLAITS_QUIET;
    }
    v->idle = quiet ? v->idle + 1 : 0;
}

static inline int slot_glide_advance(acid_slot_t *v, int *gate)
{
    if (!v->gliding) {
        return 0;
    }
    if (v->gstep_inc > 0.0f) {
        float base_inc = v->seq_step_inc > 0.0f ? v->seq_step_inc : v->gstep_inc;
        float inc = v->half ? 2.0f * base_inc : base_inc;
        v->gphase += inc;
        int stepped = 0;
        if (v->gphase >= 1.0f - 0.5f * inc) {
            v->gphase -= 1.0f;
            if (v->gphase < 0.0f) {
                v->gphase = 0.0f;
            }
            float diff = v->target_log2 - v->pitch_log2;
            if (fabsf(diff) <= v->gstep * 1.0001f) {
                v->pitch_log2 = v->target_log2;
                v->gphase = 0.0f;
                v->gliding = 0;
                v->seq_step_inc = 0.0f;
            } else {
                v->pitch_log2 += diff > 0.0f ? v->gstep : -v->gstep;
            }
            stepped = v->gart;
        }
        if (v->gliding && v->ggate < 1.0f && v->gphase >= v->ggate) {
            *gate = 0;
        }
        return stepped;
    }
    if (v->seq_inc != 0.0f) {
        float inc = v->half ? 2.0f * v->seq_inc : v->seq_inc;
        float diff = v->target_log2 - v->pitch_log2;
        if (fabsf(diff) <= fabsf(inc)) {
            v->pitch_log2 = v->target_log2;
            v->gliding = 0;
            v->seq_inc = 0.0f;
        } else {
            v->pitch_log2 += inc;
        }
        return 0;
    }
    float gc = v->half ? v->glide_coef * v->glide_coef : v->glide_coef; // полюс за два сэмпла
    v->pitch_log2 = v->target_log2 + (v->pitch_log2 - v->target_log2) * gc;
    return 0;
}

// Сэмпл голоса Plaits с громкостью слота: блоки по 12.
static float plaits_next(acid_slot_t *v, int gate)
{
    if (slot_glide_advance(v, &gate)) {
        v->retrig = 1;
    }
    if (v->rtg_gate_left > 0) {
        v->rtg_gate_left--;
    } else if (v->rtg_gate_left == 0) {
        gate = 0;
    }
    if (v->ppos == ACID_PLAITS_BLOCK) {
        plaits_refill(v, gate);
    }
    float y = v->pbuf[v->ppos++];
    if (acid_drive_on(&v->drv)) {
        return acid_drive_run(&v->drv, y * PLAITS_GAIN) * v->gain; // громкость слота после: не меняет характер
    }
    return y * (PLAITS_GAIN * v->gain);
}

// Кусок голоса Plaits: те же сэмплы, что n вызовов plaits_next, но блок pbuf уходит в выход одним проходом.
static void plaits_run(acid_slot_t *v, int gate, float *out, int n)
{
    while (n > 0) {
        if (v->gliding || v->rtg_gate_left >= 0) {
            *out++ = plaits_next(v, gate); // слайд или гейт ретриггера считают по сэмплу
            n--;
            continue;
        }
        if (v->ppos == ACID_PLAITS_BLOCK) {
            plaits_refill(v, gate);
        }
        int m = ACID_PLAITS_BLOCK - v->ppos;
        m = m > n ? n : m;
        const float *y = &v->pbuf[v->ppos];
        if (acid_drive_on(&v->drv)) {
            for (int i = 0; i < m; i++) {
                out[i] = acid_drive_run(&v->drv, y[i] * PLAITS_GAIN) * v->gain;
            }
        } else {
            float g = PLAITS_GAIN * v->gain;
            for (int i = 0; i < m; i++) {
                out[i] = y[i] * g;
            }
        }
        v->ppos += m;
        out += m;
        n -= m;
    }
}

// Сэмпл голоса 303 с громкостью слота.
static inline float voice_next(acid_synth_t *s, acid_slot_t *v, int gate)
{
    if (slot_glide_advance(v, &gate)) {
        acid_decay_trigger(&v->fenv, 1.0f);
    }
    if (v->rtg_gate_left > 0) {
        v->rtg_gate_left--;
    } else if (v->rtg_gate_left == 0) {
        gate = 0;
    }
    acid_amp_gate(&v->amp, gate);
    if (!gate && v->amp.value < AMP_SLEEP) {
        v->amp.value = 0.0f; // спит: осциллятор и фильтр стоят, с новой нотой продолжат с того же места
        return 0.0f;
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

// Коэффициенты фильтра PETRI раз в CTRL_BLOCK: срез и огибающая в октавах, маршруты из модуляций таблицы.
static void petri_ctrl(acid_synth_t *s, acid_slot_t *v)
{
    acid_petri_t *p = v->pet;
    for (int i = 0; i < ACID_PETRI_MODS; i++) {
        v->pet_mod[i] += (acid_petri_mod(p, i) - v->pet_mod[i]) * PETRI_MOD_K;
    }
    if (acid_petri_jumped(p)) {
        acid_decay_trigger(&v->fenv, 1.0f); // прыжок сеятеля бьёт огибающую
    }
    const float *d = s->petri_depth, *m = v->pet_mod;
    float env = v->pet_env + d[2] * (m[2] + m[4]); // вихрь рядом и прыжок
    env = env < 0.0f ? 0.0f : env > 1.0f ? 1.0f : env;
    float res = v->res + d[1] * m[1];
    res = res < 0.0f ? 0.0f : res > 0.97f ? 0.97f : res;
    float oct = v->pet_cut_oct + env * v->fenv.value * 6.0f + d[0] * m[0] * 4.0f;
    float hz = acid_exp2(oct);
    hz = hz > 0.42f * s->sr ? 0.42f * s->sr : hz;
    v->g = acid_ladder_coef(hz, s->sr);
    acid_ladder_prep(&v->lc, v->g, res);
}

// Сэмпл голоса PETRI с громкостью слота: сумма голосов чашки -> ladder -> перегруз.
static inline float petri_next(acid_synth_t *s, acid_slot_t *v)
{
    if (!acid_petri_active(v->pet)) {
        return 0.0f; // молчит: ни таблиц, ни фильтра
    }
    if (v->ctrl_left == 0) {
        petri_ctrl(s, v);
        v->ctrl_left = CTRL_BLOCK;
    }
    v->ctrl_left--;
    acid_decay_next(&v->fenv);
    float y = acid_ladder_tick(&v->flt, acid_petri_osc(v->pet), &v->lc);
    if (acid_drive_on(&v->drv)) {
        return acid_drive_run(&v->drv, y * PETRI_GAIN) * v->gain;
    }
    return y * (PETRI_GAIN * v->gain);
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
    float y = v->type == 0                 ? voice_next(s, v, gate)
              : v->type == ACID_LAYER_OFF  ? 0.0f
              : v->type == ACID_TYPE_PETRI ? (v->pet ? petri_next(s, v) : 0.0f)
                                           : plaits_next(v, gate);
    if (v->fade != 0) {
        y *= fade_step(s, v);
    }
    return y;
}

// n сэмплов слота со временем слота: то же, что на каждом сэмпле slot_tick и slot_next, но разбор типа - раз на
// кусок, а не на сэмпл (на плате обвязка сэмпла стоила слоту ~45 мкс на блок 2 мс). Гейт внутри куска не
// меняется: ноты приходят между кусками, а отложенное отпускание и смена типа идут по сэмплу.
static void slot_run(acid_synth_t *s, acid_slot_t *v, float *out, int n)
{
    while (n > 0) {
        int m = n;
        if (v->pend_note >= 0) {
            int32_t d = (int32_t)(v->pend_at - v->now); // отпускание - на d-м сэмпле отсюда
            m = d > 1 ? (d - 1 < n ? d - 1 : n) : 0;
        }
        if (m == 0 || v->fade != 0) {
            slot_tick(s, v);
            *out++ = slot_next(s, v, v->nheld > 0);
            n--;
            continue;
        }
        int gate = v->nheld > 0;
        v->now += (uint32_t)m;
        if (v->type == 0) {
            for (int i = 0; i < m; i++) {
                out[i] = voice_next(s, v, gate);
            }
        } else if (v->type == ACID_LAYER_OFF || (v->type == ACID_TYPE_PETRI && !v->pet)) {
            memset(out, 0, (size_t)m * sizeof *out);
        } else if (v->type == ACID_TYPE_PETRI) {
            for (int i = 0; i < m; i++) {
                out[i] = petri_next(s, v);
            }
        } else {
            plaits_run(v, gate, out, m);
        }
        out += m;
        n -= m;
    }
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
    // Защита на ступени 3: слой плавно уходит и, затихнув, не считается. Своя громкость у каждого слоя:
    // слои на разных ядрах.
    float *d = &s->layer_duck[k];
    if (s->tight == 3) {
        if (*d == 0.0f) {
            return y;
        }
        *d = *d > ACID_DUCK_DOWN ? *d - ACID_DUCK_DOWN : 0.0f;
    } else if (*d < 1.0f) {
        *d = *d < 1.0f - ACID_DUCK_UP ? *d + ACID_DUCK_UP : 1.0f;
    }
    int half = s->layer_half || s->tight >= 2; // ручкой или защитой
    if (half != lv->half) {
        layer_rate(s, lv, half);
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

// Слой синта k поверх out. Обычный случай (полная частота, защита не глушит, тип не меняется) - куском.
static void layer_mix(acid_synth_t *s, int k, float *out, size_t n)
{
    acid_slot_t *lv = &s->layer[k];
    float tmp[MIX_CHUNK];
    size_t i = 0;
    while (i < n) {
        if (!layer_on(lv)) {
            return;
        }
        int half = s->layer_half || s->tight >= 2;
        if (half || lv->half || s->tight == 3 || s->layer_duck[k] != 1.0f || lv->fade != 0) {
            out[i] = layer_add(s, k, out[i]);
            i++;
            continue;
        }
        size_t m = n - i < MIX_CHUNK ? n - i : MIX_CHUNK;
        slot_run(s, lv, tmp, (int)m);
        float g = s->layer_gain[k] * s->layer_duck[k];
        for (size_t j = 0; j < m; j++) {
            out[i + j] = out[i + j] + g * tmp[j];
        }
        lv->hlast = tmp[m - 1];
        i += m;
    }
}

void acid_synth_render_slot(acid_synth_t *s, int k, float *out, size_t n)
{
    slot_run(s, &s->slot[k], out, (int)n);
    layer_mix(s, k, out, n);
}

void acid_synth_render_layer(acid_synth_t *s, int k, float *out, size_t n)
{
    memset(out, 0, n * sizeof *out);
    layer_mix(s, k, out, n);
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
    ACID_PROF_T(t);
    slot_run(s, &s->slot[0], src->y, (int)n);
    s->clock_now += (uint32_t)n;
    ACID_PROF_ADD(ACID_PROF_SYNTH1, t);
    acid_kick_run(&s->kick, s->kick_gain * s->kick_vel, src->kick, (int)n); // голос за голосом, не сэмпл за сэмплом
    acid_hat_run(&s->hat, s->hat_gain * s->hat_vel, src->hat, (int)n);
    acid_clap_run(&s->clap, s->clap_gain * s->clap_vel, src->clap, (int)n);
    acid_snare_run(&s->snare, s->snare_gain * s->snare_vel, src->snare, (int)n);
    ACID_PROF_ADD(ACID_PROF_DRUMS, t);
}

void acid_synth_render_reverb(acid_synth_t *s, const float *send, float *wl, float *wr, size_t n)
{
    if (s->rv_wet == 0.0f || s->rv_duck == 0.0f) {
        memset(wl, 0, n * sizeof *wl);
        memset(wr, 0, n * sizeof *wr);
        return;
    }
    if (!acid_reverb_run(&s->reverb, send, wl, wr, (int)n)) {
        memset(wl, 0, n * sizeof *wl);
        memset(wr, 0, n * sizeof *wr);
    }
}

void acid_synth_render_mix_lr(acid_synth_t *s, float *l, float *r, size_t n, const float *slot1, const float *layer0,
                              const acid_synth_src_t *src, const acid_synth_side_t *side, float *rv_send,
                              const float *rv_wl, const float *rv_wr)
{
    ACID_PROF_T(t);
    // Стерео: панорамы по источникам, бочка в центре. Эффекты - проходами по куску: каждый зависит только от
    // своего входа и состояния, так что результат тот же, что сэмпл за сэмплом.
    const float *pl = s->pan_l, *pr = s->pan_r;
    float send[MIX_CHUNK], pump[MIX_CHUNK];
    acid_pump_run(&s->pump, pump, (int)n);
    for (size_t i = 0; i < n; i++) {
        float y = layer0 ? src->y[i] + layer0[i] : src->y[i], kick = src->kick[i], hat = src->hat[i], clap = src->clap[i], snare = src->snare[i];
        float s1 = slot1[i];
        float cl = side->cyl[i], cr = side->cyr[i]; // уже стерео с балансом
        float ql = side->pcl[i], qr = side->pcr[i]; // перкуссия, уже с панорамой
        // Посылы до пампинга и панорамы: повторы не качаются вместе с сухим звуком. Бочка в ревёрб не идёт.
        send[i] = s->send1 * y + s->send2 * s1 + s->send_drums * (hat + clap + snare + 0.5f * (cl + cr + ql + qr));
        rv_send[i] = s->rv_send1 * y + s->rv_send2 * s1 +
                     s->rv_send_drums * (hat + clap + snare + 0.5f * (cl + cr + ql + qr));
        l[i] = kick + pump[i] * (pl[ACID_PAN_SYNTH1] * y + pl[ACID_PAN_SYNTH2] * s1 + pl[ACID_PAN_HAT] * hat +
                                 pl[ACID_PAN_CLAP] * clap + pl[ACID_PAN_SNARE] * snare + cl + ql);
        r[i] = kick + pump[i] * (pr[ACID_PAN_SYNTH1] * y + pr[ACID_PAN_SYNTH2] * s1 + pr[ACID_PAN_HAT] * hat +
                                 pr[ACID_PAN_CLAP] * clap + pr[ACID_PAN_SNARE] * snare + cr + qr);
    }
    acid_delay_mix2(&s->delay, send, l, r, (int)n);
    if (rv_wl && s->rv_wet > 0.0f && (s->rv_duck > 0.0f || s->tight != 3)) {
        for (size_t i = 0; i < n; i++) {
            float duck = s->rv_duck;
            if (s->tight == 3) {
                s->rv_duck = duck > ACID_DUCK_DOWN ? duck - ACID_DUCK_DOWN : 0.0f;
            } else if (duck < 1.0f) {
                s->rv_duck = duck < 1.0f - ACID_DUCK_UP ? duck + ACID_DUCK_UP : 1.0f;
            }
            // Ширина: каналы wet сходятся к середине. На полной ширине narrow 0 и wet проходит как есть.
            float g = s->rv_wet * duck, cross = s->rv_narrow * (rv_wl[i] - rv_wr[i]);
            l[i] += g * (rv_wl[i] - cross);
            r[i] += g * (rv_wr[i] + cross);
        }
    }
    acid_stutter_run2(&s->stutter, l, r, (int)n);
    acid_crush_run2(&s->crush, l, r, (int)n);
    acid_djf_run2(&s->djf, l, r, (int)n);
    for (size_t i = 0; i < n; i++) {
        l[i] = s->master * OUT_PEAK * acid_tanh(s->drive * l[i]);
        r[i] = s->master * OUT_PEAK * acid_tanh(s->drive * r[i]);
    }
    ACID_PROF_ADD(ACID_PROF_FX, t);
}

void acid_synth_render_with_lr(acid_synth_t *s, float *l, float *r, size_t n, const float *slot1)
{
    acid_synth_side_t side;
    acid_synth_src_t src;
    float layer0[ACID_SPLIT_MAX], rv_wl[ACID_SPLIT_MAX], rv_wr[ACID_SPLIT_MAX];
    for (size_t off = 0; off < n; off += ACID_SPLIT_MAX) {
        size_t m = n - off < ACID_SPLIT_MAX ? n - off : ACID_SPLIT_MAX;
        acid_synth_render_layer(s, 0, layer0, m);
        acid_synth_render_src(s, &src, m);
        acid_synth_render_side(s, &side, m);
        acid_synth_render_reverb(s, s->rv_prev, rv_wl, rv_wr, m);
        acid_synth_render_mix_lr(s, l + off, r + off, m, slot1 + off, layer0, &src, &side, s->rv_prev, rv_wl, rv_wr);
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
        int page = k < ACID_SLOTS ? PAGE_SYNTH1 + k : ACID_PAGE_LAYER1 + k - ACID_SLOTS;
        *b++ = (uint8_t)v->sel_type;
        memcpy(b, v->knob, sizeof v->knob);
        if (v->sel_type < ACID_TYPES && s->mod_mask[page]) {
            uint8_t *row = b + (size_t)v->sel_type * ACID_KNOBS;
            for (int e = 1; e < ACID_KNOBS; e++) {
                if (s->mod_base[page][e] != KNOB_NONE) {
                    row[e] = (s->mod_unset[k] & (1u << e)) ? KNOB_NONE : s->mod_base[page][e];
                }
            }
        }
        b += sizeof v->knob;
    }
    memcpy(b, s->tonal.knob, sizeof s->tonal.knob);
    b += sizeof s->tonal.knob;
    for (int i = 0; i < ACID_SAVED_PAGES; i++) {
        for (int e = 0; e < ACID_KNOBS; e++) {
            *b++ = e > 0 ? (uint8_t)acid_synth_knob(s, SAVED_PAGES[i], e) : 0;
        }
    }
    memcpy(b, s->rv_raw, sizeof s->rv_raw);
    b += sizeof s->rv_raw;
    memcpy(b, s->pitch_raw, sizeof s->pitch_raw);
    return ACID_SYNTH_BLOB;
}

int acid_synth_import(acid_synth_t *s, const void *buf, size_t len)
{
    const uint8_t *b = buf;
    // Версия 2 (до слоёв) - только синты; 2-3 - по 25 типов (без PETRI), у слоя «выкл» = 25.
    int ver = len == ACID_SYNTH_BLOB && memcmp(b, BLOB_MAGIC, sizeof BLOB_MAGIC) == 0       ? 10
              : len == ACID_SYNTH_BLOB_V9 && memcmp(b, "AS5\x09", 4) == 0               ? 9
              : len == ACID_SYNTH_BLOB_V8 && memcmp(b, "AS5\x08", 4) == 0               ? 8
              : len == ACID_SYNTH_BLOB_V7 && memcmp(b, "AS5\x07", 4) == 0               ? 7
              : len == ACID_SYNTH_BLOB_V7 && memcmp(b, "AS5\x06", 4) == 0               ? 6
              : len == ACID_SYNTH_BLOB_V5 && memcmp(b, "AS5\x05", 4) == 0               ? 5
              : len == ACID_SYNTH_BLOB_V4 && memcmp(b, "AS5\x04", 4) == 0               ? 4
              : len == BLOB_OLD(2 * ACID_SLOTS) && memcmp(b, "AS5\x03", 4) == 0 ? 3
              : len == BLOB_OLD(ACID_SLOTS) && memcmp(b, "AS5\x02", 4) == 0     ? 2
                                                                                   : 0;
    if (!ver) {
        return 0;
    }
    memset(s->mod_base, KNOB_NONE, sizeof s->mod_base);
    memset(s->mod_mask, 0, sizeof s->mod_mask);
    memset(s->mod_unset, 0, sizeof s->mod_unset);
    const int types = ver >= 4 ? ACID_TYPES : OLD_TYPES;
    b += sizeof BLOB_MAGIC;
    for (int k = 0; k < (ver == 2 ? 1 : 2) * ACID_SLOTS; k++) {
        acid_slot_t *v = k < ACID_SLOTS ? &s->slot[k] : &s->layer[k - ACID_SLOTS];
        int t = b[0], layer = k >= ACID_SLOTS;
        if (ver < 4 && layer && t == OLD_TYPES) {
            t = ACID_LAYER_OFF;
        }
        int ok = t < types && !(layer && t == ACID_TYPE_PETRI);
        v->sel_type = ok ? t : layer && t == ACID_LAYER_OFF ? ACID_LAYER_OFF : (layer ? ACID_LAYER_OFF : 0);
        memset(v->knob, KNOB_NONE, sizeof v->knob);
        memcpy(v->knob, b + 1, (size_t)types * ACID_KNOBS);
        b += 1 + (size_t)types * ACID_KNOBS;
        v->type = v->next_type = v->sel_type;
        v->fade = 0;
        type_defaults(s, v);
    }
    for (int i = 0; i < ACID_TONAL_KNOBS; i++) {
        if (b[i] != KNOB_NONE) {
            acid_tonal_set(&s->tonal, i, b[i]);
        }
    }
    b += ACID_TONAL_KNOBS;
    for (int i = 0; ver >= 5 && i < ACID_SAVED_PAGES; i++, b += ACID_KNOBS) {
        for (int e = 1; e < ACID_KNOBS; e++) {
            int v = b[e];
            if (ver == 6 && SAVED_PAGES[i] == PAGE_FX && e >= 14) {
                v = e == 14 ? 0 : b[14]; // wet стоял на месте LYR HALF
            }
            if (!performance_knob(SAVED_PAGES[i], e) && v <= 127) {
                apply_page(s, SAVED_PAGES[i], e, v);
            }
        }
    }
    for (int e = 0; ver >= 6 && e < (ver >= 8 ? ACID_REVERB_KNOBS : 5); e++) {
        if (b[e] <= 127) {
            apply_page(s, ACID_PAGE_REVERB, e + 1, b[e]);
        }
    }
    b += ACID_REVERB_KNOBS;
    int n_pitch = ver >= 10 ? ACID_PITCH_KNOBS : ver == 9 ? 3 : 0;
    for (int e = 0; e < ACID_PITCH_KNOBS; e++) {
        int val = e < n_pitch ? b[e] : (e >= 3 ? 127 : 0);
        apply_page(s, ACID_PAGE_PITCH, e + 1, val);
    }
    if (s->drone) {
        drone_retune(s);
    }
    return 1;
}
