#include "acid_mod.h"

#include <string.h>

#include "acid_math.h"
#include "acid_synth.h"

static const uint8_t MAGIC[4] = {'M', 'D', '1', 1};
static const uint16_t SYNC_TICKS[ACID_MOD_SYNCS] = {0, 6, 12, 24, 48, 96, 192, 384, 768};

// Ручка 0..127 в секунды: 1 мс .. 10 с, экспонента.
static float knob_time(int v)
{
    return 0.001f * acid_exp2((float)v * (13.288f / 127.0f));
}

// Коэффициенты по ручкам: раз на правку, не на шаг.
static void recalc(acid_mod_t *m, acid_mod_one_t *o)
{
    float hz = 0.05f * acid_exp2((float)o->knob[ACID_MOD_RATE] * (8.644f / 127.0f)); // 0.05 .. 20 Гц
    o->inc = hz * m->dt;
    float a = m->dt * acid_rcp(knob_time(o->knob[ACID_MOD_ATTACK]));
    o->att_inc = a > 1.0f ? 1.0f : a;
    // Спад до 1% за время ручки.
    o->dec_k = acid_exp2(-6.644f * m->dt * acid_rcp(knob_time(o->knob[ACID_MOD_DECAY])));
}

static int knob_ok(int enc, int v)
{
    if (enc < ACID_MOD_SHAPE || enc >= ACID_MOD_DEPTH1 + ACID_MOD_TARGETS || v < 0 || v > 127) {
        return 0;
    }
    int n = enc == ACID_MOD_SHAPE ? ACID_SHAPES : enc == ACID_MOD_SYNC ? ACID_MOD_SYNCS
                                              : enc == ACID_MOD_TRIG ? ACID_MOD_TRIGS : 128;
    return v < n;
}

void acid_mod_init(acid_mod_t *m, float dt)
{
    memset(m, 0, sizeof *m);
    m->dt = dt;
    m->tick_s = 1.0f / 48.0f; // 120 уд/мин, пока клок не показал свой темп
    m->rnd = 0x2545F491u;
    for (int k = 0; k < ACID_MODS; k++) {
        acid_mod_one_t *o = &m->m[k];
        o->knob[ACID_MOD_RATE] = 64;
        o->knob[ACID_MOD_PROB] = 127;
        o->knob[ACID_MOD_DECAY] = 64;
        o->knob[ACID_MOD_AMOUNT] = 127;
        for (int i = 0; i < ACID_MOD_TARGETS; i++) {
            o->knob[ACID_MOD_DEPTH1 + i] = 64;
            o->t[i].page = ACID_MOD_NONE;
        }
        recalc(m, o);
    }
}

int acid_mod_set(acid_mod_t *m, int k, int enc, int v)
{
    if (k < 0 || k >= ACID_MODS || !knob_ok(enc, v)) {
        return 0;
    }
    acid_mod_one_t *o = &m->m[k];
    if (o->knob[enc] != v) {
        o->knob[enc] = (uint8_t)v;
        m->save = 1;
        recalc(m, o);
    }
    return 1;
}

int acid_mod_knob(const acid_mod_t *m, int k, int enc)
{
    return k >= 0 && k < ACID_MODS && enc >= 0 && enc < ACID_MOD_KNOBS ? m->m[k].knob[enc] : 0;
}

int acid_mod_page_ok(int page, int enc)
{
    return enc >= 1 && enc <= 15 && ((page >= 0 && page <= 8) || (page >= 10 && page <= 12));
}

int acid_mod_target(acid_mod_t *m, int k, int idx, int page, int enc)
{
    if (k < 0 || k >= ACID_MODS || idx < 0 || idx >= ACID_MOD_TARGETS) {
        return 0;
    }
    if (page == ACID_MOD_NONE) {
        enc = 0;
    } else if (!acid_mod_page_ok(page, enc)) {
        return 0;
    }
    acid_mod_target_t *t = &m->m[k].t[idx];
    if (t->page != page || t->enc != enc) {
        t->page = (uint8_t)page;
        t->enc = (uint8_t)enc;
        m->save = 1;
    }
    return 1;
}

static uint32_t rnd(acid_mod_t *m)
{
    m->rnd = m->rnd * 1664525u + 1013904223u;
    return m->rnd >> 8;
}

static float rnd_bi(acid_mod_t *m) // -1 .. 1
{
    return (float)(int32_t)(rnd(m) & 0xFFFFFF) * (1.0f / 8388608.0f) - 1.0f;
}

static float shape_out(int shape, float p)
{
    switch (shape) {
    case ACID_SHAPE_SIN:
        return acid_sin2pi(p);
    case ACID_SHAPE_TRI:
        return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
    case ACID_SHAPE_SAW:
        return 1.0f - 2.0f * p;
    case ACID_SHAPE_RAMP:
        return 2.0f * p - 1.0f;
    default:
        return p < 0.5f ? 1.0f : -1.0f;
    }
}

