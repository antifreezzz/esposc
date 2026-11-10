#pragma once
#include <stdint.h>

#include "pl_labels.h"
#include "pl_model.h"

// Что рисовать в ячейке сетки для любой страницы 0..15. Штатные - подписи и значения ручек,
// свои: псевдоним показывает цель (подпись, значение, цвет), макро - подпись первой цели с +N и своё 0..127.

typedef struct {
    const char *label; // NULL - пустой слот
    const char *text;  // значение словом или числом
    const char *src;   // своя страница: откуда цели ("SYNTH 1", "2 PAGES"), у штатных NULL
    uint32_t src_color;
    int v, lo, hi, bipolar, held;
    int skip; // значение внутри хода, которого у ручки нет (pl_knob_skip), -1 - нет
    uint32_t color;
    char lbuf[20], vbuf[12], sbuf[12]; // label и text могут указывать сюда: структуру не копировать
} pl_cell_t;

void pl_view_cell(const pl_model_t *m, int page, int enc, pl_cell_t *c);
const char *pl_view_page_name(int page); // NULL вне 0..15
uint32_t pl_view_page_color(int page);
// Соседняя страница по кругу внутри своей группы: штатные 0..9, свои 10..15.
int pl_view_page_step(int page, int dir);
// Цель i слота своей страницы: её страница 0..9 и энкодер в *enc, -1 - нет такой.
int pl_view_target(const pl_model_t *m, int page, int enc, int i, int *tenc);
// На странице есть кнопка перехода к экрану чашки: страница её ручек или синт с типом PETRI.
int pl_view_dish(const pl_model_t *m, int page);
// Слот вкладки MIX инструмента (PL_PAGE_MIX_*): его цель на штатной странице по типам синта и слоя из модели.
// 1 - цель есть; 0 - страница не MIX или слот пуст. Правки слота слать в цель.
int pl_view_mix(const pl_model_t *m, int page, int enc, int *tpage, int *tenc);
// Слот своей страницы - псевдоним (одна цель на весь ход).
int pl_view_is_alias(const pl_model_t *m, int up, int enc);
// Статус в шапке: "NO LINK", "DRONE", "LIVE", в демо - имя песни ("METAL", "ACID"; чужой номер - "DEMO").
const char *pl_view_status(const pl_model_t *m, int linked);
