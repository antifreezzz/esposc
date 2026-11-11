#pragma once
#include <stdint.h>

#include "pl_model.h"

// Выбор значения ручки на панели. Списки (TYPE, лад, время дилэя, транспозиция и прочие до 26 значений) - тап
// открывает сетку значений, сдвиг пальца крутит, как раньше. Переключатели и триггеры - тапом, без крутилки.
// Энкодер BSP крутит всё как раньше: это путь синта, не панели.
// Раскладка сетки в координатах экрана 480 x 480 (шапка 40 сверху), одна таблица и для отрисовки, и для касаний.

enum { PL_PICK_NONE, PL_PICK_GRID, PL_PICK_TOGGLE, PL_PICK_TRIGGER };

#define PL_PICK_MAX 26 // больше значений сеткой не выбирают

int pl_pick_kind(int page, int enc, const uint8_t raw[16]);
// Своя страница: класс цели у псевдонима (одна цель на весь ход), у макро - NONE. tp, te - куда смотреть значения.
int pl_pick_view(const pl_model_t *m, int page, int enc, int *tp, int *te);
int pl_pick_count(int page, int enc, const uint8_t raw[16]);               // значений в сетке, 0 - не сетка
int pl_pick_value(int page, int enc, const uint8_t raw[16], int slot);     // значение клетки, -1 - нет такой
int pl_pick_slot(int page, int enc, const uint8_t raw[16], int v);         // клетка значения, -1 - нет в сетке
int pl_pick_next(int page, int enc, const uint8_t raw[16], int v);         // переключатель: значение после тапа
// 1 - палец может захватить ручку крутилкой. hold 1 - захват по удержанию, 0 - по сдвигу пальца.
// У списков удержание - это тап (открыть сетку), у переключателей, триггеров и TYPE крутилки нет.
// И у ручки с подэкраном удержание - тап: иначе дрожание пальца крутит её вместо входа.
int pl_pick_grab(int page, int enc, const uint8_t raw[16], int hold);
// Подэкран за ручкой (тап открывает его, сдвиг пальца крутит ручку): FX 16 REVERB -> PL_PAGE_REVERB; -1 - нет.
int pl_pick_sub(int page, int enc);

void pl_pick_rect(int n, int slot, int *x, int *y, int *w, int *h); // клетка slot сетки из n значений
int pl_pick_at(int n, int x, int y);                                // клетка под точкой, -1 мимо
