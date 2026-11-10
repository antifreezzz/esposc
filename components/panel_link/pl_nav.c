#include "pl_nav.h"

#include <stdlib.h>

#include "pl_ia.h"
#include "pl_proto.h"

#define W 480
#define SLIP 16 // палец можно увести за полосу, как у плиток

typedef struct {
    short nav, x0, x1;
} btn_t;

// Кнопки по видам шапки; NONE закрывает список. Заголовок - отдельно, между кнопками.
static const btn_t BTN[PL_BARS][3] = {
    [PL_BAR_MAIN] = {{PL_NAV_PLAY, 228, 312}, {PL_NAV_MODE, 318, 400}},
    [PL_BAR_OVERLAY] = {{PL_NAV_CLOSE, 4, 84}, {PL_NAV_ACTION, 396, 476}},
};
static const short TITLE[PL_BARS][2] = {[PL_BAR_MAIN] = {8, 224}, [PL_BAR_OVERLAY] = {92, 388}};

int pl_nav_at(int bar, int x)
{
    if (bar < 0 || bar >= PL_BARS) {
        return PL_NAV_NONE;
    }
    for (const btn_t *b = BTN[bar]; b < BTN[bar] + 3 && b->nav != PL_NAV_NONE; b++) {
        if (x >= b->x0 && x < b->x1) {
            return b->nav;
        }
    }
    return PL_NAV_NONE;
}

void pl_nav_rect(int bar, int nav, int *x0, int *x1)
{
    *x0 = *x1 = 0;
    if (bar < 0 || bar >= PL_BARS) {
        return;
    }
    for (const btn_t *b = BTN[bar]; b < BTN[bar] + 3 && b->nav != PL_NAV_NONE; b++) {
        if (b->nav == nav) {
            *x0 = b->x0, *x1 = b->x1;
        }
    }
}

void pl_nav_title(int bar, int *x0, int *x1)
{
    *x0 = *x1 = 0;
    if (bar >= 0 && bar < PL_BARS) {
        *x0 = TITLE[bar][0], *x1 = TITLE[bar][1];
    }
}

int pl_nav_tap(int bar, int x0, int y0, int x1, int y1)
{
    if (y0 >= PL_IA_TOP_H || y1 >= PL_IA_TOP_H + SLIP) {
        return PL_NAV_NONE;
    }
    int a = pl_nav_at(bar, x0);
    if (a == PL_NAV_PLAY && abs(x1 - x0) > PL_NAV_DRAG_PX) {
        return PL_NAV_NONE; // тянули темп
    }
    return a == pl_nav_at(bar, x1) ? a : PL_NAV_NONE;
}

int pl_nav_drag(int bar, int x0, int y0, int x)
{
    return y0 < PL_IA_TOP_H && pl_nav_at(bar, x0) == PL_NAV_PLAY && abs(x - x0) > PL_NAV_DRAG_PX;
}

int pl_nav_tempo(int dump_bpm, int pos_bpm, int *ext)
{
    *ext = pos_bpm > 0 && pos_bpm != dump_bpm;
    return pos_bpm > 0 ? pos_bpm : dump_bpm;
}

int pl_nav_work_y(int tabs)
{
    return PL_IA_TOP_H + (tabs ? PL_IA_TABS_H : 0);
}

int pl_nav_zone(int y, int tabs)
{
    if (y < PL_IA_TOP_H) {
        return PL_ZONE_TOP;
    }
    if (y >= W - PL_IA_DOCK_H) {
        return PL_ZONE_DOCK;
    }
    return y < pl_nav_work_y(tabs) ? PL_ZONE_TABS : PL_ZONE_WORK;
}

int pl_nav_dock_tap(int x0, int y0, int x1, int y1)
{
    if (y0 < W - PL_IA_DOCK_H || y1 < W - PL_IA_DOCK_H - SLIP) {
        return -1;
    }
    int g = pl_ia_dock_at(x0);
    return g >= 0 && g == pl_ia_dock_at(x1) && pl_ia_group_on(g) ? g : -1;
}

static int in_tabs(int y0, int y1)
{
    return y0 >= PL_IA_TOP_H && y0 < PL_IA_TOP_H + PL_IA_TABS_H && y1 >= PL_IA_TOP_H - SLIP &&
           y1 < PL_IA_TOP_H + PL_IA_TABS_H + SLIP;
}

int pl_nav_tab_tap(int group, int x0, int y0, int x1, int y1)
{
    if (!in_tabs(y0, y1)) {
        return -1;
    }
    int t = pl_ia_tab_at(group, x0);
    return t >= 0 && t == pl_ia_tab_at(group, x1) ? t : -1;
}

#define ACT_GAP 4

void pl_nav_act_rect(int group, int n, int i, int *x0, int *x1)
{
    *x0 = *x1 = 0;
    int left = pl_ia_tabs_width(group), span = W - left;
    if (!pl_ia_tabs(group) || n <= 0 || i < 0 || i >= n || span / n < 44 + ACT_GAP) {
        return;
    }
    *x0 = left + span * i / n + ACT_GAP;
    *x1 = left + span * (i + 1) / n;
}

int pl_nav_act_tap(int group, int n, int x0, int y0, int x1, int y1)
{
    if (!in_tabs(y0, y1)) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        int a, b;
        pl_nav_act_rect(group, n, i, &a, &b);
        if (a < b && x0 >= a && x0 < b && x1 >= a && x1 < b) {
            return i;
        }
    }
    return -1;
}

int pl_nav_modes(int live, int mode[4])
{
    if (live) {
        mode[0] = PL_MODE_LIVE, mode[1] = PL_DEMO_DRONE;
        return 2;
    }
    for (int i = 0; i < 4; i++) {
        mode[i] = PL_DEMO_METAL + i;
    }
    return 4;
}

const char *pl_nav_mode_name(int live, int mode)
{
    static const char *const DEMO[4] = {"METAL", "ACID", "DRONE", "SEQ"};
    if (mode == PL_DEMO_DRONE) {
        return "DRONE";
    }
    if (live) {
        return mode == PL_MODE_LIVE ? "LIVE" : "";
    }
    return mode >= 0 && mode < 4 ? DEMO[mode] : "";
}

// Плитки 212 x 104 в два столбца, как в макете.
#define MODE_X 24
#define MODE_Y 100
#define MODE_DX 220
#define MODE_DY 124
#define MODE_W 212
#define MODE_H 104

void pl_nav_mode_rect(int n, int i, int *x, int *y, int *w, int *h)
{
    (void)n;
    *x = MODE_X + (i % 2) * MODE_DX;
    *y = MODE_Y + (i / 2) * MODE_DY;
    *w = MODE_W;
    *h = MODE_H;
}

int pl_nav_mode_at(int n, int x, int y)
{
    for (int i = 0; i < n; i++) {
        int rx, ry, rw, rh;
        pl_nav_mode_rect(n, i, &rx, &ry, &rw, &rh);
        if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
            return i;
        }
    }
    return -1;
}
