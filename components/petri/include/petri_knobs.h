#pragma once
#include <stdint.h>

#include "petri.h"
#include "pl_proto.h"

// Ручки синта (зеркало панели) -> параметры чашки. Синт типа PETRI: синт 1 главнее; его страница - рельеф, контраст,
// орбита, центр; страница «Чашка» (PL_PAGE_PETRI) - колония, жители, пересев. Численность применяется только при
// смене ручки: вымершие пожиратели не воскресают каждый кадр.
typedef struct {
    uint8_t last[PL_KNOBS]; // прежние значения страницы «Чашка»
    int have;               // last заполнен
    int steps;              // шагов на кадр
} petri_knobs_t;

void petri_knobs_init(petri_knobs_t *k);
// Номер синта PETRI (0 или 1), -1 - ни один не PETRI (чашка не нужна, ничего не применено).
int petri_knobs_apply(petri_t *p, petri_knobs_t *k, const uint8_t raw[PL_PAGES][PL_KNOBS]);
