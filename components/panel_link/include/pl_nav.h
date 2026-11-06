#pragma once

// Шапка панели (480 x 40): видимые кнопки по виду экрана, одна таблица и для отрисовки, и для касаний.
// Тап мимо кнопок ничего не делает.

enum { PL_BAR_MENU, PL_BAR_PAGE, PL_BAR_USER, PL_BAR_SCREEN, PL_BARS };
// MENU - меню (вкладки и статус), PAGE - штатная страница, USER - своя (ещё XY), SCREEN - GRIDS, PETRI, сцены,
// редактор и сетка значений: «назад» слева, действие справа.

enum {
    PL_NAV_NONE,
    PL_NAV_MENU,   // в меню, закрыть, назад
    PL_NAV_PREV,
    PL_NAV_NEXT,
    PL_NAV_XY,
    PL_NAV_ACTION, // EDIT, DISH, KNOBS, DONE
    PL_NAV_TAB_PAGES,
    PL_NAV_TAB_USER,
    PL_NAV_STATUS, // имя демо-песни: тап - следующая
};

int pl_nav_at(int bar, int x);                     // кнопка под x, PL_NAV_NONE мимо
void pl_nav_rect(int bar, int nav, int *x0, int *x1); // x0 == x1 - у вида нет такой кнопки
void pl_nav_title(int bar, int *x0, int *x1);         // место заголовка, x0 == x1 - нет
// Тап в шапке: начат на кнопке в шапке и отпущен на той же (палец можно чуть увести вниз).
int pl_nav_tap(int bar, int x0, int y0, int x1, int y1);
