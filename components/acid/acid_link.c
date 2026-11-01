#include "acid_link.h"

#include <string.h>

#define POS_NONE 0xFF
#define TYPE_ENC 1 // энкодер 2 на страницах синтов - тип синтеза
#define PAGE_SYNTH1 6

void acid_link_init(acid_link_t *l)
{
    memset(l->bsp_pos, POS_NONE, sizeof l->bsp_pos);
    memset(l->held, 0, sizeof l->held);
    l->demo_muted = 0;
    l->snapshot_due = 0;
    l->user = -1;
    l->npend = l->pend_at = 0;
    l->custom_due = 0;
    memset(l->defer_st, 0, sizeof l->defer_st);
    l->dq_head = l->dq_n = 0;
}

void acid_link_use_custom(acid_link_t *l, acid_custom_t *c)
{
    l->custom = c;
    l->user = -1;
}

int acid_link_take(acid_link_t *l, acid_link_ev_t *out)
{
    if (l->pend_at >= l->npend) {
        return 0;
    }
    *out = l->pend[l->pend_at++];
    return 1;
}

int acid_link_page(const acid_link_t *l, const acid_synth_t *s)
{
    return l->user >= 0 ? ACID_USER_FIRST + l->user : s->page;
}

int acid_link_custom_edit(acid_link_t *l, int up, int enc, int idx, int page, int tenc, int lo, int hi)
{
    if (!l->custom || up < 0 || up >= ACID_USER_PAGES) {
        return 0;
    }
    int ok = acid_custom_edit(l->custom, up, enc, idx, page, tenc, lo, hi);
    l->custom_due |= (uint8_t)(1 << up); // и при отказе: у панели устаревшая картина, ей нужна правда
    return ok;
}

static int is_user(const acid_link_t *l, int page)
{
    return l->custom && page >= ACID_USER_FIRST && page < ACID_USER_FIRST + ACID_USER_PAGES;
}

static int enc_of(int cc)
{
    for (int i = 0; i < ACID_KNOBS; i++) {
        if (acid_bsp_enc_cc[i] == cc) {
            return i;
        }
    }
    return -1;
}

static void type_check(acid_link_t *l, int page, int enc)
{
    if (enc == TYPE_ENC && (page == PAGE_SYNTH1 || page == PAGE_SYNTH1 + 1)) {
        l->snapshot_due = 1; // у каждого типа своя память ручек: вся страница новая
    }
}

// Значение ручки как на экране: у своей страницы - значение слота.
static int knob_of(const acid_link_t *l, const acid_synth_t *s, int page, int enc)
{
    if (page >= ACID_USER_FIRST) {
        return acid_custom_value(l->custom, s, page - ACID_USER_FIRST, enc);
    }
    return acid_synth_knob(s, page, enc);
}

static int report(const acid_link_t *l, const acid_synth_t *s, int page, int enc, int src, acid_link_ev_t *out)
{
    *out = (acid_link_ev_t){
        .page = (uint8_t)page,
        .enc = (uint8_t)enc,
        .value = (uint8_t)knob_of(l, s, page, enc),
        .src = (uint8_t)src,
        .bsp_pos = l->bsp_pos[enc],
        .held = l->held[page][enc],
    };
    return 1;
}

static void defer_put(acid_link_t *l, int page, int enc, int v)
{
    int k = page * ACID_KNOBS + enc;
    if (l->defer_st[k] == 0) {
        l->defer_q[(l->dq_head + l->dq_n++) % ACID_LINK_DEFER] = (uint8_t)k;
    }
    l->defer_st[k] = 1;
    l->defer_v[k] = (uint8_t)v;
}

// Прямая правка ручки новее отложенной цели макро.
static void defer_cancel(acid_link_t *l, int page, int enc)
{
    int k = page * ACID_KNOBS + enc;
    if (page < ACID_PAGES && l->defer_st[k] == 1) {
        l->defer_st[k] = 2;
    }
}

// Слот своей страницы: псевдоним - прямо в цель (событие цели в pend), макро - своё значение сразу,
// цели в очередь шага связки.
static void user_apply(acid_link_t *l, acid_synth_t *s, int up, int enc, int v)
{
    acid_custom_t *c = l->custom;
    l->npend = l->pend_at = 0;
    int n = acid_custom_count(c, up, enc);
    if (acid_custom_is_alias(c, up, enc)) {
        const acid_target_t *t = &c->t[up][enc][0];
        defer_cancel(l, t->page, t->enc);
        acid_synth_set_knob(s, t->page, t->enc, v);
        // Цель - отражение поворота слота, не поворот на её странице: всплывашка остаётся за слотом.
        report(l, s, t->page, t->enc, ACID_SRC_PANEL, &l->pend[l->npend++]);
        return;
    }
    c->val[up][enc] = (uint8_t)v;
    c->save = 1;
    for (int i = 0; i < n; i++) {
        const acid_target_t *t = &c->t[up][enc][i];
        defer_put(l, t->page, t->enc, acid_custom_map(t, v));
    }
}

