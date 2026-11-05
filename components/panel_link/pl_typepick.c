#include "pl_typepick.h"

#include "pl_proto.h"

#define X0 6
#define Y0 (40 + 6) // под шапкой
#define STEP_X 117
#define STEP_Y 62
#define GAP 6

static int is_layer(int page)
{
    return page == PL_PAGE_LAYER1 || page == PL_PAGE_LAYER1 + 1;
}

int pl_tp_on(int page, int enc)
{
    return enc == 1 && (page == PL_PAGE_SYNTH1 || page == PL_PAGE_SYNTH1 + 1 || is_layer(page));
}

// Синт: 0..25 (PETRI последним). Слой: 0..24 и «выкл» на месте PETRI.
int pl_tp_count(int page)
{
    (void)page;
    return PL_TYPE_PETRI + 1;
}

int pl_tp_type(int page, int slot)
{
    if (slot < 0 || slot >= pl_tp_count(page)) {
        return -1;
    }
    return is_layer(page) && slot == PL_TYPE_PETRI ? PL_LAYER_OFF : slot;
}

int pl_tp_slot(int page, int type)
{
    for (int s = 0; s < pl_tp_count(page); s++) {
        if (pl_tp_type(page, s) == type) {
            return s;
        }
    }
    return -1;
}

void pl_tp_rect(int slot, int *x, int *y, int *w, int *h)
{
    *x = X0 + (slot % PL_TP_COLS) * STEP_X;
    *y = Y0 + (slot / PL_TP_COLS) * STEP_Y;
    *w = STEP_X - GAP;
    *h = STEP_Y - GAP;
}

int pl_tp_at(int x, int y)
{
    if (x < X0 || y < Y0) {
        return -1;
    }
    int c = (x - X0) / STEP_X, r = (y - Y0) / STEP_Y;
    if (c >= PL_TP_COLS || r >= PL_TP_ROWS || (x - X0) % STEP_X >= STEP_X - GAP || (y - Y0) % STEP_Y >= STEP_Y - GAP) {
        return -1;
    }
    return r * PL_TP_COLS + c;
}
