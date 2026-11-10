#include "pl_edit.h"

#include "pl_labels.h"
#include "pl_mod.h"

static int count(const pl_model_t *m, int up, int enc)
{
    int n = 0;
    while (n < PL_TARGETS && m->user_t[up][enc][n].pe != PL_NONE) {
        n++;
    }
    return n;
}

static void fill(const pl_edit_t *e, pl_custom_edit_t *out)
{
    *out = (pl_custom_edit_t){(uint8_t)e->up, (uint8_t)e->enc, (uint8_t)e->idx, e->pe, e->lo, e->hi};
}

void pl_edit_start(pl_edit_t *e, int up)
{
    *e = (pl_edit_t){.state = PL_EDIT_SLOTS, .up = up};
}

void pl_edit_slot(pl_edit_t *e, int enc)
{
    if (e->state == PL_EDIT_SLOTS && enc > 0 && enc < PL_KNOBS) {
        e->enc = enc;
        e->state = PL_EDIT_TARGETS;
    }
}

void pl_edit_add(pl_edit_t *e, const pl_model_t *m)
{
    int n = count(m, e->up, e->enc);
    if (e->state == PL_EDIT_TARGETS && n < PL_TARGETS) {
        e->idx = n;
        e->state = PL_EDIT_PICK_PAGE;
    }
}

int pl_edit_open(pl_edit_t *e, const pl_model_t *m, int idx)
{
    if (e->state != PL_EDIT_TARGETS || idx < 0 || idx >= count(m, e->up, e->enc)) {
        return 0;
    }
    const pl_target_t *t = &m->user_t[e->up][e->enc][idx];
    e->idx = idx;
    e->pe = t->pe, e->lo = t->lo, e->hi = t->hi;
    e->page = t->pe >> 4;
    e->which = 0;
    e->state = PL_EDIT_RANGE;
    return 1;
}

void pl_edit_page(pl_edit_t *e, int page)
{
    if (e->state == PL_EDIT_PICK_PAGE && page >= 0 && page < PL_PAGES) {
        e->page = page;
        e->state = PL_EDIT_PICK_KNOB;
    }
}

int pl_edit_knob(pl_edit_t *e, const pl_model_t *m, int enc, pl_custom_edit_t *out)
{
    if (e->state != PL_EDIT_PICK_KNOB || enc <= 0 || enc >= PL_KNOBS) {
        return 0;
    }
    if (enc == 1 && (e->page == 6 || e->page == 7 || e->page == 10 || e->page == 11)) { // и слои
        return 0; // тип синта меняет смысл всей страницы
    }
    if (!pl_knob_label(e->page, enc, m->raw[e->page])) {
        return 0;
    }
    int lo, hi;
    pl_knob_range(e->page, enc, m->raw[e->page], &lo, &hi);
    e->pe = (uint8_t)(e->page << 4 | enc);
    e->lo = (uint8_t)lo, e->hi = (uint8_t)hi;
    e->which = 0;
    e->state = PL_EDIT_RANGE;
    fill(e, out);
    return 1;
}

int pl_edit_range(pl_edit_t *e, const pl_model_t *m, int v, pl_custom_edit_t *out)
{
    if (e->state != PL_EDIT_RANGE) {
        return 0;
    }
    int page = e->pe >> 4, lo, hi;
    pl_knob_range(page, e->pe & 15, m->raw[page], &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (e->which) {
        e->hi = (uint8_t)v;
    } else {
        e->lo = (uint8_t)v;
    }
    fill(e, out);
    return 1;
}

int pl_edit_remove(pl_edit_t *e, int idx, pl_custom_edit_t *out)
{
    if (e->state != PL_EDIT_TARGETS || idx < 0 || idx >= PL_TARGETS) {
        return 0;
    }
    *out = (pl_custom_edit_t){(uint8_t)e->up, (uint8_t)e->enc, (uint8_t)idx, PL_NONE, 0, 0};
    return 1;
}

void pl_edit_back(pl_edit_t *e)
{
    switch (e->state) {
    case PL_EDIT_RANGE:
    case PL_EDIT_PICK_PAGE: e->state = PL_EDIT_TARGETS; break;
    case PL_EDIT_PICK_KNOB: e->state = PL_EDIT_PICK_PAGE; break;
    case PL_EDIT_TARGETS: e->state = e->mod ? PL_EDIT_OFF : PL_EDIT_SLOTS; break;
    default: e->state = PL_EDIT_OFF; break;
    }
}

void pl_edit_mod_start(pl_edit_t *e, int k)
{
    *e = (pl_edit_t){.state = PL_EDIT_TARGETS, .mod = 1, .up = k};
}

void pl_edit_mod_row(pl_edit_t *e, const pl_model_t *m, int idx)
{
    (void)m;
    if (e->mod && e->state == PL_EDIT_TARGETS && idx >= 0 && idx < PL_MOD_TARGETS) {
        e->idx = idx;
        e->state = PL_EDIT_PICK_PAGE;
    }
}

void pl_edit_mod_page(pl_edit_t *e, const pl_model_t *m, int page)
{
    if (e->mod && e->state == PL_EDIT_PICK_PAGE && page >= 0 && page < PL_PAGES && pl_mod_page_has_target(m, page)) {
        e->page = page;
        e->state = PL_EDIT_PICK_KNOB;
    }
}

int pl_edit_mod_knob(pl_edit_t *e, pl_model_t *m, int enc, pl_mod_edit_t *out)
{
    if (!e->mod || e->state != PL_EDIT_PICK_KNOB || !pl_mod_edit(m, e->up, e->idx, e->page, enc, out)) {
        return 0;
    }
    e->state = PL_EDIT_TARGETS;
    return 1;
}

int pl_edit_mod_remove(pl_edit_t *e, pl_model_t *m, int idx, pl_mod_edit_t *out)
{
    if (!e->mod || e->state != PL_EDIT_TARGETS || idx < 0 || idx >= PL_MOD_TARGETS ||
        m->mod_t[e->up][idx].page == PL_NONE) {
        return 0;
    }
    return pl_mod_edit(m, e->up, idx, PL_NONE, 0, out);
}
