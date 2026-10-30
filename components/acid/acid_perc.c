#include "acid_perc.h"

#include <math.h>
#include <string.h>

#include "acid_fx.h"
#include "acid_math.h"
#include "acid_noise.h"

#define PI_F 3.14159265f
#define SUB 32              // сэмплов между пересчётами высоты и резонаторов
#define NYQ 0.45f           // выше этой доли частоты выборки генераторы зажаты, моды молчат
#define SILENT 1e-4f        // голос тише - свободен
#define RETRIG_S 0.02f      // повторный удар той же ноты: прежний уходит
#define STEAL_S 0.005f      // забор голоса
#define CLICK_S 0.0015f     // щелчок
#define EXC_S 0.002f        // возбудитель модальной модели
#define LEVEL_MAX 1.0f
#define MODAL_G_REF 0.01309f // tan(pi * 200 / 48000)
#define MODAL_2POLE 1.0f     // уровень двухполюсного резонатора к прежнему полосовому TPT (замер на хосте)

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
    p->limit = ACID_PERC_SOUNDING;
    p->click_coef = coef(CLICK_S, sr);
    p->exc_coef = coef(EXC_S, sr);
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

// Самые тихие звучащие уходят за 5 мс, пока звучащих больше keep.
static void shed(acid_perc_t *p, int sounding, int keep)
{
    for (; sounding > keep; sounding--) {
        acid_perc_voice_t *q = NULL;
        for (int i = 0; i < ACID_PERC_SLOTS; i++) {
            acid_perc_voice_t *v = &p->v[i];
            if (v->active && v->fade_step == 0.0f && (!q || level(v) < level(q))) {
                q = v;
            }
        }
        if (!q) {
            return;
        }
        fade_out(p, q, STEAL_S);
    }
}