static void run_one(acid_mod_t *m, acid_mod_one_t *o, unsigned trig, uint32_t pos)
{
    int shape = o->knob[ACID_MOD_SHAPE], src = o->knob[ACID_MOD_TRIG], sync = o->knob[ACID_MOD_SYNC];
    int fired = src && (trig & (1u << (src - 1))) && (int)(rnd(m) % 127u) < o->knob[ACID_MOD_PROB];
    if (shape == ACID_SHAPE_ENV) {
        if (fired) {
            o->stage = 1; // атака от текущего уровня
        }
        if (o->stage == 1) {
            o->out += o->att_inc;
            if (o->out >= 1.0f) {
                o->out = 1.0f;
                o->stage = 2;
            }
        } else if (o->stage == 2) {
            o->out *= o->dec_k;
            if (o->out < 0.001f) {
                o->out = 0.0f;
                o->stage = 0;
            }
        } else {
            o->out = 0.0f;
        }
        return;
    }
    o->stage = 0;
    float was = o->phase;
    int wrap = 0;
    if (fired) {
        o->phase = 0.0f;
    } else if (sync && !src) {
        // Привязка к клоку: место в периоде по тикам плюс доля тика по времени.
        float frac = m->since_tick * acid_rcp(m->tick_s);
        frac = frac > 1.0f ? 1.0f : frac;
        o->phase = ((float)(pos % SYNC_TICKS[sync]) + frac) * (1.0f / (float)SYNC_TICKS[sync]);
        o->phase = o->phase >= 1.0f ? 0.999999f : o->phase;
        wrap = o->phase < was;
    } else {
        o->phase += sync ? m->dt * acid_rcp((float)SYNC_TICKS[sync] * m->tick_s) : o->inc;
        if (o->phase >= 1.0f) {
            o->phase -= 1.0f;
            wrap = 1;
        }
    }
    int fresh = src ? fired : wrap; // новое случайное значение: на запуск, а при FREE - раз в период
    if (shape == ACID_SHAPE_SH) {
        if (fresh) {
            o->held = rnd_bi(m);
        }
        o->out = o->held;
    } else if (shape == ACID_SHAPE_DRIFT) {
        if (fresh) {
            o->goal = rnd_bi(m);
        }
        float k = o->inc * 4.0f;
        o->out += (o->goal - o->out) * (k > 1.0f ? 1.0f : k);
    } else {
        o->out = shape_out(shape, o->phase);
    }
}

void acid_mod_run(acid_mod_t *m, unsigned trig, uint32_t pos)
{
    if (pos != m->pos) {
        uint32_t d = pos - m->pos;
        if (d >= 1 && d <= 8 && m->since_tick > 0.0f) {
            float t = m->since_tick * acid_rcp((float)d);
            m->tick_s = t < 0.002f ? 0.002f : t > 0.5f ? 0.5f : t;
        }
        m->pos = pos;
        m->since_tick = 0.0f;
    }
    for (int k = 0; k < ACID_MODS; k++) {
        run_one(m, &m->m[k], trig, pos);
    }
    m->since_tick += m->dt;
}

float acid_mod_out(const acid_mod_t *m, int k)
{
    return m->m[k].out;
}

int acid_mod_offset(const acid_mod_t *m, int k, int idx)
{
    const acid_mod_one_t *o = &m->m[k];
    if (o->t[idx].page == ACID_MOD_NONE) {
        return 0;
    }
    float x = o->out * (float)o->knob[ACID_MOD_AMOUNT] * (2.0f / 127.0f) * (float)(o->knob[ACID_MOD_DEPTH1 + idx] - 64);
    return (int)(x + (x < 0.0f ? -0.5f : 0.5f));
}

int acid_mod_sum(const acid_mod_t *m, int page, int enc, int *n)
{
    int sum = 0, cnt = 0;
    for (int k = 0; k < ACID_MODS; k++) {
        for (int i = 0; i < ACID_MOD_TARGETS; i++) {
            if (m->m[k].t[i].page == page && m->m[k].t[i].enc == enc) {
                sum += acid_mod_offset(m, k, i);
                cnt++;
            }
        }
    }
    if (n) {
        *n = cnt;
    }
    return sum;
}

int acid_mod_take_save(acid_mod_t *m)
{
    int s = m->save;
    m->save = 0;
    return s;
}

void acid_mod_export(const acid_mod_t *m, void *buf)
{
    uint8_t *b = buf;
    memcpy(b, MAGIC, sizeof MAGIC);
    b += sizeof MAGIC;
    for (int k = 0; k < ACID_MODS; k++) {
        memcpy(b, m->m[k].knob, ACID_MOD_KNOBS);
        b += ACID_MOD_KNOBS;
        for (int i = 0; i < ACID_MOD_TARGETS; i++) {
            *b++ = m->m[k].t[i].page;
            *b++ = m->m[k].t[i].enc;
        }
    }
}

