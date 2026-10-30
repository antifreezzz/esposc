#include "acid_perc.h"

#include <math.h>
#include <string.h>

#include "acid_fx.h"
#include "acid_math.h"
#include "acid_noise.h"

#define PI_F 3.14159265f
#define SUB 8               // сэмплов между пересчётами высоты и резонаторов
#define NYQ 0.45f           // выше этой доли частоты выборки генераторы зажаты, моды молчат
#define SILENT 1e-4f        // голос тише - свободен
#define RETRIG_S 0.02f      // повторный удар той же ноты: прежний уходит
#define STEAL_S 0.005f      // забор голоса
#define CLICK_S 0.0015f     // щелчок
#define EXC_S 0.002f        // возбудитель модальной модели
#define LEVEL_MAX 1.0f
#define MODAL_G_REF 0.01309f // tan(pi * 200 / 48000)

// Звук после включения: ручки по числу на экране, порядок как в enum ручек.
static const uint8_t DEFAULTS[ACID_PERC_N][ACID_PERC_KNOBS] = {
    // модель строй спад глуб время тон хар щелч перегр краш громк пан чувств интерв ширина
    {ACID_PERC_ANALOG, 48, 79, 25, 86, 20, 40, 30, 0, 0, 90, 64, 100, 37, 50},   // томы: ~80 Гц, ~0.35 с
    {ACID_PERC_ANALOG, 51, 19, 18, 41, 90, 70, 110, 0, 0, 80, 72, 100, 0, 0},    // рим: ~500 Гц, ~40 мс
    {ACID_PERC_SQUARES, 55, 70, 0, 64, 83, 41, 10, 0, 0, 70, 56, 100, 0, 0},     // ковбелл: ~540 Гц, x1.48
};

// Раскладки мод модальной модели, ручка «характер» плавно идёт от первой к последней.
static const float MODES[4][ACID_PERC_MODES] = {
    {1.0f, 1.59f, 2.14f, 2.30f}, // мембрана
    {1.0f, 2.76f, 5.40f, 8.93f}, // брусок
    {1.0f, 1.183f, 1.506f, 2.0f}, // колокол
    {1.0f, 1.71f, 2.93f, 4.47f}, // неровная
};
static const float MODE_GAIN[ACID_PERC_MODES] = {1.0f, 0.7f, 0.5f, 0.35f};

// Громкость корпуса моделей: близкая громкость на ручках по умолчанию (замер на хосте).
static const float MODEL_GAIN[ACID_PERC_MODELS] = {1.0f, 0.8f, 0.9f, 16.0f, 3.0f};

static float coef(float tau_s, float sr)
{
    return expf(-1.0f / (tau_s * sr));
}

static float expo(int v, float a, float b)
{
    return a * powf(b / a, (float)v / 127.0f);
}

// sin(2 pi p) для p в [0, 1): парабола с уточнением, ошибка ~0.1%.
static inline float fsin(float p)
{
    float x = 2.0f * p - 1.0f;
    float y = 4.0f * x * (1.0f - fabsf(x));
    y = 0.225f * (y * fabsf(y) - y) + y;
    return -y;
}

static inline float wrap(float p)
{
    return p - floorf(p);
}

void acid_perc_set(acid_perc_t *p, int inst, int knob, int v)
{
    if (inst < 0 || inst >= ACID_PERC_N || knob < 0 || knob >= ACID_PERC_KNOBS || v < 0 || v > 127) {
        return;
    }
    acid_perc_inst_t *in = &p->inst[inst];
    float x = (float)v / 127.0f;
    switch (knob) {
    case ACID_PERC_MODEL:
        if (v >= ACID_PERC_MODELS) {
            return; // не модель: число на экране вне списка
        }
        in->model = v;
        break;
    case ACID_PERC_TUNE: in->hz = inst == ACID_PERC_TOMS ? expo(v, 40.0f, 250.0f) : expo(v, 200.0f, 2000.0f); break;
    case ACID_PERC_DECAY: in->amp_coef = coef(expo(v, 0.02f, 2.0f), p->sr); break;
    case ACID_PERC_PDEPTH: in->pdepth = 36.0f * x; break;
    case ACID_PERC_PTIME: in->pitch_coef = coef(expo(v, 0.002f, 0.3f), p->sr); break;
    case ACID_PERC_TONE: in->tone = x; break;
    case ACID_PERC_CHAR: in->chr = x; break;
    case ACID_PERC_CLICK: in->click = x; break;
    case ACID_PERC_DRIVE: in->drive = 1.0f + 24.0f * x * x; break;
    case ACID_PERC_CRUSH:
        in->hold = v ? 1 + (int)(31.0f * x * x) : 1;
        in->quant = v ? exp2f(-(12.0f - 9.0f * x)) : 0.0f;
        break;
    case ACID_PERC_LEVEL: in->gain = LEVEL_MAX * x; break;
    case ACID_PERC_PAN: break;
    case ACID_PERC_SENS: in->sens = x; break;
    case ACID_PERC_INTERVAL:
    case ACID_PERC_WIDTH:
        if (inst != ACID_PERC_TOMS) {
            return; // у рима и ковбелла этих ручек нет
        }
        if (knob == ACID_PERC_INTERVAL) {
            in->interval = 12.0f * x;
        } else {
            in->width = x;
        }
        break;
    default: return;
    }
    in->knob[knob] = (uint8_t)v;
}

