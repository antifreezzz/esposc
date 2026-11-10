#include "pl_ia.h"

#include <stddef.h>

#include "pl_proto.h"

#define MAX_TABS 7

typedef struct {
    const char *name;
    short ntabs, width; // width - сколько полосы занимают вкладки (правее - действие страницы)
    pl_tab_t tab[MAX_TABS];
} group_t;

static const group_t GROUP[PL_GROUPS] = {
    [PL_GRP_PERF] = {"PERF", 0, 0, {{0}}},
    [PL_GRP_DRUMS] = {"DRUMS", 7, 480,
                      {{"KIT", PL_TAB_PAGE, 1}, {"CYM", PL_TAB_PAGE, 2}, {"TOMS", PL_TAB_PAGE, 3},
                       {"RIM", PL_TAB_PAGE, 4}, {"BELL", PL_TAB_PAGE, 5}, {"GRIDS", PL_TAB_GRIDS, PL_PAGE_GRIDS},
                       {"MIX", PL_TAB_PAGE, PL_PAGE_MIX_DRUMS}}},
    [PL_GRP_SYN1] = {"SYN 1", 4, 400,
                     {{"SOUND", PL_TAB_PAGE, PL_PAGE_SYNTH1}, {"LAYER", PL_TAB_PAGE, PL_PAGE_LAYER1},
                      {"SEQ", PL_TAB_SEQ, 0}, {"MIX", PL_TAB_PAGE, PL_PAGE_MIX_SYN1}}},
    [PL_GRP_SYN2] = {"SYN 2", 4, 400,
                     {{"SOUND", PL_TAB_PAGE, PL_PAGE_SYNTH1 + 1}, {"LAYER", PL_TAB_PAGE, PL_PAGE_LAYER1 + 1},
                      {"SEQ", PL_TAB_SEQ, 1}, {"MIX", PL_TAB_PAGE, PL_PAGE_MIX_SYN1 + 1}}},
    [PL_GRP_FX] = {"FX", 5, 345,
                   {{"FX", PL_TAB_PAGE, 8}, {"TONAL", PL_TAB_PAGE, 9}, {"MIX", PL_TAB_PAGE, 0},
                    {"USER", PL_TAB_USER, PL_USER_FIRST}, {"PETRI", PL_TAB_PAGE, PL_PAGE_PETRI}}},
    [PL_GRP_MOD] = {"MOD", 4, 480,
                    {{"M1", PL_TAB_PAGE, PL_PAGE_MOD1}, {"M2", PL_TAB_PAGE, PL_PAGE_MOD1 + 1},
                     {"M3", PL_TAB_PAGE, PL_PAGE_MOD1 + 2}, {"M4", PL_TAB_PAGE, PL_PAGE_MOD1 + 3}}},
};

static const group_t *grp(int group)
{
    return group >= 0 && group < PL_GROUPS ? &GROUP[group] : NULL;
}

const char *pl_ia_group_name(int group)
{
    const group_t *g = grp(group);
    return g ? g->name : "";
}

int pl_ia_group_on(int group)
{
    return grp(group) != NULL;
}

int pl_ia_tabs(int group)
{
    const group_t *g = grp(group);
    return g ? g->ntabs : 0;
}

const pl_tab_t *pl_ia_tab(int group, int tab)
{
    const group_t *g = grp(group);
    return g && tab >= 0 && tab < g->ntabs ? &g->tab[tab] : NULL;
}

int pl_ia_find(int page, int *group, int *tab)
{
    for (int g = 0; g < PL_GROUPS; g++) {
        for (int t = 0; t < GROUP[g].ntabs; t++) {
            const pl_tab_t *x = &GROUP[g].tab[t];
            int hit = x->kind == PL_TAB_USER ? page >= PL_USER_FIRST && page < PL_USER_FIRST + PL_USER_PAGES
                                             : x->kind != PL_TAB_SEQ && x->page == page;
            if (hit) {
                *group = g, *tab = t;
                return 1;
            }
        }
    }
    return 0;
}

int pl_ia_tab_bsp(int group, int tab, int user)
{
    const pl_tab_t *x = pl_ia_tab(group, tab);
    if (!x || x->kind == PL_TAB_SEQ) {
        return -1;
    }
    return pl_page_bsp(x->kind == PL_TAB_USER ? x->page + user : x->page);
}

int pl_ia_tabs_width(int group)
{
    const group_t *g = grp(group);
    return g ? g->width : 0;
}

void pl_ia_tab_rect(int group, int tab, int *x0, int *x1)
{
    const group_t *g = grp(group);
    *x0 = *x1 = 0;
    if (g && tab >= 0 && tab < g->ntabs) {
        *x0 = g->width * tab / g->ntabs;
        *x1 = g->width * (tab + 1) / g->ntabs;
    }
}

int pl_ia_tab_at(int group, int x)
{
    const group_t *g = grp(group);
    if (!g || !g->ntabs || x < 0 || x >= g->width) {
        return -1;
    }
    for (int t = 0; t < g->ntabs; t++) { // теми же границами, что pl_ia_tab_rect: ширина делится не нацело
        if (x < g->width * (t + 1) / g->ntabs) {
            return t;
        }
    }
    return -1;
}

void pl_ia_dock_rect(int group, int *x0, int *x1)
{
    *x0 = *x1 = 0;
    if (grp(group)) {
        *x0 = group * PL_IA_DOCK_W;
        *x1 = *x0 + PL_IA_DOCK_W;
    }
}

int pl_ia_dock_at(int x)
{
    return x >= 0 && x < PL_GROUPS * PL_IA_DOCK_W ? x / PL_IA_DOCK_W : -1;
}
