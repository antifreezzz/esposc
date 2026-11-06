#include "pl_nav.h"

#define TOP_H 40
#define SLIP 16 // палец можно увести вниз за шапку, как у плиток

typedef struct {
    short nav, x0, x1;
} btn_t;

// Кнопки по видам шапки; NONE закрывает список. Заголовок - отдельно, между кнопками.
static const btn_t BTN[PL_BARS][6] = {
    [PL_BAR_MENU] = {{PL_NAV_TAB_PAGES, 132, 236}, {PL_NAV_TAB_USER, 244, 348}, {PL_NAV_STATUS, 404, 480}},
    [PL_BAR_PAGE] = {{PL_NAV_MENU, 4, 84}, {PL_NAV_PREV, 92, 140}, {PL_NAV_NEXT, 300, 348}, {PL_NAV_ACTION, 410, 476}},
    [PL_BAR_USER] = {{PL_NAV_MENU, 4, 84},
                     {PL_NAV_PREV, 92, 140},
                     {PL_NAV_NEXT, 300, 348},
                     {PL_NAV_XY, 354, 404},
                     {PL_NAV_ACTION, 410, 476}},
    [PL_BAR_SCREEN] = {{PL_NAV_MENU, 4, 84}, {PL_NAV_ACTION, 396, 476}},
};
static const short TITLE[PL_BARS][2] = {
    [PL_BAR_MENU] = {0, 0}, [PL_BAR_PAGE] = {140, 300}, [PL_BAR_USER] = {140, 300}, [PL_BAR_SCREEN] = {92, 388}};

int pl_nav_at(int bar, int x)
{
    if (bar < 0 || bar >= PL_BARS) {
        return PL_NAV_NONE;
    }
    for (const btn_t *b = BTN[bar]; b < BTN[bar] + 6 && b->nav != PL_NAV_NONE; b++) {
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
    for (const btn_t *b = BTN[bar]; b < BTN[bar] + 6 && b->nav != PL_NAV_NONE; b++) {
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
    if (y0 >= TOP_H || y1 >= TOP_H + SLIP) {
        return PL_NAV_NONE;
    }
    int a = pl_nav_at(bar, x0);
    return a == pl_nav_at(bar, x1) ? a : PL_NAV_NONE;
}
