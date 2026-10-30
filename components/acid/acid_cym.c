#include "acid_cym.h"

#include <math.h>
#include <string.h>

#include "acid_fx.h"

#define RETRIG_S 0.7f // перебитый удар той же ноты уходит медленно: звон копится, как у живой тарелки
#define STEAL_S 0.005f // забранный голос уходит
#define TAIL_MIN_S 0.05f
#define TAIL_MAX_S 2.0f
#define NOTE0 49 // крэш 49, райд 51, купол 53

void acid_cym_init(acid_cym_t *c, float sr)
{
    acid_adpcm_init_lut();
    memset(c, 0, sizeof *c);
    c->sr = sr;
    c->sens = 100.0f / 127.0f;
    for (int w = 0; w < ACID_CYM_N; w++) {
        c->gain[w] = 1.0f;
        c->tail_coef[w] = 1.0f;
        c->rate[w] = 1.0f;
        c->pan_l[w] = c->pan_r[w] = 1.0f;
    }
}

static void set_one(acid_cym_t *c, int w, int tail_knob, int tune_knob, int pan_knob)
{
    // Хвост: 127 - как в записи, ниже - экспонента поверх, на 0 постоянная 0.05 с (за ~0.15 с почти тишина).
    if (tail_knob >= 127) {
        c->tail_coef[w] = 1.0f;
    } else {
        float tau = TAIL_MIN_S * exp2f((float)tail_knob / 126.0f * log2f(TAIL_MAX_S / TAIL_MIN_S));
        c->tail_coef[w] = expf(-1.0f / (tau * c->sr));
    }
    float semis = (float)(tune_knob - 64) / 63.0f * 12.0f;
    semis = semis < -12.0f ? -12.0f : semis;
    c->rate[w] = exp2f(semis / 12.0f);
    acid_pan((float)pan_knob, &c->pan_l[w], &c->pan_r[w]);
}

void acid_cym_set(acid_cym_t *c, int which, float gain, int tail_knob, int tune_knob, int pan_knob)
{
    if (which < 0 || which >= ACID_CYM_N) {
        return;
    }
    c->gain[which] = gain;
    if (which == ACID_CYM_BELL) {
        return; // хвост, строй и панорама купола - от райда
    }
    set_one(c, which, tail_knob, tune_knob, pan_knob);
    if (which == ACID_CYM_RIDE) {
        set_one(c, ACID_CYM_BELL, tail_knob, tune_knob, pan_knob);
    }
}

void acid_cym_set_sens(acid_cym_t *c, float sens)
{
    c->sens = sens;
}

static float level(const acid_cym_voice_t *v)
{
    return v->amp * v->env * v->fade;
}

static void fade_out(acid_cym_voice_t *v, float secs, float sr)
{
    if (v->fade_step == 0.0f) {
        v->fade_step = 1.0f / (secs * sr);
    }
}

// Следующий кадр в cur; 0 - кадры кончились.
static int decode(acid_cym_voice_t *v)
{
    if (v->pos >= v->rr->frames) {
        return 0;
    }
    if (v->pos % ACID_BANK_BLOCK == 0) {
        // Состояние из заголовка блока: ошибка не тянется дальше блока.
        for (int ch = 0; ch < 2; ch++) {
            v->dec[ch].pred = (int16_t)(v->p[0] | v->p[1] << 8);
            v->dec[ch].index = v->p[2] > 88 ? 88 : v->p[2];
            v->p += 4;
        }
    }
    uint8_t b = *v->p++;
    v->cur[0] = (float)acid_adpcm_step(&v->dec[0], b & 15); // в единицах int16, масштаб - в усилении
    v->cur[1] = (float)acid_adpcm_step(&v->dec[1], b >> 4);
    v->pos++;
    return 1;
}

static int stealing(const acid_cym_voice_t *v, float sr)
{
    return v->fade_step >= 0.99f / (STEAL_S * sr);
}

// После удара держим ACID_CYM_RESERVE слотов свободными или забираемыми (уход за 5 мс): следующие удары,
// даже три разом, встают в свободный слот, и ничего слышного не обрывается.
static void reserve(acid_cym_t *c, const acid_cym_voice_t *fresh)
{
    for (;;) {
        int spare = 0;
        acid_cym_voice_t *quiet = NULL;
        for (int i = 0; i < ACID_CYM_SLOTS; i++) {
            acid_cym_voice_t *v = &c->v[i];
            if (v->rr == NULL || stealing(v, c->sr)) {
                spare++;
            } else if (v != fresh && (!quiet || level(v) < level(quiet))) {
                quiet = v;
            }
        }
        if (spare >= ACID_CYM_RESERVE || quiet == NULL) {
            return;
        }
        quiet->fade_step = 1.0f / (STEAL_S * c->sr); // и уходящий медленно - быстрее
    }
}

