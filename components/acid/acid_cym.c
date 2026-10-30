#include "acid_cym.h"

#include <math.h>
#include <string.h>

#include "acid_fx.h"

#define RETRIG_S 0.05f // перебитый удар той же ноты уходит
#define STEAL_S 0.005f // забранный голос уходит
#define TAIL_MIN_S 0.05f
#define TAIL_MAX_S 2.0f
#define NOTE0 49 // крэш 49, райд 51, купол 53

void acid_cym_init(acid_cym_t *c, float sr)
{
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
    v->cur[0] = (float)acid_adpcm_step(&v->dec[0], b & 15) * (1.0f / 32768.0f);
    v->cur[1] = (float)acid_adpcm_step(&v->dec[1], b >> 4) * (1.0f / 32768.0f);
    v->pos++;
    return 1;
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

    // Прежний удар той же ноты уходит; при 6 звучащих уходит самый тихий.
    int sounding = 0;
    acid_cym_voice_t *quiet = NULL, *free_v = NULL, *faded = NULL;
    for (int i = 0; i < ACID_CYM_SLOTS; i++) {
        acid_cym_voice_t *v = &c->v[i];
        if (v->rr == NULL) {
            free_v = free_v ? free_v : v;
            continue;
        }
        if (v->which == which) {
            fade_out(v, RETRIG_S, c->sr);
        }
        if (v->fade_step == 0.0f) {
            sounding++;
            quiet = !quiet || level(v) < level(quiet) ? v : quiet;
        } else {
            faded = !faded || level(v) < level(faded) ? v : faded;
        }
    }
    if (sounding >= ACID_CYM_VOICES && quiet) {
        fade_out(quiet, STEAL_S, c->sr);
        faded = !faded || level(quiet) < level(faded) ? quiet : faded;
    }
    acid_cym_voice_t *v = free_v ? free_v : faded; // слоты кончились: обрываем самый тихий из уходящих
    if (v == NULL) {
        return;
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
}

void acid_cym_next(acid_cym_t *c, float *l, float *r)
{
    float sl = 0.0f, sr = 0.0f;
    for (int i = 0; i < ACID_CYM_SLOTS; i++) {
        acid_cym_voice_t *v = &c->v[i];
        if (v->rr == NULL) {
            continue;
        }
        int w = v->which;
        float g = v->amp * v->env * v->fade * c->gain[w];
        float x0 = v->prev[0] + (v->cur[0] - v->prev[0]) * v->phase;
        float x1 = v->prev[1] + (v->cur[1] - v->prev[1]) * v->phase;
        sl += g * c->pan_l[w] * x0;
        sr += g * c->pan_r[w] * x1;
        v->env *= c->tail_coef[w];
        if (v->fade_step != 0.0f) {
            v->fade -= v->fade_step;
            if (v->fade <= 0.0f) {
                v->rr = NULL;
                continue;
            }
        }
        v->phase += c->rate[w];
        while (v->phase >= 1.0f) {
            v->phase -= 1.0f;
            v->prev[0] = v->cur[0];
            v->prev[1] = v->cur[1];
            if (!decode(v)) {
                v->rr = NULL;
                break;
            }
        }
        if (v->rr && v->env < 1e-4f) {
            v->rr = NULL; // хвост ручкой заглушён
        }
    }
    *l = sl;
    *r = sr;
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
