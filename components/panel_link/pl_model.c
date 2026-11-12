#include "pl_model.h"

#include <string.h>

#include "pl_labels.h"
#include "pl_mix.h"

#define SRC_BSP 0

void pl_model_init(pl_model_t *m)
{
    memset(m, 0, sizeof *m);
    memset(m->bsp_pos, 0xFF, sizeof m->bsp_pos);
    m->goto_page = -1;
    memset(m->user_t, PL_NONE, sizeof m->user_t);
    m->scene.cur = PL_NONE;
    m->grids_step = -1;
    memset(m->mod_t, PL_NONE, sizeof m->mod_t);
}

static int is_user(int page)
{
    return page >= PL_USER_FIRST && page < PL_USER_FIRST + PL_USER_PAGES;
}

static int is_mod(int page)
{
    return page >= PL_PAGE_MOD1 && page < PL_PAGE_MOD1 + PL_MOD_PAGES;
}

static void apply_mod(pl_model_t *m, const pl_mod_t *f)
{
    int k = f->mod;
    memcpy(m->mod_knob[k], f->knob, PL_KNOBS);
    memcpy(m->mod_t[k], f->t, sizeof f->t);
    for (int e = 0; e < PL_KNOBS; e++) {
        m->mod_held[k][e] = e > 0 ? (f->held[(e - 1) / 8] >> ((e - 1) % 8)) & 1 : 0;
    }
    m->mod_have |= 1 << k;
}

static void apply_custom(pl_model_t *m, const pl_custom_t *c)
{
    if (c->up >= PL_USER_PAGES) {
        return;
    }
    for (int e = 1; e < PL_KNOBS; e++) {
        memcpy(m->user_t[c->up][e], c->t[e - 1], sizeof c->t[e - 1]);
        m->user_val[c->up][e] = c->val[e - 1];
        m->user_held[c->up][e] = (c->held[(e - 1) / 8] >> ((e - 1) % 8)) & 1;
    }
    m->user_have |= 1 << c->up;
}

static void apply_snapshot(pl_model_t *m, const pl_snapshot_t *s)
{
    m->have = 1;
    m->mode = s->mode;
    m->demo = s->demo;
    m->bsp_page = s->bsp_page;
    memcpy(m->raw, s->raw, sizeof m->raw);
}

static void apply_snapshot2(pl_model_t *m, const pl_snapshot2_t *s)
{
    memcpy(m->bsp_pos, s->bsp_pos, sizeof m->bsp_pos);
    for (int i = 0; i < PL_PAGES * PL_KNOBS; i++) {
        m->held[i / PL_KNOBS][i % PL_KNOBS] = (s->held[i / 8] >> (i % 8)) & 1;
    }
    for (int e = 1; e <= PL_REVERB_KNOBS; e++) {
        m->reverb[e] = s->reverb[e - 1];
    }
    memset(m->reverb_held, 0, sizeof m->reverb_held);
    for (int e = 1; e <= PL_PITCH_KNOBS; e++) {
        m->pitch[e] = s->pitch[e - 1];
    }
    memset(m->pitch_held, 0, sizeof m->pitch_held);
}

// Чья позиция BSP в событии: на открытой у синта вкладке MIX энкодер BSP - слот, событие несёт его цель.
static int pos_enc(const pl_model_t *m, const pl_knob_t *k)
{
    if (k->src != SRC_BSP || !pl_mix_is(m->bsp_page)) {
        return k->enc;
    }
    int t = m->bsp_page == PL_PAGE_MIX_SYN1 + 1; // у ударки типы не важны
    for (int e = 1; e < PL_KNOBS; e++) {
        int tp, te;
        if (pl_mix_target(m->bsp_page, e, m->raw[PL_PAGE_SYNTH1 + t][1], m->raw[PL_PAGE_LAYER1 + t][1], &tp, &te) &&
            tp == k->page && te == k->enc) {
            return e;
        }
    }
    return k->enc;
}

