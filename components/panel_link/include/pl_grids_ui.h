#pragma once

// Экран Grids на панели: раскладка в координатах экрана 480 x 480 (шапка 40 сверху), попадание пальцем
// и какая ручка страницы Grids стоит за элементом. Одна таблица и для отрисовки, и для касаний.

enum {
    PL_GUI_NONE,
    PL_GUI_ON,     // вкл/выкл, тап
    PL_GUI_FILL,   // сбивка, пока держишь
    PL_GUI_HAT,    // сегменты CLOSED OPEN MIX
    PL_GUI_TIME,   // сегменты HAT RIDE
    PL_GUI_EVERY,  // сегменты OFF 4 8 16
    PL_GUI_LEN,    // сегменты 1 2 4
    PL_GUI_SLIDER, // KICK SNARE HAT CHAOS SWING
    PL_GUI_XY,     // карта: X и Y; у банка и своих на её месте кнопка редактора
    PL_GUI_SRC,    // источник рисунка: тап открывает сетку выбора
    PL_GUI_GRID,   // сетка шагов: тап открывает редактор
    PL_GUI_IDS
};

// Прямоугольник элемента id (idx - сегмент или ползунок). 0 - нет такого.
int pl_gui_rect(int id, int idx, int *x, int *y, int *w, int *h);
// Что под точкой; в arg - сегмент или ползунок.
int pl_gui_at(int x, int y, int *arg);
// Индекс enc ручки страницы Grids за элементом; у XY - 0 (две ручки, 2 и 3).
int pl_gui_enc(int id, int idx);
// Значение ползунка по y пальца: верх 127, низ 0.
int pl_gui_slider_value(int y);
// Свинг по y пальца на своём ползунке: низ 0, верх 3.
int pl_gui_swing_value(int y);
// Клетка сетки шагов: шаг 0..31, дорожка 0..2 (бочка, малый, хэт). 0 - вне сетки.
int pl_gui_cell(int step, int part, int *x, int *y, int *w, int *h);
// То же с сеткой рисунка (pl_grids.h): у триольной 24 клетки пошире, слоты 12-15 такта - 0.
int pl_gui_cell_at(int grid, int step, int part, int *x, int *y, int *w, int *h);

// Редактор своего рисунка поверх экрана Grids: один такт крупно (16 или 12 колонок на 3 дорожки).
enum {
    PL_GED_NONE,
    PL_GED_BACK,  // закрыть
    PL_GED_SLOT,  // U1..U4
    PL_GED_BAR,   // такт 1 / 2
    PL_GED_TRIP,  // сетка 16 / 12T
    PL_GED_COPY,  // скопировать источник, с которого пришли
    PL_GED_CLEAR, // очистить рисунок
    PL_GED_CELL,
    PL_GED_IDS
};
int pl_ged_rect(int id, int idx, int *x, int *y, int *w, int *h); // кнопки; у клеток - pl_ged_cell
int pl_ged_cell(int grid, int col, int part, int *x, int *y, int *w, int *h);
// Что под точкой; у клетки arg = дорожка * 16 + колонка. Клетки ловят и щели между собой.
int pl_ged_at(int grid, int x, int y, int *arg);
// Уровень клетки после тапа: пусто -> удар -> акцент -> гост -> пусто.
int pl_ged_next(int lv);