void acid_perc_init(acid_perc_t *p, float sr)
{
    memset(p, 0, sizeof *p);
    p->sr = sr;
    p->seed = 0x2545f491u;
    for (int i = 0; i < ACID_PERC_N; i++) {
        for (int k = 0; k < ACID_PERC_KNOBS; k++) {
            int v = DEFAULTS[i][k];
            // Интервал и ширину у рима и ковбелла set не пишет: сырые значения ставим сами.
            p->inst[i].knob[k] = (uint8_t)v;
            acid_perc_set(p, i, k, v);
        }
    }
}

int acid_perc_inst_of(int note, int *tom)
{
    static const int8_t TOM[] = {41, 43, 45, 47, 48, 50};
    if (tom) {
        *tom = -1;
    }
    for (int i = 0; i < 6; i++) {
        if (note == TOM[i]) {
            if (tom) {
                *tom = i;
            }
            return ACID_PERC_TOMS;
        }
    }
    return note == 37 ? ACID_PERC_RIM : note == 56 ? ACID_PERC_COW : -1;
}

static float level(const acid_perc_voice_t *v)
{
    return v->amp * (v->env > v->cenv ? v->env : v->cenv) * v->fade;
}

static void fade_out(acid_perc_t *p, acid_perc_voice_t *v, float s)
{
    float step = 1.0f / (s * p->sr);
    if (v->fade_step == 0.0f || step > v->fade_step) {
        v->fade_step = step;
    }
}

void acid_perc_trigger(acid_perc_t *p, int note, float vel)
{
    int tom;
    int inst = acid_perc_inst_of(note, &tom);
    if (inst < 0) {
        return;
    }
    int sounding = 0;
    for (int i = 0; i < ACID_PERC_SLOTS; i++) {
        acid_perc_voice_t *v = &p->v[i];
        if (v->active && v->fade_step == 0.0f) {
            if (v->note == note) {
                fade_out(p, v, RETRIG_S); // мембрану ударили снова
            } else {
                sounding++;
            }
        }
    }
    while (sounding >= ACID_PERC_SOUNDING) {
        acid_perc_voice_t *q = NULL;
        for (int i = 0; i < ACID_PERC_SLOTS; i++) {
            acid_perc_voice_t *v = &p->v[i];
            if (v->active && v->fade_step == 0.0f && (!q || level(v) < level(q))) {
                q = v;
            }
        }
        fade_out(p, q, STEAL_S);
        sounding--;
    }
    acid_perc_voice_t *slot = NULL;
    for (int i = 0; i < ACID_PERC_SLOTS && !slot; i++) {
        slot = p->v[i].active ? NULL : &p->v[i];
    }
    if (!slot) { // все слоты уходят: берём самый тихий уходящий
        for (int i = 0; i < ACID_PERC_SLOTS; i++) {
            if (!slot || level(&p->v[i]) < level(slot)) {
                slot = &p->v[i];
            }
        }
        p->hard_cuts += level(slot) > SILENT;
    }
    const acid_perc_inst_t *in = &p->inst[inst];
    memset(slot, 0, sizeof *slot);
    slot->active = 1;
    slot->inst = (uint8_t)inst;
    slot->note = (uint8_t)note;
    slot->tom = (int8_t)tom;
    slot->vel = vel;
    slot->amp = 1.0f - in->sens * (1.0f - vel);
    slot->env = slot->penv = slot->cenv = slot->fade = 1.0f;
    slot->hold_left = 0;
    p->seed = p->seed * 1664525u + 1013904223u;
    slot->rng = p->seed | 1u;
}

