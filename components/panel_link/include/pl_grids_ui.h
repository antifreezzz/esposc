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
    PL_GUI_SLIDER, // KICK SNARE HAT CHAOS
    PL_GUI_XY,     // карта: X и Y
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
// Клетка сетки шагов: шаг 0..31, дорожка 0..2 (бочка, малый, хэт). 0 - вне сетки.
int pl_gui_cell(int step, int part, int *x, int *y, int *w, int *h);