static void apply_knob(pl_model_t *m, const pl_knob_t *k, uint32_t ms)
{
    if (k->enc == 0) {
        m->bsp_page = k->value;
        if (k->src == SRC_BSP && (k->value < PL_PAGES || is_user(k->value) || is_mod(k->value))) {
            m->goto_page = k->value; // листают на BSP - экран за ним; демо и своё эхо - нет
        }
        return;
    }
    if (is_user(k->page) && k->enc < PL_KNOBS) {
        int up = k->page - PL_USER_FIRST;
        m->user_val[up][k->enc] = k->value;
        m->bsp_pos[k->enc] = k->bsp_pos;
        m->user_held[up][k->enc] = k->held;
        if (k->src == SRC_BSP && m->user_t[up][k->enc][0].pe != PL_NONE) { // пустой слот не всплывает
            m->toast_page = k->page;
            m->toast_enc = k->enc;
            m->toast_until_ms = ms + PL_TOAST_MS;
        }
        return;
    }
    if (k->page == PL_PAGE_REVERB) {
        if (k->enc <= PL_REVERB_KNOBS) {
            m->reverb[k->enc] = k->value;
            m->reverb_held[k->enc] = k->held;
        }
        return;
    }
    if (k->page == PL_PAGE_PITCH) {
        if (k->enc <= PL_PITCH_KNOBS) {
            m->pitch[k->enc] = k->value;
            m->pitch_held[k->enc] = k->held;
        }
        return;
    }
    if (is_mod(k->page) && k->enc < PL_KNOBS) {
        int mk = k->page - PL_PAGE_MOD1;
        m->mod_knob[mk][k->enc] = k->value;
        m->bsp_pos[k->enc] = k->bsp_pos;
        m->mod_held[mk][k->enc] = k->held;
        if (k->src == SRC_BSP && pl_knob_label(k->page, k->enc, m->mod_knob[mk])) { // пустой энкодер не всплывает
            m->toast_page = k->page;
            m->toast_enc = k->enc;
            m->toast_until_ms = ms + PL_TOAST_MS;
        }
        return;
    }
    if (k->page >= PL_PAGES || k->enc >= PL_KNOBS) {
        return;
    }
    m->raw[k->page][k->enc] = k->value;
    m->bsp_pos[pos_enc(m, k)] = k->bsp_pos;
    m->held[k->page][k->enc] = k->held;
    if (k->src == SRC_BSP && pl_knob_label(k->page, k->enc, m->raw[k->page])) { // пустой энкодер не всплывает
        m->toast_page = k->page;
        m->toast_enc = k->enc;
        m->toast_until_ms = ms + PL_TOAST_MS;
    }
}

void pl_model_apply(pl_model_t *m, uint8_t type, const uint8_t *payload, size_t len, uint32_t ms)
{
    if (type == PL_SNAPSHOT && len == sizeof(pl_snapshot_t)) {
        pl_snapshot_t s;
        memcpy(&s, payload, sizeof s);
        apply_snapshot(m, &s);
    } else if (type == PL_SNAPSHOT2 && len == sizeof(pl_snapshot2_t)) {
        pl_snapshot2_t s;
        memcpy(&s, payload, sizeof s);
        apply_snapshot2(m, &s);
    } else if (type == PL_KNOB && len == sizeof(pl_knob_t)) {
        pl_knob_t k;
        memcpy(&k, payload, sizeof k);
        apply_knob(m, &k, ms);
    } else if (type == PL_CUSTOM && len == sizeof(pl_custom_t)) {
        pl_custom_t c;
        memcpy(&c, payload, sizeof c);
        apply_custom(m, &c);
    } else if (type == PL_MOD && len == sizeof(pl_mod_t) && payload[0] < PL_MOD_PAGES) {
        pl_mod_t f;
        memcpy(&f, payload, sizeof f);
        apply_mod(m, &f);
    } else if (type == PL_MOD_POS && len == PL_MOD_POS_LEN) {
        memcpy(m->mod_out, payload, PL_MOD_PAGES);
        memcpy(m->mod_phase, payload + PL_MOD_PAGES, PL_MOD_PAGES);
        m->mod_pos_ms = ms;
    } else if (type == PL_SCENE_STATE && len == sizeof(pl_scene_state_t)) {
        memcpy(&m->scene, payload, sizeof m->scene);
    } else if (type == PL_LOAD && len == sizeof(pl_load_t)) {
        memcpy(&m->load, payload, sizeof m->load);
        m->load_ms = ms;
        m->have_load = 1;
    } else if (type == PL_GRIDS_STEP && len == 1) {
        m->grids_step = payload[0] < 32 ? payload[0] : -1;
    } else if (type == PL_GRIDS_PAT && len == sizeof(pl_grids_pat_frame_t)) {
        pl_grids_pat_frame_t fr;
        memcpy(&fr, payload, sizeof fr);
        if (pl_grids_user_set(m->gpat, fr.slot, &fr.pat)) {
            m->gpat_have |= 1 << fr.slot;
        }
    } else if (type == PL_SEQ_DUMP && len == sizeof(pl_seq_dump_t) && payload[0] < PL_SEQ_SYNTHS) {
        memcpy(&m->seq[payload[0]], payload, len);
        m->seq_have |= 1 << payload[0];
    } else if (type == PL_SEQ_POS && len == sizeof(pl_seq_pos_t) && payload[0] < PL_SEQ_SYNTHS) {
        pl_seq_pos_t *p = &m->seq_pos[payload[0]];
        memcpy(p, payload, len);
        m->seq[payload[0]].run = p->run; // Stop BSP останавливает и секвенсер: кнопка ▶ видит
    } else {
        return;
    }
    m->last_rx_ms = ms;
}

