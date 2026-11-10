#pragma once
#include "pl_model.h"
#include "pl_proto.h"

// Редактор своей страницы на панели: слот -> его цели -> добавить (страница, ручка) или открыть цель ->
// ход lo/hi крутилкой. Правки уходят синту (PL_CUSTOM_EDIT), правда - его эхо PL_CUSTOM.

enum { PL_EDIT_OFF, PL_EDIT_SLOTS, PL_EDIT_TARGETS, PL_EDIT_PICK_PAGE, PL_EDIT_PICK_KNOB, PL_EDIT_RANGE };

typedef struct {
    int state, up, enc, idx, page;
    int which;          // RANGE: 0 крутим lo, 1 hi
    int mod;            // 1 - цели модулятора up (не своей страницы): TARGETS - его 4 строки, дальше PICK_PAGE и PICK_KNOB
    uint8_t pe, lo, hi; // цель в правке (до эха синта)
} pl_edit_t;

void pl_edit_start(pl_edit_t *e, int up);                 // EDIT на своей странице up -> SLOTS
void pl_edit_slot(pl_edit_t *e, int enc);                 // SLOTS: тап по карточке enc 1..15 -> TARGETS
void pl_edit_add(pl_edit_t *e, const pl_model_t *m);      // TARGETS: «+», если целей меньше 4 -> PICK_PAGE
int pl_edit_open(pl_edit_t *e, const pl_model_t *m, int idx); // TARGETS: цель idx -> RANGE; 0 - нет цели
void pl_edit_page(pl_edit_t *e, int page);                // PICK_PAGE: штатная 0..9 -> PICK_KNOB
// PICK_KNOB: ручка enc страницы -> RANGE, out - новая цель с ходом ручки целиком. 0 - пусто или тип синта.
int pl_edit_knob(pl_edit_t *e, const pl_model_t *m, int enc, pl_custom_edit_t *out);
// RANGE: v в lo или hi (по which), зажато в ход ручки. 1 - out слать синту.
int pl_edit_range(pl_edit_t *e, const pl_model_t *m, int v, pl_custom_edit_t *out);
int pl_edit_remove(pl_edit_t *e, int idx, pl_custom_edit_t *out); // TARGETS: убрать цель idx
void pl_edit_back(pl_edit_t *e);

// Цели модулятора k: тот же ход (страница, ручка), но без слотов и хода lo/hi. Из списка целей (4 строки) назад - наружу.
void pl_edit_mod_start(pl_edit_t *e, int k);                       // TARGETS
void pl_edit_mod_row(pl_edit_t *e, const pl_model_t *m, int idx);  // TARGETS: тап по строке idx 0..3 -> PICK_PAGE
// PICK_PAGE: страница, где есть что модулировать (pl_mod_page_has_target) -> PICK_KNOB.
void pl_edit_mod_page(pl_edit_t *e, const pl_model_t *m, int page);
// PICK_KNOB: ручка enc, если годится целью -> TARGETS; out - кадр PL_MOD_EDIT, цель уже в модели. 0 - не годится.
int pl_edit_mod_knob(pl_edit_t *e, pl_model_t *m, int enc, pl_mod_edit_t *out);
int pl_edit_mod_remove(pl_edit_t *e, pl_model_t *m, int idx, pl_mod_edit_t *out); // TARGETS: убрать цель idx; 0 - пусто

#define PL_EDIT_MOD_CLEAR_MS 600
// Отпускание строки: выбрать цель, а занятую после явного удержания удалить. HOLD жеста на 50 мс действия не даёт.
enum { PL_MOD_ROW_NONE, PL_MOD_ROW_SELECT, PL_MOD_ROW_REMOVE };
int pl_edit_mod_row_action(int ev, uint32_t held_ms, int occupied);

void pl_edit_back(pl_edit_t *e); // шаг назад, из SLOTS - выход
