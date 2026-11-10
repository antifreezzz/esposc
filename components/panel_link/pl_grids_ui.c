#include "pl_grids_ui.h"

#include "pl_grids.h"

// Ряды экрана (y) между вкладками и доком (76..432), отступ 8 по краям.
#define ROW1_Y 82 // вкл, сбивка, хэт
#define ROW1_H 40
#define ROW2_Y 128 // карта и ползунки
#define ROW2_H 160
#define SRC_H 36 // плитка источника над картой
#define ROW3_Y 294 // время, каждые, длина
#define ROW3_H 36
#define GRID_X 32 // сетка шагов: подписи дорожек левее
#define GRID_Y 338
#define CELL_DX 14
#define CELL_DY 28
#define CELL_W 12
#define CELL_H 24

typedef struct {
    short x, y, w, h, n; // n сегментов поровну по ширине
} area_t;

static const area_t AREA[PL_GUI_IDS] = {
    [PL_GUI_ON] = {8, ROW1_Y, 80, ROW1_H, 1},
    [PL_GUI_FILL] = {96, ROW1_Y, 120, ROW1_H, 1},
    [PL_GUI_HAT] = {224, ROW1_Y, 248, ROW1_H, 3},
    [PL_GUI_SRC] = {8, ROW2_Y, 200, SRC_H, 1},
    [PL_GUI_XY] = {8, ROW2_Y + SRC_H + 4, 200, ROW2_H - SRC_H - 4, 1},
    [PL_GUI_SLIDER] = {220, ROW2_Y, 250, ROW2_H, 5},
    [PL_GUI_GRID] = {GRID_X, GRID_Y, 32 * CELL_DX, 3 * CELL_DY, 1},
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
    case PL_GUI_SLIDER: return idx < 4 ? 4 + idx : 14; // плотности и хаос, пятый - свинг
    case PL_GUI_SRC: return 13;
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

int pl_gui_swing_value(int y)
{
    return pl_gui_slider_value(y) * 4 / 128;
}

int pl_gui_cell_at(int grid, int step, int part, int *x, int *y, int *w, int *h)
{
    if (grid != PL_GRIDS_12T) {
        return pl_gui_cell(step, part, x, y, w, h);
    }
    if (step < 0 || step >= 32 || step % 16 >= 12 || part < 0 || part >= 3) {
        return 0;
    }
    int col = step / 16 * 12 + step % 16, dx = 32 * CELL_DX / 24;
    *x = GRID_X + col * dx;
    *y = GRID_Y + part * CELL_DY;
    *w = dx - (CELL_DX - CELL_W);
    *h = CELL_H;
    return 1;
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

// Редактор: кнопки сверху, такт в два яруса по три дорожки, COPY и CLEAR снизу.
#define ED_TOP_Y 82
#define ED_TOP_H 40
#define ED_X 28 // левее - подписи дорожек
#define ED_Y 130
#define ED_ROW_DY 35 // дорожка в ярусе
#define ED_ROW_H 33
#define ED_TIER_DY 116 // ярус: три дорожки, под ними бегущий шаг
#define ED_BOT_Y 370
#define ED_BOT_H 44

static const area_t ED[PL_GED_IDS] = {
    [PL_GED_BACK] = {8, ED_TOP_Y, 72, ED_TOP_H, 1},
    [PL_GED_SLOT] = {88, ED_TOP_Y, 200, ED_TOP_H, 4},
    [PL_GED_BAR] = {296, ED_TOP_Y, 96, ED_TOP_H, 2},
    [PL_GED_TRIP] = {400, ED_TOP_Y, 72, ED_TOP_H, 1},
    [PL_GED_COPY] = {8, ED_BOT_Y, 260, ED_BOT_H, 1},
    [PL_GED_CLEAR] = {276, ED_BOT_Y, 196, ED_BOT_H, 1},
};

int pl_ged_rect(int id, int idx, int *x, int *y, int *w, int *h)
{
    if (id <= PL_GED_NONE || id >= PL_GED_IDS || idx < 0 || idx >= ED[id].n) {
        return 0;
    }
    const area_t *a = &ED[id];
    int step = a->w / a->n;
    *x = a->x + idx * step;
    *y = a->y;
    *w = a->n > 1 ? step - GAP : a->w;
    *h = a->h;
    return 1;
}

// Шагов в ярусе и шаг клетки по x.
static int ed_per(int grid)
{
    return grid == PL_GRIDS_12T ? 6 : 8;
}

static int ed_pitch(int grid)
{
    return grid == PL_GRIDS_12T ? 74 : 56;
}

int pl_ged_cell(int grid, int col, int part, int *x, int *y, int *w, int *h)
{
    int per = ed_per(grid);
    if (col < 0 || col >= 2 * per || part < 0 || part >= 3) {
        return 0;
    }
    *x = ED_X + col % per * ed_pitch(grid);
    *y = ED_Y + col / per * ED_TIER_DY + part * ED_ROW_DY;
    *w = ed_pitch(grid) - 3;
    *h = ED_ROW_H;
    return 1;
}

int pl_ged_at(int grid, int x, int y, int *arg)
{
    for (int id = 1; id < PL_GED_IDS; id++) {
        for (int i = 0; i < ED[id].n; i++) {
            int rx, ry, rw, rh;
            pl_ged_rect(id, i, &rx, &ry, &rw, &rh);
            if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
                *arg = i;
                return id;
            }
        }
    }
    int per = ed_per(grid);
    if (x >= ED_X && x < ED_X + per * ed_pitch(grid)) {
        for (int tier = 0; tier < 2; tier++) {
            int ty = ED_Y + tier * ED_TIER_DY;
            if (y >= ty && y < ty + 3 * ED_ROW_DY) {
                *arg = (y - ty) / ED_ROW_DY * 16 + tier * per + (x - ED_X) / ed_pitch(grid);
                return PL_GED_CELL;
            }
        }
    }
    *arg = -1;
    return PL_GED_NONE;
}

int pl_ged_next(int lv)
{
    if (lv <= 0) {
        return PL_GRIDS_LV_HIT;
    }
    if (lv > PL_GRIDS_ACCENT) {
        return PL_GRIDS_LV_GHOST;
    }
    return lv >= PL_GRIDS_GHOST ? PL_GRIDS_LV_ACC : 0;
}