int pl_model_need_hello(const pl_model_t *m, uint32_t ms)
{
    if (m->hello_sent && ms - m->last_hello_ms < PL_HELLO_MS) {
        return 0;
    }
    return !m->have || ms - m->last_rx_ms > PL_QUIET_MS;
}

void pl_model_hello_sent(pl_model_t *m, uint32_t ms)
{
    m->hello_sent = 1;
    m->last_hello_ms = ms;
}

int pl_model_local_set(pl_model_t *m, int page, int enc, int v)
{
    if (page == PL_PAGE_REVERB || page == PL_PAGE_PITCH) {
        int pitch = page == PL_PAGE_PITCH;
        if (!m->have || enc <= 0 || enc > (pitch ? PL_PITCH_KNOBS : PL_REVERB_KNOBS)) {
            return 0;
        }
        (pitch ? m->pitch : m->reverb)[enc] = (uint8_t)v;
        (pitch ? m->pitch_held : m->reverb_held)[enc] = 1;
        return 1;
    }
    if (m->have && is_user(page) && enc > 0 && enc < PL_KNOBS && (m->user_have >> (page - PL_USER_FIRST) & 1)) {
        const pl_target_t *t = m->user_t[page - PL_USER_FIRST][enc];
        m->user_val[page - PL_USER_FIRST][enc] = (uint8_t)v;
        m->user_held[page - PL_USER_FIRST][enc] = 1;
        int alias = t[0].pe != PL_NONE && (t[1].pe == PL_NONE) && t[0].lo == 0 && t[0].hi == 127;
        if (alias && (t[0].pe >> 4) < PL_PAGES) {
            m->raw[t[0].pe >> 4][t[0].pe & 15] = (uint8_t)v; // псевдоним показывает цель: двигаем её сразу
        }
        return 1;
    }
    if (m->have && is_mod(page) && enc > 0 && enc < PL_KNOBS && (m->mod_have >> (page - PL_PAGE_MOD1) & 1)) {
        m->mod_knob[page - PL_PAGE_MOD1][enc] = (uint8_t)v;
        m->mod_held[page - PL_PAGE_MOD1][enc] = 1;
        return 1;
    }
    if (!m->have || page < 0 || page >= PL_PAGES || enc <= 0 || enc >= PL_KNOBS) {
        return 0;
    }
    m->raw[page][enc] = (uint8_t)v;
    m->held[page][enc] = 1;
    return 1;
}

void pl_model_mod_expire(pl_model_t *m, uint32_t ms)
{
    // Знаковая разность, как в pl_model_linked: время опроса бывает раньше последнего кадра.
    if ((int32_t)(ms - m->mod_pos_ms) > PL_MOD_STALE_MS) {
        memset(m->mod_out, 0, sizeof m->mod_out);
    }
}

