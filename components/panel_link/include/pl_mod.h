#pragma once
#include <stdint.h>

#include "pl_model.h"
#include "pl_proto.h"

// Модуляторы на панели: что можно сделать целью, правка целей и действующее значение ручки под модуляцией.
// Числа те же, что у синта (acid_mod.c): тест сравнивает обе стороны.

// Ручка page/enc годится целью: страницы 0-8, слои 10-11 и Grids 12, не пустой слот, плавная (не список, не
// переключатель и не триггер) - по типу синта в сырых значениях модели. Это pl_pick_view == PL_PICK_NONE
// плюс «слот не пуст» и границы страниц, как acid_synth_knob_smooth.
int pl_mod_can_target(const pl_model_t *m, int page, int enc);

// Сдвиг одной цели: out - выход модулятора int8 (-127..127 = -1..1), amount - AMOUNT, depth - ручка DEPTH (64 - ноль).
// round(out * amount / 127 * (depth - 64) * 2), считается тем же порядком float, что у синта.
int pl_mod_offset(int out, int amount, int depth);
// Сумма сдвигов всех модуляторов, чьи цели - ручка page/enc (учитываются только допустимые цели);
// *n - сколько целей на неё назначено (можно NULL).
int pl_mod_sum(const pl_model_t *m, int page, int enc, int *n);
// Действующее значение ручки: база из модели (синт шлёт именно базу) + сумма сдвигов, зажатая в 0..127.
// Вне страниц и энкодеров 0. Выход приходит int8 (~20 Гц), поэтому у точки на дуге погрешность до единицы.
int pl_mod_effective(const pl_model_t *m, int page, int enc);

// На странице есть хоть одна ручка, которую можно сделать целью (TONAL, чашка и выключенный слой - нет).
int pl_mod_page_has_target(const pl_model_t *m, int page);
// Целей у модулятора k (0..3), 0 вне диапазона.
int pl_mod_count(const pl_model_t *m, int k);
// Поставить цель idx модулятора k (page PL_NONE - убрать) в модели и собрать кадр PL_MOD_EDIT для синта.
// 0 - кадра модулятора ещё не было, вне границ или ручка не годится целью.
int pl_mod_edit(pl_model_t *m, int k, int idx, int page, int enc, pl_mod_edit_t *out);
