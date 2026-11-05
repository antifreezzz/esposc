#pragma once
#include <stdint.h>

#include "petri.h"

// Цвета экрана чашки (RGB565): V x3.1 -> индекс палитры из 5 опорных точек прототипа (тёмная бирюза -> почти белый),
// след мутагена поверх: красный - распад сильнее, синий - слабее.
#define PETRI_RGB565(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

void petri_palette(uint16_t lut[256]);
uint16_t petri_cell_color(const uint16_t lut[256], int v, int dk);