void acid_perc_set_limit(acid_perc_t *p, int n)
{
    p->limit = n < 1 ? 1 : n > ACID_PERC_SOUNDING ? ACID_PERC_SOUNDING : n;
    shed(p, acid_perc_sounding(p), p->limit);
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
    shed(p, sounding, p->limit - 1); // место новому удару
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

// Без libm в рендере: на плате вызовы tanf, exp2f, floorf из флэша стоили больше всего голоса (замер этапа 10).
// 2^x: целая часть в показатель, дробная - полином (ошибка ~0.01%).
static inline float fexp2(float x)
{
    x = x < -60.0f ? -60.0f : x > 60.0f ? 60.0f : x;
    int i = (int)x;
    i -= x < (float)i;
    float f = x - (float)i;
    float m = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    union {
        float f;
        int32_t i;
    } u = {m};
    u.i += i << 23;
    return u.f;
}

// tan(x) для 0 <= x < 1.42 (частота до 0.45 sr): Паде 3/2, ошибка до ~3% у верхнего края.
static inline float ftan(float x)
{
    float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

// p в [0, 1) для p в (-2, 3).
static inline float wrap(float p)
{
    p -= (float)(int)p;
    return p < 0.0f ? p + 1.0f : p;
}

// Коэффициенты TPT SVF: полосовой с пиком 1/k на частоте среза; out - множитель выхода.
typedef struct {
    float a1, a2, a3, out;
} res_t;

static inline void res_set(res_t *r, float w, float k, float out)
{
    if (w >= NYQ) {
        r->a1 = 1.0f; // мода выше зажатия молчит
        r->a2 = r->a3 = r->out = 0.0f;
        return;
    }
    float g = ftan(PI_F * w);
    r->a1 = 1.0f / (1.0f + g * (g + k));
    r->a2 = g * r->a1;
    r->a3 = g * r->a2;
    r->out = out;
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

// Постоянное на кусок рендера для одного голоса.
typedef struct {
    float gl, gr, body_gain, drive, inv_drive, quant, inv_quant, click, chr, amp_coef, pitch_coef, cenv_coef;
    float tri_mix, clip, inv_clip, fm_index, exc_lp;
    int hold;
    res_t rs;                                // квадраты и шум: один полосовой TPT
    float b1[ACID_PERC_MODES], b2[ACID_PERC_MODES], mo[ACID_PERC_MODES]; // модальная: двухполюсные резонаторы
} pctx_t;

// Подблок одной модели: model - константа после встраивания, ветвления по модели из цикла уходят.
static inline __attribute__((always_inline)) int sub_block(acid_perc_voice_t *v, const pctx_t *c, float *l, float *r,
                                                           int i0, int i1, float inc_a, float inc_b, const int model)
{
    float pa = v->pa, pb = v->pb, env = v->env, penv = v->penv, cenv = v->cenv, lp = v->lp, fade = v->fade;
    const float fade_step = v->fade_step;
    uint32_t rng = v->rng;
    float *z = &v->res[0][0];
    for (int i = i0; i < i1; i++) {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        float nz = (float)(rng >> 8) * (2.0f / 16777215.0f) - 1.0f;
        float y;
        if (model == ACID_PERC_ANALOG) {
            float s = fsin(pa);
            float tri = 4.0f * fabsf(pa - 0.5f) - 1.0f;
            y = s + c->tri_mix * (tri - s);
            if (c->clip > 1.0f) {
                y = acid_tanh(c->clip * y) * c->inv_clip;
            }
            y += 0.7f * c->chr * env * fsin(pb); // обертон уходит быстрее корпуса
        } else if (model == ACID_PERC_SQUARES) {
            float sq = (pa < 0.5f ? 1.0f : -1.0f) + (pb < 0.5f ? 1.0f : -1.0f);
            y = res_bp(z, &c->rs, sq) * c->rs.out;
        } else if (model == ACID_PERC_FM) {
            y = fsin(wrap(pa + c->fm_index * (0.25f + 0.75f * env) * fsin(pb)));
        } else if (model == ACID_PERC_MODAL) {
            float exc = (cenv == 1.0f ? 1.0f : 0.0f) + 0.5f * nz * cenv;
            lp += c->exc_lp * (exc - lp);
            y = 0.0f;
            for (int m = 0; m < ACID_PERC_MODES; m++) {
                float o = c->b1[m] * z[2 * m] - c->b2[m] * z[2 * m + 1] + lp;
                z[2 * m + 1] = z[2 * m];
                z[2 * m] = o;
                y += c->mo[m] * o;
            }
        } else { // шум
            lp += 0.1f * (nz - lp);
            float src = nz * c->chr + 3.0f * lp * (1.0f - c->chr);
            y = res_bp(z, &c->rs, src) * c->rs.out;
        }
        y = c->body_gain * y * env + c->click * cenv * nz;
        if (c->drive > 1.0f) {
            y = acid_tanh(c->drive * y) * c->inv_drive;
        }
        if (c->hold > 1 || c->quant > 0.0f) {
            if (v->hold_left <= 0) {
                float q = y;
                if (c->quant > 0.0f) {
                    float t = y * c->inv_quant;
                    q = c->quant * (float)(int)(t + (t < 0.0f ? -0.5f : 0.5f));
                }
                v->held = q;
                v->hold_left = c->hold;
            }
            v->hold_left--;
            y = v->held;
        }
        y *= fade;
        l[i] += c->gl * y;
        r[i] += c->gr * y;
        pa += inc_a;
        pa -= pa >= 1.0f ? 1.0f : 0.0f;
        pb += inc_b;
        pb -= pb >= 1.0f ? 1.0f : 0.0f;
        env *= c->amp_coef;
        penv *= c->pitch_coef;
        cenv *= c->cenv_coef;
        if (fade_step != 0.0f) {
            fade -= fade_step;
            if (fade <= 0.0f) {
                v->active = 0;
                return 0;
            }
        }
    }
    v->pa = pa;
    v->pb = pb;
    v->env = env;
    v->penv = penv;
    v->cenv = cenv;
    v->lp = lp;
    v->fade = fade;
    v->rng = rng;
    return 1;
}

static void render_voice(acid_perc_t *p, acid_perc_voice_t *v, float *l, float *r, int n)
{
    const acid_perc_inst_t *in = &p->inst[v->inst];
    const float inv_sr = 1.0f / p->sr;
    const int model = in->model;
    pctx_t c;
    // Сильный удар ярче и с большей глубиной высоты; при чувствительности 0 все как сильный.
    float soft = in->sens * (1.0f - v->vel);
    float tone = in->tone * (1.0f - 0.25f * soft);
    float pd12 = in->pdepth * (1.0f - 0.4f * soft) * (1.0f / 12.0f);
    float w0 = in->hz * inv_sr; // строй в долях частоты выборки
    float pos = (float)in->knob[ACID_PERC_PAN];
    if (v->tom >= 0) {
        w0 *= fexp2((float)v->tom * in->interval * (1.0f / 12.0f));
        pos += in->width * 63.0f * ((float)v->tom * 0.4f - 1.0f);
        pos = pos < 0.0f ? 0.0f : pos > 127.0f ? 127.0f : pos;
    }
    acid_pan(pos, &c.gl, &c.gr);
    float out_gain = v->amp * in->gain;
    c.gl *= out_gain;
    c.gr *= out_gain;
    c.body_gain = MODEL_GAIN[model];
    c.drive = in->drive;
    c.inv_drive = c.drive > 1.0f ? 1.0f / acid_tanh(c.drive) : 1.0f;
    c.hold = in->hold;
    c.quant = in->quant;
    c.inv_quant = c.quant > 0.0f ? 1.0f / c.quant : 0.0f;
    c.click = in->click;
    c.chr = in->chr;
    c.amp_coef = in->amp_coef;
    c.pitch_coef = in->pitch_coef;
    c.cenv_coef = model == ACID_PERC_MODAL ? p->exc_coef : p->click_coef;
    // Модель 0: форма и второй обертон. Модель 2: индекс и отношение. Модель 3: моды.
    c.tri_mix = tone < 0.5f ? 2.0f * tone : 1.0f;
    c.clip = tone > 0.5f ? 1.0f + 16.0f * (tone - 0.5f) : 1.0f;
    c.inv_clip = c.clip > 1.0f ? 1.0f / acid_tanh(c.clip) : 1.0f;
    c.fm_index = 8.0f * tone / (2.0f * PI_F);
    c.exc_lp = 0.05f + 0.9f * tone;
    const float ratio = model == ACID_PERC_SQUARES ? 1.0f + 1.5f * c.chr : model == ACID_PERC_FM ? 0.5f + 7.5f * c.chr : 1.59f;
    float mratio[ACID_PERC_MODES], mq[ACID_PERC_MODES], mout[ACID_PERC_MODES];
    float nz_k = 0.0f, nz_out = 0.0f;
    if (model == ACID_PERC_MODAL) {
        float x = c.chr * 3.0f;
        int a = x >= 3.0f ? 2 : (int)x;
        float t = x - (float)a;
        float q0 = 60.0f * (0.5f + tone);
        // Отклик резонатора на импульс растёт с частотой: корень из нормировки по основной моде
        // держит том и рим на близкой громкости.
        float wn = w0 < 20.0f * inv_sr ? 20.0f * inv_sr : w0 > NYQ ? NYQ : w0;
        float norm = 0.25f * sqrtf(MODAL_G_REF / ftan(PI_F * wn));
        for (int m = 0; m < ACID_PERC_MODES; m++) {
            mratio[m] = MODES[a][m] + t * (MODES[a + 1][m] - MODES[a][m]);
            mq[m] = PI_F / (q0 * (1.0f - 0.6f * (1.0f - tone) * (float)m / 3.0f));
            mout[m] = MODE_GAIN[m] * norm;
        }
    } else if (model == ACID_PERC_SQUARES) {
        res_set(&c.rs, 200.0f * fexp2(tone * 5.643856f) * inv_sr, 0.35f, 0.5f); // 200 Гц .. 10 кГц
    } else if (model == ACID_PERC_NOISE) {
        float q = 0.7f * fexp2(tone * 5.83289f); // 0.7 .. 40
        nz_k = 1.0f / q;
        nz_out = sqrtf(nz_k);
    }

    for (int i0 = 0; i0 < n; i0 += SUB) {
        int i1 = i0 + SUB < n ? i0 + SUB : n;
        float w = w0 * fexp2(pd12 * v->penv);
        float inc_a = w < NYQ ? w : NYQ;
        float inc_b = w * ratio < NYQ ? w * ratio : NYQ;
        if (model == ACID_PERC_NOISE) {
            res_set(&c.rs, w, nz_k, nz_out);
        } else if (model == ACID_PERC_MODAL) {
            for (int m = 0; m < ACID_PERC_MODES; m++) {
                // y = b1 y1 - b2 y2 + x, полюса r e^(+-i th): th = 2 pi wm, r ~ 1 - pi wm / Q.
                // Импульс даёт амплитуду ~1/sin(th): sin^2 на выходе ведёт к уровню полосового TPT.
                float wm = w * mratio[m];
                if (wm >= NYQ) {
                    c.b1[m] = c.b2[m] = c.mo[m] = 0.0f; // мода выше зажатия молчит
                    continue;
                }
                float rr = 1.0f - mq[m] * wm;
                float sn = fsin(wm), cs = fsin(wm + 0.25f);
                c.b1[m] = 2.0f * rr * cs;
                c.b2[m] = rr * rr;
                c.mo[m] = mout[m] * MODAL_2POLE * sn * sn;
            }
        }
        int alive;
        switch (model) {
        case ACID_PERC_ANALOG: alive = sub_block(v, &c, l, r, i0, i1, inc_a, inc_b, ACID_PERC_ANALOG); break;
        case ACID_PERC_SQUARES: alive = sub_block(v, &c, l, r, i0, i1, inc_a, inc_b, ACID_PERC_SQUARES); break;
        case ACID_PERC_FM: alive = sub_block(v, &c, l, r, i0, i1, inc_a, inc_b, ACID_PERC_FM); break;
        case ACID_PERC_MODAL: alive = sub_block(v, &c, l, r, i0, i1, inc_a, inc_b, ACID_PERC_MODAL); break;
        default: alive = sub_block(v, &c, l, r, i0, i1, inc_a, inc_b, ACID_PERC_NOISE); break;
        }
        if (!alive) {
            return;
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
