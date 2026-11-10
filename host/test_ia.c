// Панель по инструментам: группы дока, вкладки, страница вкладки и обратно.
#include <string.h>

#include "pl_ia.h"
#include "pl_proto.h"
#include "test.h"

static int tab_named(int g, const char *name)
{
    for (int t = 0; t < pl_ia_tabs(g); t++) {
        if (!strcmp(pl_ia_tab(g, t)->name, name)) {
            return t;
        }
    }
    return -1;
}

TEST(test_groups)
{
    CHECK(PL_GROUPS == 6);
    CHECK(!strcmp(pl_ia_group_name(PL_GRP_PERF), "PERF") && !strcmp(pl_ia_group_name(PL_GRP_SYN2), "SYN 2"));
    CHECK(pl_ia_tabs(PL_GRP_PERF) == 0 && pl_ia_tabs(PL_GRP_MOD) == 0); // PERF - один экран, MOD - позже
    CHECK(pl_ia_tabs(PL_GRP_DRUMS) == 7 && pl_ia_tabs(PL_GRP_SYN1) == 4 && pl_ia_tabs(PL_GRP_SYN2) == 4);
    CHECK(pl_ia_tabs(PL_GRP_FX) == 5);
    CHECK(pl_ia_tab(PL_GRP_PERF, 0) == NULL && pl_ia_tab(PL_GRP_FX, 5) == NULL && pl_ia_tab(-1, 0) == NULL);
    CHECK(pl_ia_group_on(PL_GRP_PERF) && pl_ia_group_on(PL_GRP_FX) && !pl_ia_group_on(PL_GRP_MOD));
}

TEST(test_tab_pages)
{
    const pl_tab_t *t = pl_ia_tab(PL_GRP_SYN1, 0);
    CHECK(!strcmp(t->name, "SOUND") && t->kind == PL_TAB_PAGE && t->page == PL_PAGE_SYNTH1);
    t = pl_ia_tab(PL_GRP_SYN2, 1);
    CHECK(!strcmp(t->name, "LAYER") && t->page == PL_PAGE_LAYER1 + 1);
    t = pl_ia_tab(PL_GRP_SYN2, tab_named(PL_GRP_SYN2, "SEQ"));
    CHECK(t->kind == PL_TAB_SEQ && t->page == 1); // у SEQ в page - номер синта
    t = pl_ia_tab(PL_GRP_SYN1, tab_named(PL_GRP_SYN1, "MIX"));
    CHECK(t->kind == PL_TAB_PAGE && t->page == PL_PAGE_MIX_SYN1);
    t = pl_ia_tab(PL_GRP_DRUMS, tab_named(PL_GRP_DRUMS, "GRIDS"));
    CHECK(t->kind == PL_TAB_GRIDS && t->page == PL_PAGE_GRIDS);
    t = pl_ia_tab(PL_GRP_DRUMS, tab_named(PL_GRP_DRUMS, "MIX"));
    CHECK(t->page == PL_PAGE_MIX_DRUMS);
    t = pl_ia_tab(PL_GRP_FX, tab_named(PL_GRP_FX, "MIX"));
    CHECK(t->kind == PL_TAB_PAGE && t->page == 0);
    t = pl_ia_tab(PL_GRP_FX, tab_named(PL_GRP_FX, "USER"));
    CHECK(t->kind == PL_TAB_USER && t->page == PL_USER_FIRST);
    CHECK(PL_PAGE_MIX_DRUMS == PL_USER_FIRST + PL_USER_PAGES && PL_PAGE_MIX_SYN1 == PL_PAGE_MIX_DRUMS + 1);
}

