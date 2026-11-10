#include "pl_view.h"

#include <stdio.h>

#include "pl_mix.h"

#define USER_COLOR 0xe8dcc0

static int user_count(const pl_model_t *m, int up, int enc)
{
    int n = 0;
    while (n < PL_TARGETS && m->user_t[up][enc][n].pe != PL_NONE) {
        n++;
    }
    return n;
}

int pl_view_is_alias(const pl_model_t *m, int up, int enc)
{
    const pl_target_t *t = &m->user_t[up][enc][0];
    return user_count(m, up, enc) == 1 && t->lo == 0 && t->hi == 127;
}

int pl_view_target(const pl_model_t *m, int page, int enc, int i, int *tenc)
{
    int up = page - PL_USER_FIRST;
    if (up < 0 || up >= PL_USER_PAGES || !(m->user_have >> up & 1) || enc <= 0 || enc >= PL_KNOBS || i < 0 ||
        i >= user_count(m, up, enc)) {
        return -1;
    }
    *tenc = m->user_t[up][enc][i].pe & 15;
    return m->user_t[up][enc][i].pe >> 4;
}

// Откуда цели слота: одна страница - её имя и цвет, несколько - сколько их.
static void user_src(const pl_model_t *m, int up, int enc, pl_cell_t *c)
{
    int n = user_count(m, up, enc), pages = 0;
    for (int i = 0; i < n; i++) {
        int p = m->user_t[up][enc][i].pe >> 4, seen = 0;
        for (int j = 0; j < i; j++) {
            seen |= (m->user_t[up][enc][j].pe >> 4) == p;
        }
        pages += !seen;
    }
    int p0 = m->user_t[up][enc][0].pe >> 4;
    if (pages == 1 && p0 < PL_PAGES) {
        c->src = pl_page_name(p0);
        c->src_color = pl_page_color(p0);
        return;
    }
    snprintf(c->sbuf, sizeof c->sbuf, "%d PAGES", pages);
    c->src = c->sbuf;
    c->src_color = USER_COLOR;
}

int pl_view_mix(const pl_model_t *m, int page, int enc, int *tpage, int *tenc)
{
    int k = page == PL_PAGE_MIX_SYN1 + 1; // у ударки типы не важны
    return pl_mix_target(page, enc, m->raw[PL_PAGE_SYNTH1 + k][1], m->raw[PL_PAGE_LAYER1 + k][1], tpage, tenc);
}

void pl_view_cell(const pl_model_t *m, int page, int enc, pl_cell_t *c)
{
    c->label = NULL;
    c->text = "";
    c->src = NULL;
    c->src_color = 0;
    c->v = c->lo = c->bipolar = c->held = 0;
    c->hi = 127;
    c->skip = -1;
    c->color = 0;
    int mp, me;
    if (pl_view_mix(m, page, enc, &mp, &me)) {
        pl_view_cell(m, mp, me, c); // ячейка цели как есть, под именем - её страница
        if (c->label) {
            c->src = pl_page_name(mp);
            c->src_color = pl_page_color(mp);
        }
        return;
    }
    if (page >= 0 && page < PL_PAGES) {
        const uint8_t *raw = m->raw[page];
        c->label = pl_knob_label(page, enc, raw);
        if (!c->label) {
            return;
        }
        c->v = raw[enc];
        pl_knob_range(page, enc, raw, &c->lo, &c->hi);
        c->skip = pl_knob_skip(page, enc, raw);
        c->bipolar = pl_knob_bipolar(page, enc, raw);
        c->color = pl_knob_color(page, enc, raw);
        c->text = pl_value_text(page, enc, c->v, raw, c->vbuf);
        c->held = m->held[page][enc];
        return;
    }
    int up = page - PL_USER_FIRST;
    if (up < 0 || up >= PL_USER_PAGES || !(m->user_have >> up & 1) || enc <= 0 || enc >= PL_KNOBS) {
        return;
    }
    c->label = pl_slot_label(m->user_t[up][enc], m->raw, c->lbuf);
    if (!c->label) {
        return;
    }
    const pl_target_t *t = &m->user_t[up][enc][0];
    int tp = t->pe >> 4, te = t->pe & 15;
    c->held = m->user_held[up][enc];
    user_src(m, up, enc, c);
    if (pl_view_is_alias(m, up, enc)) {
        const uint8_t *raw = m->raw[tp];
        c->v = raw[te];
        c->bipolar = pl_knob_bipolar(tp, te, raw);
        c->color = pl_knob_color(tp, te, raw);
        c->text = pl_value_text(tp, te, c->v, raw, c->vbuf);
        return;
    }
    c->v = m->user_val[up][enc];
    c->color = tp < PL_PAGES ? pl_knob_color(tp, te, m->raw[tp]) : USER_COLOR;
    snprintf(c->vbuf, sizeof c->vbuf, "%d", c->v);
    c->text = c->vbuf;
}

const char *pl_view_page_name(int page)
{
    if (pl_mix_is(page)) {
        static const char *const MIX[PL_MIX_PAGES] = {"DRUMS MIX", "SYNTH 1 MIX", "SYNTH 2 MIX"};
        return MIX[page - PL_PAGE_MIX_DRUMS];
    }
    return page < PL_PAGES ? pl_page_name(page) : pl_user_name(page - PL_USER_FIRST);
}

uint32_t pl_view_page_color(int page)
{
    if (pl_mix_is(page)) {
        return pl_page_color(page == PL_PAGE_MIX_DRUMS ? 1 : PL_PAGE_SYNTH1 + page - PL_PAGE_MIX_SYN1);
    }
    if (page >= PL_USER_FIRST && page < PL_USER_FIRST + PL_USER_PAGES) {
        return USER_COLOR;
    }
    return pl_page_color(page);
}

int pl_view_page_step(int page, int dir)
{
    if (pl_mix_is(page)) {
        return page; // вкладка инструмента, в круг страниц не входит
    }
    if (page >= PL_USER_FIRST) {
        return PL_USER_FIRST + (page - PL_USER_FIRST + dir + PL_USER_PAGES) % PL_USER_PAGES;
    }
    if (page == PL_PAGE_GRIDS) {
        return page; // у Grids свой экран, в круге страниц его нет
    }
    return (page + dir + PL_PAGE_GRIDS) % PL_PAGE_GRIDS;
}

const char *pl_view_status(const pl_model_t *m, int linked)
{
    static const char *const NAMES[PL_DEMO_SONGS] = {"METAL", "ACID"};
    if (!linked) {
        return "NO LINK";
    }
    if (m->demo == PL_DEMO_DRONE) {
        return "DRONE";
    }
    if (m->demo == PL_DEMO_SEQ && !m->mode) {
        return "SEQ";
    }
    if (m->mode) {
        return "LIVE";
    }
    return m->demo >= 0 && m->demo < PL_DEMO_SONGS ? NAMES[m->demo] : "DEMO";
}

int pl_view_dish(const pl_model_t *m, int page)
{
    int synth = page == PL_PAGE_SYNTH1 || page == PL_PAGE_SYNTH1 + 1;
    return m->have && (page == PL_PAGE_PETRI || (synth && m->raw[page][1] == PL_TYPE_PETRI));
}