int acid_link_step(acid_link_t *l, acid_synth_t *s, acid_link_ev_t *out)
{
    l->npend = l->pend_at = 0;
    int got = 0;
    while (l->dq_n > 0 && got < ACID_LINK_PER_CHUNK) {
        int k = l->defer_q[l->dq_head];
        l->dq_head = (l->dq_head + 1) % ACID_LINK_DEFER;
        l->dq_n--;
        int st = l->defer_st[k];
        l->defer_st[k] = 0;
        if (st != 1) {
            continue;
        }
        int page = k / ACID_KNOBS, enc = k % ACID_KNOBS;
        acid_synth_set_knob(s, page, enc, l->defer_v[k]);
        report(l, s, page, enc, ACID_SRC_PANEL, got ? &l->pend[l->npend++] : out);
        got++;
    }
    return got > 0;
}

int acid_link_midi(acid_link_t *l, acid_synth_t *s, const acid_midi_event_t *ev, int src, acid_link_ev_t *out)
{
    int enc = ev->type == ACID_MIDI_CC ? enc_of(ev->d1) : -1;
    l->npend = l->pend_at = 0;
    if (ev->type == ACID_MIDI_CC && src == ACID_SRC_DEMO && l->demo_muted) {
        return 0;
    }
    if (enc < 0) {
        acid_synth_midi(s, ev);
        return 0;
    }
    if (enc == 0) {
        acid_synth_midi(s, ev);
        if (is_user(l, ev->d2)) {
            l->user = ev->d2 - ACID_USER_FIRST; // синт её проигнорировал, штатная s->page прежняя
            *out = (acid_link_ev_t){.page = ev->d2, .enc = 0, .value = ev->d2, .src = (uint8_t)src};
            return 1;
        }
        if (ev->d2 >= ACID_PAGES) {
            return 0; // не страница: синт её проигнорировал, экран не дёргаем
        }
        l->user = -1;
        *out = (acid_link_ev_t){.page = (uint8_t)s->page, .enc = 0, .value = (uint8_t)s->page, .src = (uint8_t)src};
        return 1;
    }
    int page = acid_link_page(l, s);
    if (src == ACID_SRC_BSP) {
        int prev = l->bsp_pos[enc], v = ev->d2;
        l->bsp_pos[enc] = (uint8_t)v;
        if (l->held[page][enc]) {
            int target = knob_of(l, s, page, enc);
            if (prev == POS_NONE || (prev - target) * (v - target) > 0) {
                return report(l, s, page, enc, src, out); // ещё не дошёл: BSP молчит
            }
            l->held[page][enc] = 0;
        }
    }
    if (l->user >= 0) {
        user_apply(l, s, l->user, enc, ev->d2);
        return report(l, s, page, enc, src, out);
    }
    defer_cancel(l, page, enc);
    acid_synth_midi(s, ev);
    type_check(l, page, enc);
    return report(l, s, page, enc, src, out);
}

int acid_link_panel_set(acid_link_t *l, acid_synth_t *s, int page, int enc, int v, acid_link_ev_t *out)
{
    if (page < 0 || (page >= ACID_PAGES && !is_user(l, page)) || enc <= 0 || enc >= ACID_KNOBS || v < 0 ||
        v > 127) {
        return 0;
    }
    if (page >= ACID_USER_FIRST) {
        user_apply(l, s, page - ACID_USER_FIRST, enc, v);
    } else {
        l->npend = l->pend_at = 0;
        defer_cancel(l, page, enc);
        acid_synth_set_knob(s, page, enc, v);
    }
    l->held[page][enc] = 1;
    l->demo_muted = 1; // в live демо нет, флаг ни на что не влияет
    type_check(l, page, enc);
    return report(l, s, page, enc, ACID_SRC_PANEL, out);
}

int acid_link_panel_page(acid_link_t *l, acid_synth_t *s, int page, acid_link_ev_t *out)
{
    if (is_user(l, page)) {
        l->user = page - ACID_USER_FIRST;
        *out = (acid_link_ev_t){.page = (uint8_t)page, .enc = 0, .value = (uint8_t)page, .src = ACID_SRC_PANEL};
        return 1;
    }
    if (page < 0 || page >= ACID_PAGES) {
        return 0;
    }
    l->user = -1;
    s->page = page;
    *out = (acid_link_ev_t){.page = (uint8_t)page, .enc = 0, .value = (uint8_t)page, .src = ACID_SRC_PANEL};
    return 1;
}
