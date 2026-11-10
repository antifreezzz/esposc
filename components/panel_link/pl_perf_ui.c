#include "pl_perf_ui.h"
#include "pl_proto.h"

#define HBAR_X 216
#define HBAR_W 256
#define FADER_Y 316
#define FADER_H 108

typedef struct {
    short x, y, step, w, h, n;
} area_t;

static const area_t AREA[PL_PUI_IDS] = {
    [PL_PUI_SCENE] = {8, 48, 50, 46, 52, PL_PUI_SCENES},
    [PL_PUI_MORPH] = {410, 48, 62, 62, 52, 1},
    [PL_PUI_XY] = {8, 108, 200, 200, 200, 1},
    [PL_PUI_FILL] = {216, 108, 124, 124, 52, 1},
    [PL_PUI_GRIDS] = {348, 108, 124, 124, 52, 1},
    [PL_PUI_STUTTER] = {216, 182, 52, 48, 38, PL_PUI_STUTTERS},
    [PL_PUI_DJF] = {HBAR_X, 226, HBAR_W, HBAR_W, 38, 1},
    [PL_PUI_PUMP] = {HBAR_X, 270, HBAR_W, HBAR_W, 38, 1},
    [PL_PUI_FADER] = {8, FADER_Y, 67, 62, FADER_H, PL_PUI_FADERS},
};

int pl_pui_rect(int id, int idx, int *x, int *y, int *w, int *h)
{
    if (id <= PL_PUI_NONE || id >= PL_PUI_IDS || idx < 0 || idx >= AREA[id].n) {
        return 0;
    }
    const area_t *a = &AREA[id];
    *x = a->x + idx * a->step;
    *y = a->y;
    *w = a->w;
    *h = a->h;
    return 1;
}

int pl_pui_at(int x, int y, int *arg)
{
    for (int id = 1; id < PL_PUI_IDS; id++) {
        for (int i = 0; i < AREA[id].n; i++) {
            int rx, ry, rw, rh;
            pl_pui_rect(id, i, &rx, &ry, &rw, &rh);
            if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
                *arg = i;
                return id;
            }
        }
    }
    *arg = -1;
    return PL_PUI_NONE;
}

int pl_pui_target(int id, int idx, int *page, int *enc)
{
    static const int FENC[PL_PUI_FADERS] = {1, 5, 6, 7, 8, 3, 4}; // MASTER, KICK, HATS, CLAP, SNARE, SYN 1, SYN 2
    if (id <= PL_PUI_NONE || id >= PL_PUI_IDS || idx < 0 || idx >= AREA[id].n) {
        return 0;
    }
    switch (id) {
    case PL_PUI_FILL:
        *page = PL_PAGE_GRIDS, *enc = 12;
        return 1;
    case PL_PUI_GRIDS:
        *page = PL_PAGE_GRIDS, *enc = 1;
        return 1;
    case PL_PUI_STUTTER:
        *page = 8, *enc = 9;
        return 1;
    case PL_PUI_DJF:
        *page = 0, *enc = 11;
        return 1;
    case PL_PUI_PUMP:
        *page = 0, *enc = 9;
        return 1;
    case PL_PUI_FADER:
        *page = 0, *enc = FENC[idx];
        return 1;
    default:
        return 0;
    }
}

int pl_pui_hbar_value(int x)
{
    int v = (x - HBAR_X) * 127 / (HBAR_W - 1);
    return v < 0 ? 0 : v > 127 ? 127 : v;
}

int pl_pui_fader_value(int y)
{
    int v = (FADER_Y + FADER_H - 1 - y) * 127 / (FADER_H - 1);
    return v < 0 ? 0 : v > 127 ? 127 : v;
}
