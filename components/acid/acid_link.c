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

static int report(const acid_link_t *l, const acid_synth_t *s, int page, int enc, int src, acid_link_ev_t *out)
{
    *out = (acid_link_ev_t){
        .page = (uint8_t)page,
        .enc = (uint8_t)enc,
        .value = (uint8_t)acid_synth_knob(s, page, enc),
        .src = (uint8_t)src,
        .bsp_pos = l->bsp_pos[enc],
        .held = l->held[page][enc],
    };
    return 1;
}

int acid_link_midi(acid_link_t *l, acid_synth_t *s, const acid_midi_event_t *ev, int src, acid_link_ev_t *out)
{
    int enc = ev->type == ACID_MIDI_CC ? enc_of(ev->d1) : -1;
    if (ev->type == ACID_MIDI_CC && src == ACID_SRC_DEMO && l->demo_muted) {
        return 0;
    }
    if (enc < 0) {
        acid_synth_midi(s, ev);
        return 0;
    }
    if (enc == 0) {
        acid_synth_midi(s, ev);
        *out = (acid_link_ev_t){.page = (uint8_t)s->page, .enc = 0, .value = (uint8_t)s->page, .src = (uint8_t)src};
        return 1;
    }
    int page = s->page;
    if (src == ACID_SRC_BSP) {
        int prev = l->bsp_pos[enc], v = ev->d2;
        l->bsp_pos[enc] = (uint8_t)v;
        if (l->held[page][enc]) {
            int target = acid_synth_knob(s, page, enc);
            if (prev == POS_NONE || (prev - target) * (v - target) > 0) {
                return report(l, s, page, enc, src, out); // ещё не дошёл: BSP молчит
            }
            l->held[page][enc] = 0;
        }
    }
    acid_synth_midi(s, ev);
    type_check(l, page, enc);
    return report(l, s, page, enc, src, out);
}

int acid_link_panel_set(acid_link_t *l, acid_synth_t *s, int page, int enc, int v, acid_link_ev_t *out)
{
    if (page < 0 || page >= ACID_PAGES || enc <= 0 || enc >= ACID_KNOBS || v < 0 || v > 127) {
        return 0;
    }
    acid_synth_set_knob(s, page, enc, v);
    l->held[page][enc] = 1;
    l->demo_muted = 1; // в live демо нет, флаг ни на что не влияет
    type_check(l, page, enc);
    return report(l, s, page, enc, ACID_SRC_PANEL, out);
}

int acid_link_panel_page(acid_link_t *l, acid_synth_t *s, int page, acid_link_ev_t *out)
{
    (void)l;
    if (page < 0 || page >= ACID_PAGES) {
        return 0;
    }
    s->page = page;
    *out = (acid_link_ev_t){.page = (uint8_t)page, .enc = 0, .value = (uint8_t)page, .src = ACID_SRC_PANEL};
    return 1;
}