// Любая страница синта находится ровно в одном месте: энкодер 1 BSP открывает группу и вкладку.
TEST(test_find_every_page)
{
    int seen[PL_GROUPS][8] = {{0}};
    for (int page = 0; page < PL_PAGE_MIX_DRUMS + PL_MIX_PAGES; page++) {
        int g = -1, t = -1;
        CHECK(pl_ia_find(page, &g, &t));
        const pl_tab_t *tab = pl_ia_tab(g, t);
        CHECK(tab != NULL);
        if (!tab) {
            continue;
        }
        if (tab->kind == PL_TAB_USER) {
            CHECK(page >= PL_USER_FIRST && page < PL_USER_FIRST + PL_USER_PAGES);
        } else {
            CHECK(tab->page == page && (tab->kind == PL_TAB_PAGE || tab->kind == PL_TAB_GRIDS));
            CHECK(!seen[g][t]++);
        }
    }
    int g, t;
    CHECK(pl_ia_find(PL_PAGE_SYNTH1, &g, &t) && g == PL_GRP_SYN1 && t == 0);
    CHECK(pl_ia_find(PL_PAGE_LAYER1 + 1, &g, &t) && g == PL_GRP_SYN2 && t == 1);
    CHECK(pl_ia_find(PL_PAGE_PETRI, &g, &t) && g == PL_GRP_FX);
    CHECK(pl_ia_find(PL_USER_FIRST + 3, &g, &t) && g == PL_GRP_FX && pl_ia_tab(g, t)->kind == PL_TAB_USER);
    CHECK(!pl_ia_find(-1, &g, &t) && !pl_ia_find(99, &g, &t));
}

// Номер на вкладке - как на экране BSP; у вкладок без страницы BSP его нет.
TEST(test_tab_bsp_number)
{
    CHECK(pl_ia_tab_bsp(PL_GRP_SYN1, 0, 0) == 6 && pl_ia_tab_bsp(PL_GRP_SYN1, 1, 0) == 16);
    CHECK(pl_ia_tab_bsp(PL_GRP_SYN2, 1, 0) == 17);
    CHECK(pl_ia_tab_bsp(PL_GRP_DRUMS, tab_named(PL_GRP_DRUMS, "GRIDS"), 0) == -1);
    CHECK(pl_ia_tab_bsp(PL_GRP_DRUMS, tab_named(PL_GRP_DRUMS, "MIX"), 0) == -1);
    CHECK(pl_ia_tab_bsp(PL_GRP_SYN1, tab_named(PL_GRP_SYN1, "SEQ"), 0) == -1);
    int u = tab_named(PL_GRP_FX, "USER");
    CHECK(pl_ia_tab_bsp(PL_GRP_FX, u, 0) == 10 && pl_ia_tab_bsp(PL_GRP_FX, u, 5) == 15);
    CHECK(pl_ia_tab_bsp(PL_GRP_FX, tab_named(PL_GRP_FX, "PETRI"), 0) == 18);
}

// Полосы: вкладки и док под палец, без дыр внутри полосы вкладок, действие справа не задето.
TEST(test_strips)
{
    for (int g = 0; g < PL_GROUPS; g++) {
        int n = pl_ia_tabs(g), w = pl_ia_tabs_width(g);
        CHECK(w <= 480);
        for (int t = 0; t < n; t++) {
            int x0, x1;
            pl_ia_tab_rect(g, t, &x0, &x1);
            CHECK(x1 - x0 >= 60 && x1 <= w);
            CHECK(pl_ia_tab_at(g, x0) == t && pl_ia_tab_at(g, x1 - 1) == t);
        }
        CHECK(pl_ia_tab_at(g, -1) == -1 && pl_ia_tab_at(g, w) == -1 && pl_ia_tab_at(g, 479) == (w == 480 && n ? n - 1 : -1));
    }
    CHECK(pl_ia_tabs_width(PL_GRP_DRUMS) == 480); // 7 вкладок: действия нет
    CHECK(pl_ia_tabs_width(PL_GRP_SYN1) <= 404);  // справа действие (DISH)
    CHECK(pl_ia_tabs_width(PL_GRP_FX) <= 348);    // справа XY и EDIT
    for (int g = 0; g < PL_GROUPS; g++) {
        int x0, x1;
        pl_ia_dock_rect(g, &x0, &x1);
        CHECK(x1 - x0 == 80 && pl_ia_dock_at(x0) == g && pl_ia_dock_at(x1 - 1) == g);
    }
    CHECK(pl_ia_dock_at(-1) == -1 && pl_ia_dock_at(480) == -1);
    CHECK(PL_IA_TOP_H + PL_IA_TABS_H + PL_IA_DOCK_H + 5 * PL_IA_ROW_H <= 480);
}

int main(void)
{
    RUN(test_groups);
    RUN(test_tab_pages);
    RUN(test_find_every_page);
    RUN(test_tab_bsp_number);
    RUN(test_strips);
    TEST_MAIN_END();
}