int acid_mod_import(acid_mod_t *m, const void *buf, size_t len)
{
    const uint8_t *b = buf;
    if (len != ACID_MOD_BLOB || memcmp(b, MAGIC, sizeof MAGIC) != 0) {
        return 0;
    }
    const uint8_t *p = b + sizeof MAGIC;
    for (int k = 0; k < ACID_MODS; k++, p += ACID_MOD_KNOBS + 2 * ACID_MOD_TARGETS) {
        for (int e = ACID_MOD_SHAPE; e < ACID_MOD_DEPTH1 + ACID_MOD_TARGETS; e++) {
            if (!knob_ok(e, p[e])) {
                return 0;
            }
        }
        for (int i = 0; i < ACID_MOD_TARGETS; i++) {
            const uint8_t *t = p + ACID_MOD_KNOBS + 2 * i;
            if (t[0] != ACID_MOD_NONE && !acid_mod_page_ok(t[0], t[1])) {
                return 0;
            }
        }
    }
    p = b + sizeof MAGIC;
    for (int k = 0; k < ACID_MODS; k++) {
        acid_mod_one_t *o = &m->m[k];
        for (int e = ACID_MOD_SHAPE; e < ACID_MOD_DEPTH1 + ACID_MOD_TARGETS; e++) {
            o->knob[e] = p[e];
        }
        p += ACID_MOD_KNOBS;
        for (int i = 0; i < ACID_MOD_TARGETS; i++, p += 2) {
            o->t[i].page = p[0];
            o->t[i].enc = p[0] == ACID_MOD_NONE ? 0 : p[1];
        }
        recalc(m, o);
    }
    return 1;
}

int acid_mod_step(acid_mod_t *m, acid_synth_t *s, int budget)
{
    if (s->tight >= 1) {
        return 0; // защита: фазы и последние действующие значения замирают, базу не снимаем
    }
    acid_mod_run(m, acid_synth_take_trig(s), acid_synth_clock_pos(s));
    if (budget <= 0) {
        return 0; // без бюджета не обходим цели и не тратим применение на снятие оверлея
    }
    uint16_t want[ACID_PAGES] = {0};
    for (int k = 0; k < ACID_MODS; k++) {
        if (!m->m[k].knob[ACID_MOD_AMOUNT]) {
            continue;
        }
        for (int i = 0; i < ACID_MOD_TARGETS; i++) {
            int p = m->m[k].t[i].page, e = m->m[k].t[i].enc;
            if (p != ACID_MOD_NONE && m->m[k].knob[ACID_MOD_DEPTH1 + i] != 64 && acid_synth_knob_smooth(s, p, e)) {
                want[p] |= (uint16_t)(1u << e);
            }
        }
    }
    for (int p = 0; p < ACID_PAGES; p++) {
        uint16_t gone = s->mod_mask[p] & (uint16_t)~want[p];
        for (int e = 1; gone && e < ACID_KNOBS; e++) {
            if (gone & (1u << e)) {
                acid_synth_mod_release(s, p, e);
            }
        }
    }
    int lim = budget < ACID_MOD_PER_CHUNK ? budget : ACID_MOD_PER_CHUNK;
    if (lim <= 0) {
        return 0;
    }
    uint16_t seen[ACID_PAGES] = {0};
    int applied = 0, n = ACID_MODS * ACID_MOD_TARGETS;
    for (int step = 0; step < n; step++) {
        int slot = (m->next + step) % n;
        int k = slot / ACID_MOD_TARGETS, i = slot % ACID_MOD_TARGETS;
        int p = m->m[k].t[i].page, e = m->m[k].t[i].enc;
        if (p == ACID_MOD_NONE || !(want[p] & (1u << e)) || (seen[p] & (1u << e))) {
            continue;
        }
        seen[p] |= (uint16_t)(1u << e);
        int eff = acid_synth_knob(s, p, e) + acid_mod_sum(m, p, e, NULL);
        if (acid_synth_mod_apply(s, p, e, eff)) {
            if (++applied >= lim) {
                m->next = (slot + 1) % n;
                break;
            }
        }
    }
    return applied;
}

int acid_mod_chunk(acid_mod_t *m, acid_synth_t *s, int apply, int budget)
{
    if (apply) {
        return acid_mod_step(m, s, budget);
    }
    if (s->tight >= 1) {
        return 0; // защита: как в acid_mod_step, иначе фазы шли бы на кусках без применения
    }
    acid_mod_run(m, acid_synth_take_trig(s), acid_synth_clock_pos(s));
    return 0;
}
