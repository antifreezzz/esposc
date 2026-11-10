#pragma once
#include <stdint.h>

// Панель по инструментам: док из групп, у группы вкладки, вкладка - страница ручек или свой экран.
// Одна таблица и для отрисовки, и для касаний, и для энкодера 1 BSP (страница -> группа и вкладка).

#define PL_IA_TOP_H 40  // шапка
#define PL_IA_TABS_H 36 // полоса вкладок под шапкой
#define PL_IA_DOCK_H 48 // док внизу
#define PL_IA_ROW_H 70  // шаг ряда карточек: 5 рядов между вкладками и доком
#define PL_IA_DOCK_W 80 // кнопка дока

enum { PL_GRP_PERF, PL_GRP_DRUMS, PL_GRP_SYN1, PL_GRP_SYN2, PL_GRP_FX, PL_GRP_MOD, PL_GROUPS };
enum {
    PL_TAB_PAGE,  // сетка карточек страницы page
    PL_TAB_GRIDS, // экран Grids (page - его страница ручек)
    PL_TAB_SEQ,   // экран секвенсера, page - номер синта 0..1
    PL_TAB_USER,  // свои страницы: page - первая, какая открыта - помнит экран
};

typedef struct {
    const char *name;
    uint8_t kind, page;
} pl_tab_t;

const char *pl_ia_group_name(int group);
int pl_ia_group_on(int group); // 0 - кнопка дока тусклая и не нажимается (MOD до модуляторов)
int pl_ia_tabs(int group);     // число вкладок, 0 - группа из одного экрана
const pl_tab_t *pl_ia_tab(int group, int tab); // NULL - нет такой
// Где живёт страница page (внутренний номер): 1 - нашлась. Своя страница - вкладка USER.
int pl_ia_find(int page, int *group, int *tab);
// Номер вкладки на экране BSP, -1 - нет; user - какая своя страница открыта (0..5), для остальных не важен.
int pl_ia_tab_bsp(int group, int tab, int user);

// Полоса вкладок: занимает 0..width, правее - действие страницы (DISH, XY, EDIT).
int pl_ia_tabs_width(int group);
void pl_ia_tab_rect(int group, int tab, int *x0, int *x1);
int pl_ia_tab_at(int group, int x); // -1 мимо
void pl_ia_dock_rect(int group, int *x0, int *x1);
int pl_ia_dock_at(int x); // группа под x, -1 мимо
