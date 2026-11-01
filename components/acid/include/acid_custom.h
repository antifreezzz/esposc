#pragma once
#include <stddef.h>
#include <stdint.h>

#include "acid_synth.h"

// Свои страницы: 6 штук на энкодере 1 BSP (10..15), слоты - энкодеры 2..16 (индексы 1..15).
// Слот ведёт до 4 целей на штатных страницах. Одна цель с ходом 0..127 - псевдоним (показывает живое
// значение цели), иначе макро: своё значение, цели получают lo + (hi - lo) * v / 127.

#define ACID_USER_PAGES 6
#define ACID_USER_FIRST ACID_PAGES // своя страница up - страница ACID_USER_FIRST + up
#define ACID_USER_TARGETS 4
#define ACID_CUSTOM_NONE 0xFF

typedef struct {
    uint8_t page, enc, lo, hi; // page ACID_CUSTOM_NONE - пусто; enc с 0
} acid_target_t;

typedef struct {
    acid_target_t t[ACID_USER_PAGES][ACID_KNOBS][ACID_USER_TARGETS]; // [.][0][.] не используется
    uint8_t val[ACID_USER_PAGES][ACID_KNOBS];                       // своё значение слота
    int save;                                                       // правка: пора записать во flash
} acid_custom_t;

#define ACID_CUSTOM_BLOB (4 + sizeof(((acid_custom_t *)0)->t) + sizeof(((acid_custom_t *)0)->val))

void acid_custom_init(acid_custom_t *c);
// Цель idx слота enc своей страницы up: page 0..9, tenc 1..15, тип синта (энкодер 2 страниц 6-7) нельзя.
// idx - заменить существующую или добавить следом. page ACID_CUSTOM_NONE - убрать цель idx, остальные
// сдвигаются вверх. 1 - принято, 0 - отвергнуто.
int acid_custom_edit(acid_custom_t *c, int up, int enc, int idx, int page, int tenc, int lo, int hi);
int acid_custom_count(const acid_custom_t *c, int up, int enc);
int acid_custom_is_alias(const acid_custom_t *c, int up, int enc);
int acid_custom_map(const acid_target_t *t, int v);
// Значение слота для экрана: у псевдонима - живое значение цели, иначе своё.
int acid_custom_value(const acid_custom_t *c, const acid_synth_t *s, int up, int enc);
size_t acid_custom_export(const acid_custom_t *c, void *buf, size_t cap);
// 1 - применено, 0 - чужой или битый blob (c не меняется).
int acid_custom_import(acid_custom_t *c, const void *buf, size_t len);
