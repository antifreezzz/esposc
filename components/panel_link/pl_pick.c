#include "pl_pick.h"

#include "pl_labels.h"
#include "pl_view.h"

#define X0 6
#define Y0 (40 + 6) // под шапкой
#define AREA_W 468
#define AREA_H 434
#define GAP 6
#define MAX_STEP_Y 110 // у коротких списков клетки не выше этого, ряд по центру

#define TONAL 9
#define FX 8

static int is_synth(int page)
{
    return page == PL_PAGE_SYNTH1 || page == PL_PAGE_SYNTH1 + 1 || page == PL_PAGE_LAYER1 || page == PL_PAGE_LAYER1 + 1;
}

// Переключатели 0..127 с порогом 64: LYR HALF, FREEZE.
static int half_toggle(int page, int enc)
{
    return (page == FX || page == PL_PAGE_PETRI) && enc == 14;
}

// Синт ловит фронт 0 -> 1: RESEED чашки, FILL Grids.
static int trigger(int page, int enc)
{
    return (page == PL_PAGE_PETRI && enc == 15) || (page == PL_PAGE_GRIDS && enc == 12);
}

// Ход сетки: шаги тональности в ладу синт зажимает до ±7, лишние клетки повторяли бы крайние.
static void span(int page, int enc, const uint8_t raw[16], int *lo, int *hi)
{
    pl_knob_range(page, enc, raw, lo, hi);
    if (page == TONAL && enc >= 8 && raw[6] != 0) {
        *lo = 64 - 7, *hi = 64 + 7;
    }
}

int pl_pick_kind(int page, int enc, const uint8_t raw[16])
{
    if (page < 0 || page >= PL_PAGES || !pl_knob_label(page, enc, raw)) {
        return PL_PICK_NONE;
    }
    if (trigger(page, enc)) {
        return PL_PICK_TRIGGER;
    }
    if (half_toggle(page, enc)) {
        return PL_PICK_TOGGLE;
    }
    if (!pl_knob_list(page, enc, raw)) {
        return PL_PICK_NONE;
    }
    int lo, hi;
    span(page, enc, raw, &lo, &hi);
    return hi - lo == 1 ? PL_PICK_TOGGLE : PL_PICK_GRID;
}

int pl_pick_view(const pl_model_t *m, int page, int enc, int *tp, int *te)
{
    *tp = page, *te = enc;
    if (page < PL_PAGES) {
        return page >= 0 ? pl_pick_kind(page, enc, m->raw[page]) : PL_PICK_NONE;
    }
    if (!pl_view_is_alias(m, page - PL_USER_FIRST, enc) || pl_view_target(m, page, enc, 0, te) < 0) {
        return PL_PICK_NONE;
    }
    *tp = pl_view_target(m, page, enc, 0, te);
    return *tp < PL_PAGES ? pl_pick_kind(*tp, *te, m->raw[*tp]) : PL_PICK_NONE;
}

int pl_pick_count(int page, int enc, const uint8_t raw[16])
{
    if (pl_pick_kind(page, enc, raw) != PL_PICK_GRID) {
        return 0;
    }
    int lo, hi, skip = pl_knob_skip(page, enc, raw);
    span(page, enc, raw, &lo, &hi);
    int n = hi - lo + 1 - (skip >= lo && skip <= hi);
    return n <= PL_PICK_MAX ? n : 0;
}

int pl_pick_value(int page, int enc, const uint8_t raw[16], int slot)
{
    if (slot < 0 || slot >= pl_pick_count(page, enc, raw)) {
        return -1;
    }
    int lo, hi, skip = pl_knob_skip(page, enc, raw);
    span(page, enc, raw, &lo, &hi);
    int v = lo + slot;
    return skip >= lo && v >= skip ? v + 1 : v; // TYPE слоя: PETRI пропущен, OFF на его месте
}

int pl_pick_slot(int page, int enc, const uint8_t raw[16], int v)
{
    for (int s = 0; s < pl_pick_count(page, enc, raw); s++) {
        if (pl_pick_value(page, enc, raw, s) == v) {
            return s;
        }
    }
    return -1;
}

int pl_pick_next(int page, int enc, const uint8_t raw[16], int v)
{
    if (half_toggle(page, enc)) {
        return v >= 64 ? 0 : 127;
    }
    int lo, hi;
    span(page, enc, raw, &lo, &hi);
    return v == lo ? hi : lo;
}

int pl_pick_grab(int page, int enc, const uint8_t raw[16], int hold)
{
    int k = pl_pick_kind(page, enc, raw);
    if (k == PL_PICK_NONE) {
        return 1;
    }
    return k == PL_PICK_GRID && !hold && !(is_synth(page) && enc == 1);
}

// Столбцы по числу значений: типы - прежняя сетка 4x7, короткий список - один ряд.
static void layout(int n, int *cols, int *rows, int *sx, int *sy, int *y0)
{
    *cols = n <= 3 ? (n > 0 ? n : 1) : n <= 9 ? 3 : n <= 16 || n > 25 ? 4 : 5;
    *rows = (n + *cols - 1) / *cols;
    if (*rows < 1) {
        *rows = 1;
    }
    *sx = AREA_W / *cols;
    *sy = AREA_H / *rows;
    *sy = *sy > MAX_STEP_Y ? MAX_STEP_Y : *sy;
    *y0 = Y0 + (AREA_H - *rows * *sy) / 2;
    if (n > 25) {
        *y0 = Y0; // типы - как раньше, от шапки
    }
}

void pl_pick_rect(int n, int slot, int *x, int *y, int *w, int *h)
{
    int cols, rows, sx, sy, y0;
    layout(n, &cols, &rows, &sx, &sy, &y0);
    *x = X0 + (slot % cols) * sx;
    *y = y0 + (slot / cols) * sy;
    *w = sx - GAP;
    *h = sy - GAP;
}

int pl_pick_at(int n, int x, int y)
{
    int cols, rows, sx, sy, y0;
    layout(n, &cols, &rows, &sx, &sy, &y0);
    if (x < X0 || y < y0) {
        return -1;
    }
    int c = (x - X0) / sx, r = (y - y0) / sy, s = r * cols + c;
    if (c >= cols || r >= rows || (x - X0) % sx >= sx - GAP || (y - y0) % sy >= sy - GAP || s >= n) {
        return -1;
    }
    return s;
}
