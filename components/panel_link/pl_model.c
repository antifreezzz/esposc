#include "pl_model.h"

#include <string.h>

#include "pl_labels.h"

#define SRC_BSP 0

void pl_model_init(pl_model_t *m)
{
    memset(m, 0, sizeof *m);
    memset(m->bsp_pos, 0xFF, sizeof m->bsp_pos);
    m->goto_page = -1;
    memset(m->user_t, PL_NONE, sizeof m->user_t);
    m->scene.cur = PL_NONE;
}

static int is_user(int page)
{
    return page >= PL_USER_FIRST && page < PL_USER_FIRST + PL_USER_PAGES;
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
    m->bsp_page = s->bsp_page;
    memcpy(m->raw, s->raw, sizeof m->raw);
    memcpy(m->bsp_pos, s->bsp_pos, sizeof m->bsp_pos);
    for (int i = 0; i < PL_PAGES * PL_KNOBS; i++) {
        m->held[i / PL_KNOBS][i % PL_KNOBS] = (s->held[i / 8] >> (i % 8)) & 1;
    }
}

static void apply_knob(pl_model_t *m, const pl_knob_t *k, uint32_t ms)
{
    if (k->enc == 0) {
        m->bsp_page = k->value;
        if (k->src == SRC_BSP && (k->value < PL_PAGES || is_user(k->value))) {
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
    if (k->page >= PL_PAGES || k->enc >= PL_KNOBS) {
        return;
    }
    m->raw[k->page][k->enc] = k->value;
    m->bsp_pos[k->enc] = k->bsp_pos;
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
    } else if (type == PL_KNOB && len == sizeof(pl_knob_t)) {
        pl_knob_t k;
        memcpy(&k, payload, sizeof k);
        apply_knob(m, &k, ms);
    } else if (type == PL_CUSTOM && len == sizeof(pl_custom_t)) {
        pl_custom_t c;
        memcpy(&c, payload, sizeof c);
        apply_custom(m, &c);
    } else if (type == PL_SCENE_STATE && len == sizeof(pl_scene_state_t)) {
        memcpy(&m->scene, payload, sizeof m->scene);
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
    if (!m->have || page < 0 || page >= PL_PAGES || enc <= 0 || enc >= PL_KNOBS) {
        return 0;
    }
    m->raw[page][enc] = (uint8_t)v;
    m->held[page][enc] = 1;
    return 1;
}

int pl_model_linked(const pl_model_t *m, uint32_t ms)
{
    return m->have && ms - m->last_rx_ms <= PL_LOST_MS;
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
