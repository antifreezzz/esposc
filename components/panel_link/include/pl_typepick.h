#pragma once

// Выбор типа синта сеткой на панели: тап по TYPE синта или слоя открывает сетку типов вместо крутилки
// (26 типов листать пальцем неудобно). Энкодер BSP крутит TYPE как раньше: это путь синта, не панели.
// Раскладка в координатах экрана 480 x 480 (шапка 40 сверху), одна таблица и для отрисовки, и для касаний.

#define PL_TP_COLS 4
#define PL_TP_ROWS 7

int pl_tp_on(int page, int enc);       // 1 - ручка выбирается сеткой (TYPE синта 1-2 или слоя)
int pl_tp_count(int page);             // клеток с типами
int pl_tp_type(int page, int slot);    // тип в клетке slot, -1 - пусто
int pl_tp_slot(int page, int type);    // клетка типа, -1 - такого типа у страницы нет
void pl_tp_rect(int slot, int *x, int *y, int *w, int *h);
int pl_tp_at(int x, int y);            // клетка под точкой, -1 мимо
