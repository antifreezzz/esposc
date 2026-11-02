#include "pl_grids_ui.h"

// Ряды экрана (y), отступ 8 по краям.
#define ROW1_Y 48 // вкл, сбивка, хэт
#define ROW1_H 48
#define ROW2_Y 104 // карта и ползунки
#define ROW2_H 200
#define ROW3_Y 312 // время, каждые, длина
#define ROW3_H 40
#define GRID_X 32 // сетка шагов: подписи дорожек левее
#define GRID_Y 362
#define CELL_DX 14
#define CELL_DY 38
#define CELL_W 12
#define CELL_H 34

typedef struct {
    short x, y, w, h, n; // n сегментов поровну по ширине
} area_t;

static const area_t AREA[PL_GUI_IDS] = {
    [PL_GUI_ON] = {8, ROW1_Y, 80, ROW1_H, 1},
    [PL_GUI_FILL] = {96, ROW1_Y, 120, ROW1_H, 1},
    [PL_GUI_HAT] = {224, ROW1_Y, 248, ROW1_H, 3},
    [PL_GUI_XY] = {8, ROW2_Y, 200, ROW2_H, 1},
    [PL_GUI_SLIDER] = {220, ROW2_Y, 252, ROW2_H, 4},
    [PL_GUI_TIME] = {8, ROW3_Y, 120, ROW3_H, 2},
    [PL_GUI_EVERY] = {136, ROW3_Y, 200, ROW3_H, 4},
    [PL_GUI_LEN] = {344, ROW3_Y, 128, ROW3_H, 3},
};
#define GAP 4 // между сегментами

int pl_gui_rect(int id, int idx, int *x, int *y, int *w, int *h)
{
    if (id <= PL_GUI_NONE || id >= PL_GUI_IDS || idx < 0 || idx >= AREA[id].n) {
        return 0;
    }
    const area_t *a = &AREA[id];
    int step = a->w / a->n;
    *x = a->x + idx * step;
    *y = a->y;
    *w = a->n > 1 ? step - GAP : a->w;
    *h = a->h;
    return 1;
}

int pl_gui_at(int x, int y, int *arg)
{
    for (int id = 1; id < PL_GUI_IDS; id++) {
        for (int i = 0; i < AREA[id].n; i++) {
            int rx, ry, rw, rh;
            pl_gui_rect(id, i, &rx, &ry, &rw, &rh);
            if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
                *arg = i;
                return id;
            }
        }
    }
    *arg = -1;
    return PL_GUI_NONE;
}

int pl_gui_enc(int id, int idx)
{
    switch (id) {
    case PL_GUI_ON: return 1;
    case PL_GUI_SLIDER: return 4 + idx;
    case PL_GUI_HAT: return 8;
    case PL_GUI_TIME: return 9;
    case PL_GUI_EVERY: return 10;
    case PL_GUI_LEN: return 11;
    case PL_GUI_FILL: return 12;
    default: return 0;
    }
}

int pl_gui_slider_value(int y)
{
    int v = (ROW2_Y + ROW2_H - 1 - y) * 127 / (ROW2_H - 1);
    return v < 0 ? 0 : v > 127 ? 127 : v;
}

int pl_gui_cell(int step, int part, int *x, int *y, int *w, int *h)
{
    if (step < 0 || step >= 32 || part < 0 || part >= 3) {
        return 0;
    }
    *x = GRID_X + step * CELL_DX;
    *y = GRID_Y + part * CELL_DY;
    *w = CELL_W;
    *h = CELL_H;
    return 1;
}
