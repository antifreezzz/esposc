#pragma once
#include <stdint.h>

#include "pl_proto.h"

// Подписи ручек панели по-английски, капителью. enc - индекс энкодера BSP с 0 (0 - страница, на круге нет).
// page_raw - сырые значения страницы: энкодер 2 (индекс 1) - тип синта на 6-7 и модель перкуссии на 3-5.

const char *pl_page_name(int page); // NULL вне 0..12
// NULL - пустой слот.
const char *pl_knob_label(int page, int enc, const uint8_t page_raw[16]);
// Ход ручки: 0..127 или границы списка, которые синт принимает.
void pl_knob_range(int page, int enc, const uint8_t page_raw[16], int *lo, int *hi);
// Значение внутри хода, которого у ручки нет (крутилка перескакивает), -1 - нет такого. TYPE слоя: 25 (PETRI).
int pl_knob_skip(int page, int enc, const uint8_t page_raw[16]);
// Слово для списков, сдвиг со знаком для транспозиций, иначе число. Возвращает buf или константу.
const char *pl_value_text(int page, int enc, int v, const uint8_t page_raw[16], char buf[12]);
// 1 - подписи страницы зависят от энкодера 2 (тип синта, модель перкуссии): при его смене перестроить круг.
int pl_labels_depend(int page);
// Акцентный цвет страницы (RGB24) для меню и шапки; 0 вне 0..9.
uint32_t pl_page_color(int page);
// Цвет смысловой группы ручки (RGB24); 0 для пустого слота.
uint32_t pl_knob_color(int page, int enc, const uint8_t page_raw[16]);
// 1 - биполярная ручка с центром в 64 (панорама, DJ-фильтр, транспозиция, тон перегруза, огибающие Plaits).
int pl_knob_bipolar(int page, int enc, const uint8_t page_raw[16]);
// Краткая сводка состояния страницы для плитки меню и центра круга. Возвращает buf или константу.
const char *pl_page_summary(int page, const uint8_t page_raw[16], char buf[24]);
// Имя своей страницы up 0..5 ("USER 1".."USER 6"), NULL вне.
const char *pl_user_name(int up);
// Подпись слота своей страницы: подпись первой цели по raw её страницы и " +N" у слота с N + 1 целями.
// NULL - пустой слот. Возвращает buf или константу.
const char *pl_slot_label(const pl_target_t t[PL_TARGETS], const uint8_t raw[PL_PAGES][PL_KNOBS], char buf[20]);
