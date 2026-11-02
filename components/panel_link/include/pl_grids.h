#pragma once
#include <stdint.h>

// Генератор барабанов Grids (Mutable Instruments), порт режима барабанов.
// Copyright 2011-2012 Emilie Gillet, GPL v3 or later; прошивка целиком под GPLv3 (см. LICENSE).
// Общий код панели и синта: панель считает превью сетки сама.

#define PL_GRIDS_STEPS 32 // два такта шестнадцатых
#define PL_GRIDS_PARTS 3  // 0 бочка, 1 малый, 2 хэт
#define PL_GRIDS_ACCENT 192

// Карта 5x5 узлов по 96 байт (дорожка * 32 + шаг): индексы [x >> 6][y >> 6]. Лежит в pl_grids_map.c.
extern const uint8_t *const pl_grids_drum_map[5][5];

// Ручка 0..127 -> 0..255: v << 1 | v >> 6.
uint8_t pl_grids_u8(int v);
// Уровень дорожки part (0..2) на шаге step (0..31) в точке карты x, y (ReadDrumMap оригинала).
uint8_t pl_grids_level(uint8_t x, uint8_t y, int part, int step);
// Биты 0-2 - удар дорожки, 3-5 - акцент. density и perturb по дорожкам, 0..255.
uint8_t pl_grids_step(uint8_t x, uint8_t y, const uint8_t density[3], const uint8_t perturb[3], int step);
// Превью по ручкам страницы Grids (индексы enc: 2 X, 3 Y, 4-6 плотности), без хаоса: маска на 32 шага.
void pl_grids_preview(const uint8_t knob[16], uint8_t out[PL_GRIDS_STEPS]);
