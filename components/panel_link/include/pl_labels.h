#pragma once
#include <stdint.h>

// Подписи ручек панели по-английски, капителью. enc - индекс энкодера BSP с 0 (0 - страница, на круге нет).
// page_raw - сырые значения страницы: энкодер 2 (индекс 1) - тип синта на 6-7 и модель перкуссии на 3-5.

const char *pl_page_name(int page); // NULL вне 0..9
// NULL - пустой слот.
const char *pl_knob_label(int page, int enc, const uint8_t page_raw[16]);
// Ход ручки: 0..127 или границы списка, которые синт принимает.
void pl_knob_range(int page, int enc, const uint8_t page_raw[16], int *lo, int *hi);
// Слово для списков, сдвиг со знаком для транспозиций, иначе число. Возвращает buf или константу.
const char *pl_value_text(int page, int enc, int v, const uint8_t page_raw[16], char buf[12]);
// 1 - подписи страницы зависят от энкодера 2 (тип синта, модель перкуссии): при его смене перестроить круг.
int pl_labels_depend(int page);
