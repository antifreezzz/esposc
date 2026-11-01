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
// Слот своей страницы - псевдоним (одна цель на весь ход).
int pl_view_is_alias(const pl_model_t *m, int up, int enc);