void acid_cym_trigger(acid_cym_t *c, const acid_bank_t *b, int note, float vel)
{
    int which = (note - NOTE0) / 2;
    if (b == NULL || note < NOTE0 || (note - NOTE0) % 2 != 0 || which >= ACID_CYM_N) {
        return;
    }
    const acid_bank_note_t *n = acid_bank_note(b, note);
    if (n == NULL) {
        return;
    }
    float e = 1.0f - c->sens * (1.0f - vel);
    int li = 0;
    while (li + 1 < n->nlayers && (float)b->layers[n->first_layer + li].vel_hi < e * 127.0f) {
        li++;
    }
    const acid_bank_layer_t *layer = &b->layers[n->first_layer + li];
    int ci = li < ACID_CYM_LAYERS ? li : ACID_CYM_LAYERS - 1;
    int ri = c->rr_next[which][ci] % layer->nrr;
    c->rr_next[which][ci] = (uint8_t)((ri + 1) % layer->nrr);

    // Прежний удар той же ноты уходит медленно, звенит под новым.
    acid_cym_voice_t *free_v = NULL, *faded = NULL;
    for (int i = 0; i < ACID_CYM_SLOTS; i++) {
        acid_cym_voice_t *v = &c->v[i];
        if (v->rr == NULL) {
            free_v = free_v ? free_v : v;
            continue;
        }
        if (v->which == which) {
            fade_out(v, RETRIG_S, c->sr);
        }
        faded = !faded || level(v) < level(faded) ? v : faded;
    }
    acid_cym_voice_t *v = free_v ? free_v : faded; // слоты кончились: обрываем самый тихий из уходящих
    if (v == NULL) {
        return;
    }
    if (v != free_v && level(v) > 1e-3f) {
        c->hard_cuts++;
    }
    memset(v, 0, sizeof *v);
    v->rr = &b->rrs[layer->first_rr + ri];
    v->p = b->img + v->rr->offset;
    v->which = (uint8_t)which;
    v->layer = (uint8_t)li;
    v->rr_index = (uint8_t)ri;
    v->amp = e * e;
    v->env = 1.0f;
    v->fade = 1.0f;
    decode(v);
    v->prev[0] = v->cur[0];
    v->prev[1] = v->cur[1];
    if (!decode(v)) {
        v->cur[0] = v->cur[1] = 0.0f; // сэмпл в один кадр
    }
    reserve(c, v);
}

// Заголовок блока ADPCM: состояние декодера, ошибка не тянется дальше блока.
static inline const uint8_t *block_hdr(const uint8_t *p, int *pl, int *il, int *pr, int *ir)
{
    *pl = (int16_t)(p[0] | p[1] << 8);
    *il = p[2] > 88 ? 88 : p[2];
    *pr = (int16_t)(p[4] | p[5] << 8);
    *ir = p[6] > 88 ? 88 : p[6];
    return p + ACID_BANK_BLOCK_HDR;
}