// Коэффициенты TPT SVF: полосовой с пиком 1/k на частоте среза.
typedef struct {
    float a1, a2, a3, k, gain;
} res_t;

static inline void res_set(res_t *r, float f, float k, float gain, float sr)
{
    if (f >= NYQ * sr) {
        r->a1 = 1.0f; // мода выше зажатия молчит
        r->a2 = r->a3 = 0.0f;
        r->k = k;
        r->gain = 0.0f;
        return;
    }
    float g = tanf(PI_F * f / sr);
    r->a1 = 1.0f / (1.0f + g * (g + k));
    r->a2 = g * r->a1;
    r->a3 = g * r->a2;
    r->k = k;
    r->gain = gain;
}

static inline float res_bp(float *ic, const res_t *r, float in)
{
    float v3 = in - ic[1];
    float v1 = r->a1 * ic[0] + r->a2 * v3;
    float v2 = ic[1] + r->a2 * ic[0] + r->a3 * v3;
    ic[0] = 2.0f * v1 - ic[0];
    ic[1] = 2.0f * v2 - ic[1];
    return v1;
}

static void render_voice(acid_perc_t *p, acid_perc_voice_t *v, float *l, float *r, int n)
{
    const acid_perc_inst_t *in = &p->inst[v->inst];
    const float sr = p->sr;
    const int model = in->model;
    // Сильный удар ярче и с большей глубиной высоты; при чувствительности 0 все как сильный.
    float soft = in->sens * (1.0f - v->vel);
    float tone = in->tone * (1.0f - 0.25f * soft);
    float pdepth = in->pdepth * (1.0f - 0.4f * soft);
    float hz = in->hz;
    float pos = (float)in->knob[ACID_PERC_PAN];
    if (v->tom >= 0) {
        hz *= exp2f((float)v->tom * in->interval / 12.0f);
        pos += in->width * 63.0f * ((float)v->tom / 2.5f - 1.0f);
        pos = pos < 0.0f ? 0.0f : pos > 127.0f ? 127.0f : pos;
    }
    float gl, gr;
    acid_pan(pos, &gl, &gr);
    float out_gain = v->amp * in->gain;
    float body_gain = MODEL_GAIN[model];
    float inv_drive = in->drive > 1.0f ? 1.0f / acid_tanh(in->drive) : 1.0f;
    float click_coef = coef(CLICK_S, sr), exc_coef = coef(EXC_S, sr);
    // Модель 0: форма и второй обертон. Модель 2: индекс и отношение. Модель 3: моды.
    float tri_mix = tone < 0.5f ? 2.0f * tone : 1.0f;
    float clip = tone > 0.5f ? 1.0f + 16.0f * (tone - 0.5f) : 1.0f;
    float inv_clip = clip > 1.0f ? 1.0f / acid_tanh(clip) : 1.0f;
    float ratio = model == ACID_PERC_SQUARES ? 1.0f + 1.5f * in->chr : model == ACID_PERC_FM ? 0.5f + 7.5f * in->chr : 1.59f;
    float fm_index = 8.0f * tone / (2.0f * PI_F);
    float mratio[ACID_PERC_MODES];
    if (model == ACID_PERC_MODAL) {
        float c = in->chr * 3.0f;
        int a = c >= 3.0f ? 2 : (int)c;
        float t = c - (float)a;
        for (int m = 0; m < ACID_PERC_MODES; m++) {
            mratio[m] = MODES[a][m] + t * (MODES[a + 1][m] - MODES[a][m]);
        }
    }
    float exc_lp = 0.05f + 0.9f * tone;
    float noise_lp = 0.1f;
    res_t rs[ACID_PERC_MODES];

    for (int i0 = 0; i0 < n; i0 += SUB) {
        int m_end = i0 + SUB < n ? i0 + SUB : n;
        float f = hz * exp2f(pdepth * v->penv * (1.0f / 12.0f));
        float fa = f < NYQ * sr ? f : NYQ * sr;
        float fb = f * ratio < NYQ * sr ? f * ratio : NYQ * sr;
        float inc_a = fa / sr, inc_b = fb / sr;
        if (model == ACID_PERC_SQUARES) {
            res_set(&rs[0], expo((int)(tone * 127.0f), 200.0f, 10000.0f), 0.35f, 0.35f, sr);
        } else if (model == ACID_PERC_NOISE) {
            float q = 0.7f * powf(57.0f, tone);
            res_set(&rs[0], f, 1.0f / q, sqrtf(1.0f / q), sr);
        } else if (model == ACID_PERC_MODAL) {
            float q0 = 60.0f * (0.5f + tone);
            // Отклик резонатора на импульс растёт с частотой: корень из нормировки по основной моде
            // держит том и рим на близкой громкости.
            float norm = 0.25f * sqrtf(MODAL_G_REF / tanf(PI_F * (fa < 20.0f ? 20.0f : fa) / sr));
            for (int m = 0; m < ACID_PERC_MODES; m++) {
                float q = q0 * (1.0f - 0.6f * (1.0f - tone) * (float)m / 3.0f);
                res_set(&rs[m], f * mratio[m], 1.0f / q, MODE_GAIN[m] * norm, sr);
            }
        }
        for (int i = i0; i < m_end; i++) {
            float nz = acid_noise(&v->rng);
            float y;
            switch (model) {
            case ACID_PERC_ANALOG: {
                float s = fsin(v->pa);
                float tri = 4.0f * fabsf(v->pa - 0.5f) - 1.0f;
                y = s + tri_mix * (tri - s);
                if (clip > 1.0f) {
                    y = acid_tanh(clip * y) * inv_clip;
                }
                y += 0.7f * in->chr * v->env * fsin(v->pb); // обертон уходит быстрее корпуса
                break;
            }
            case ACID_PERC_SQUARES: {
                float sq = (v->pa < 0.5f ? 1.0f : -1.0f) + (v->pb < 0.5f ? 1.0f : -1.0f);
                y = res_bp(v->res[0], &rs[0], sq) * rs[0].gain / rs[0].k * 0.5f;
                break;
            }
            case ACID_PERC_FM:
                y = fsin(wrap(v->pa + fm_index * (0.25f + 0.75f * v->env) * fsin(v->pb)));
                break;
            case ACID_PERC_MODAL: {
                float exc = (v->cenv == 1.0f ? 1.0f : 0.0f) + 0.5f * nz * v->cenv;
                v->lp += exc_lp * (exc - v->lp);
                y = 0.0f;
                for (int m = 0; m < ACID_PERC_MODES; m++) {
                    y += rs[m].gain * res_bp(v->res[m], &rs[m], v->lp);
                }
                break;
            }
            default: { // шум
                v->lp += noise_lp * (nz - v->lp);
                float src = nz * in->chr + 3.0f * v->lp * (1.0f - in->chr);
                y = res_bp(v->res[0], &rs[0], src) * rs[0].gain;
                break;
            }
            }
            y = body_gain * y * v->env + in->click * v->cenv * nz;
            if (in->drive > 1.0f) {
                y = acid_tanh(in->drive * y) * inv_drive;
            }
            if (in->hold > 1 || in->quant > 0.0f) {
                if (v->hold_left <= 0) {
                    v->held = in->quant > 0.0f ? in->quant * floorf(y / in->quant + 0.5f) : y;
                    v->hold_left = in->hold;
                }
                v->hold_left--;
                y = v->held;
            }
            y *= out_gain * v->fade;
            l[i] += gl * y;
            r[i] += gr * y;
            v->pa = wrap(v->pa + inc_a);
            v->pb = wrap(v->pb + inc_b);
            v->env *= in->amp_coef;
            v->penv *= in->pitch_coef;
            v->cenv *= model == ACID_PERC_MODAL ? exc_coef : click_coef;
            if (v->fade_step != 0.0f) {
                v->fade -= v->fade_step;
                if (v->fade <= 0.0f) {
                    v->active = 0;
                    return;
                }
            }
        }
        if (level(v) < SILENT) {
            v->active = 0;
            return;
        }
    }
}

void acid_perc_render(acid_perc_t *p, float *l, float *r, int n)
{
    memset(l, 0, sizeof(float) * (size_t)n);
    memset(r, 0, sizeof(float) * (size_t)n);
    for (int i = 0; i < ACID_PERC_SLOTS; i++) {
        if (p->v[i].active) {
            render_voice(p, &p->v[i], l, r, n);
        }
    }
}

int acid_perc_voices(const acid_perc_t *p)
{
    int c = 0;
    for (int i = 0; i < ACID_PERC_SLOTS; i++) {
        c += p->v[i].active;
    }
    return c;
}

int acid_perc_sounding(const acid_perc_t *p)
{
    int c = 0;
    for (int i = 0; i < ACID_PERC_SLOTS; i++) {
        c += p->v[i].active && p->v[i].fade_step == 0.0f;
    }
    return c;
}