const uint8_t *pl_model_raw(const pl_model_t *m, int page)
{
    if (page == PL_PAGE_REVERB) {
        return m->reverb;
    }
    if (page == PL_PAGE_PITCH) {
        return m->pitch;
    }
    if (page >= 0 && page < PL_PAGES) {
        return m->raw[page];
    }
    return is_mod(page) ? m->mod_knob[page - PL_PAGE_MOD1] : NULL;
}

int pl_model_linked(const pl_model_t *m, uint32_t ms)
{
    // Знаковая разность: время отрисовки бывает раньше последнего кадра (отстаёт на перерисовку), это не потеря.
    return m->have && (int32_t)(ms - m->last_rx_ms) <= PL_LOST_MS;
}

int pl_model_page_resend(const pl_model_t *m, int screen_page, uint32_t ms)
{
    return m->have && m->mode == 1 && m->goto_page < 0 && m->bsp_page != screen_page &&
           (!m->page_sent || ms - m->page_sent_ms >= PL_PAGE_MS);
}

void pl_model_page_sent(pl_model_t *m, uint32_t ms)
{
    m->page_sent = 1;
    m->page_sent_ms = ms;
    m->goto_page = -1; // страницу выбрали на экране: это новее листания на BSP
}

static int seq_ok(const pl_model_t *m, int synth)
{
    return synth >= 0 && synth < PL_SEQ_SYNTHS && (m->seq_have >> synth & 1);
}

int pl_model_seq_step(pl_model_t *m, int synth, int pat, int step, uint8_t v)
{
    if (!seq_ok(m, synth) || pat < 0 || pat >= PL_SEQ_PATS || step < 0 || step >= PL_SEQ_STEPS) {
        return 0;
    }
    m->seq[synth].pat[pat][step] = v;
    return 1;
}

int pl_model_seq_oct(pl_model_t *m, int synth, int pat, int oct)
{
    if (!seq_ok(m, synth) || pat < 0 || pat >= PL_SEQ_PATS || oct < -2 || oct > 2) {
        return 0;
    }
    m->seq[synth].oct[pat] = (uint8_t)(oct + 2);
    return 1;
}

int pl_model_seq_scale(pl_model_t *m, int synth, int pat, int scale)
{
    if (!seq_ok(m, synth) || pat < 0 || pat >= PL_SEQ_PATS || scale < 0 || scale >= PL_SEQ_SCALES) {
        return 0;
    }
    m->seq[synth].scale[pat] = (uint8_t)scale;
    return 1;
}

int pl_model_seq_row(pl_model_t *m, int synth, int row, const uint8_t cell[PL_SEQ_CELLS], int rep)
{
    if (!seq_ok(m, synth) || row < 0 || row >= PL_SEQ_ROWS || rep < 1 || rep > 16) {
        return 0;
    }
    memcpy(m->seq[synth].cell[row], cell, PL_SEQ_CELLS);
    m->seq[synth].rep[row] = (uint8_t)rep;
    return 1;
}

int pl_model_seq_ctrl(pl_model_t *m, int synth, int run, int edit)
{
    if (!seq_ok(m, synth) || edit < 0 || edit >= PL_SEQ_PATS) {
        return 0;
    }
    if (run != PL_SEQ_KEEP) {
        m->seq[synth].run = run ? 1 : 0;
    }
    m->seq[synth].edit = (uint8_t)edit;
    return 1;
}

int pl_model_seq_bpm(pl_model_t *m, int bpm)
{
    if (!m->seq_have || bpm < 60 || bpm > 200) {
        return 0;
    }
    for (int k = 0; k < PL_SEQ_SYNTHS; k++) {
        m->seq[k].bpm = (uint8_t)bpm;
    }
    return 1;
}

static int gpat_ok(const pl_model_t *m, int slot)
{
    return slot >= 0 && slot < PL_GRIDS_USER && (m->gpat_have >> slot & 1);
}

int pl_model_grids_cell(pl_model_t *m, int slot, int part, int step, int lv)
{
    return gpat_ok(m, slot) && pl_grids_user_cell(m->gpat, slot, part, step, lv);
}

int pl_model_grids_pat(pl_model_t *m, int slot, const pl_grids_pat_t *pat)
{
    return gpat_ok(m, slot) && pl_grids_user_set(m->gpat, slot, pat);
}