// Голос целиком на кусок: состояние в локальных переменных, декодер по таблице (горячий цикл платы).
// Кадры prev/cur в единицах int16, масштаб 1/32768 - в усилении.
static void voice_render(acid_cym_t *c, acid_cym_voice_t *v, float *l, float *r, int n)
{
    int w = v->which;
    float gl = v->amp * c->gain[w] * c->pan_l[w] * (1.0f / 32768.0f);
    float gr = v->amp * c->gain[w] * c->pan_r[w] * (1.0f / 32768.0f);
    float coef = c->tail_coef[w], rate = c->rate[w];
    float env = v->env, fade = v->fade, fstep = v->fade_step, phase = v->phase;
    float p0 = v->prev[0], p1 = v->prev[1], c0 = v->cur[0], c1 = v->cur[1];
    int pl = v->dec[0].pred, il = v->dec[0].index, pr = v->dec[1].pred, ir = v->dec[1].index;
    const uint8_t *p = v->p;
    uint32_t pos = v->pos, frames = v->rr->frames;
    // Уход кончится через столько сэмплов (включая последний звучащий).
    int fade_left = fstep > 0.0f ? (int)ceilf(fade / fstep) : n + 1;
    int alive = 1;
    if (rate == 1.0f && phase == 0.0f) {
        // Строй 64: кадр за кадром без интерполяции (самый частый случай).
        int i = 0;
        while (i < n) {
            if (pos >= frames) {
                // Последний кадр: отдать prev, и голос кончился.
                float g = env * fade;
                l[i] += g * gl * p0;
                r[i] += g * gr * p1;
                alive = 0;
                break;
            }
            if ((pos & (ACID_BANK_BLOCK - 1)) == 0) {
                p = block_hdr(p, &pl, &il, &pr, &ir);
            }
            // Отрезок до конца блока ADPCM, сэмпла, ухода или куска - без проверок внутри.
            uint32_t left = ACID_BANK_BLOCK - (pos & (ACID_BANK_BLOCK - 1));
            left = left < frames - pos ? left : frames - pos;
            int m = n - i < (int)left ? n - i : (int)left;
            m = m < fade_left ? m : fade_left;
            for (int k = 0; k < m; k++, i++) {
                float g = env * fade;
                l[i] += g * gl * p0;
                r[i] += g * gr * p1;
                env *= coef;
                fade -= fstep;
                p0 = c0;
                p1 = c1;
                int b = *p++;
                c0 = (float)acid_adpcm_next(&pl, &il, b & 15);
                c1 = (float)acid_adpcm_next(&pr, &ir, b >> 4);
            }
            pos += (uint32_t)m;
            fade_left -= m;
            if (fade_left <= 0 || env < 1e-4f) {
                alive = 0; // ушёл или хвост ручкой заглушён
                break;
            }
        }
    } else {
        for (int i = 0; i < n; i++) {
            float g = env * fade;
            l[i] += g * gl * (p0 + (c0 - p0) * phase);
            r[i] += g * gr * (p1 + (c1 - p1) * phase);
            env *= coef;
            fade -= fstep;
            if (--fade_left <= 0) {
                alive = 0;
                break;
            }
            phase += rate;
            while (phase >= 1.0f) {
                phase -= 1.0f;
                p0 = c0;
                p1 = c1;
                if (pos >= frames) {
                    alive = 0;
                    break;
                }
                if ((pos & (ACID_BANK_BLOCK - 1)) == 0) {
                    p = block_hdr(p, &pl, &il, &pr, &ir);
                }
                int b = *p++;
                c0 = (float)acid_adpcm_next(&pl, &il, b & 15);
                c1 = (float)acid_adpcm_next(&pr, &ir, b >> 4);
                pos++;
            }
            if (!alive || env < 1e-4f) {
                alive = 0;
                break;
            }
        }
    }
    if (!alive) {
        v->rr = NULL;
        return;
    }
    v->env = env;
    v->fade = fade;
    v->phase = phase;
    v->prev[0] = p0;
    v->prev[1] = p1;
    v->cur[0] = c0;
    v->cur[1] = c1;
    v->dec[0].pred = (int16_t)pl;
    v->dec[0].index = (uint8_t)il;
    v->dec[1].pred = (int16_t)pr;
    v->dec[1].index = (uint8_t)ir;
    v->p = p;
    v->pos = pos;
}

void acid_cym_render(acid_cym_t *c, float *l, float *r, int n)
{
    for (int i = 0; i < n; i++) {
        l[i] = r[i] = 0.0f;
    }
    for (int k = 0; k < ACID_CYM_SLOTS; k++) {
        if (c->v[k].rr != NULL) {
            voice_render(c, &c->v[k], l, r, n);
        }
    }
}

void acid_cym_next(acid_cym_t *c, float *l, float *r)
{
    acid_cym_render(c, l, r, 1);
}

int acid_cym_voices(const acid_cym_t *c)
{
    int n = 0;
    for (int i = 0; i < ACID_CYM_SLOTS; i++) {
        n += c->v[i].rr != NULL;
    }
    return n;
}

int acid_cym_sounding(const acid_cym_t *c)
{
    int n = 0;
    for (int i = 0; i < ACID_CYM_SLOTS; i++) {
        n += c->v[i].rr != NULL && c->v[i].fade_step == 0.0f;
    }
    return n;
}
