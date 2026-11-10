#pragma once
#include <stddef.h>
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

// Источник рисунка (ручка SRC страницы Grids, по числу на экране): 0 - карта, 1..BANK - банк классики,
// следом свои рисунки U1..U4. Банк и свои - те же 96 уровней, что узел карты: плотность, хаос и сбивки
// работают с ними как с картой, точка X/Y им безразлична.
#define PL_GRIDS_BANK 18
#define PL_GRIDS_USER 4
#define PL_GRIDS_SRC_MAP 0
#define PL_GRIDS_SRC_USER (1 + PL_GRIDS_BANK)
#define PL_GRIDS_SRCS (PL_GRIDS_SRC_USER + PL_GRIDS_USER)
// Сила удара у банка и своих по уровню: выше ACCENT - акцент, ниже GHOST - гост (тихий), между - обычный.
// У карты гостов нет. Убавляешь плотность - первыми уходят госты, акценты последними.
#define PL_GRIDS_GHOST 128
#define PL_GRIDS_LV_ACC 255  // 'X': звучит с плотности 1
#define PL_GRIDS_LV_HIT 176  // 'x': с плотности 40
#define PL_GRIDS_LV_GHOST 124 // 'g': с плотности 66 (умолчания 70-90 играют весь канон)
#define PL_GRIDS_LV_EXTRA 64 // ',': украшение-гост, только когда плотность выкручена (с 96; умолчание хэта 90)
enum { PL_GRIDS_16, PL_GRIDS_12T }; // сетка: 16 шестнадцатых в такте или 12 триольных восьмых (слоты 12-15 молчат)

typedef struct {
    uint8_t grid;
    uint8_t lv[PL_GRIDS_PARTS * PL_GRIDS_STEPS]; // дорожка * 32 + шаг, как узел карты
} pl_grids_pat_t;

// Рисунок банка строками, чтобы править глазами: два такта по 16 (или 12 у триолей) знаков из ".,gxX".
typedef struct {
    const char *name;
    uint8_t grid;
    const char *part[PL_GRIDS_PARTS];
} pl_grids_bank_t;
extern const pl_grids_bank_t pl_grids_bank[PL_GRIDS_BANK];

// user - свои рисунки [PL_GRIDS_USER] или NULL (тогда свои источники молчат). Источник вне списка молчит.
int pl_grids_src_grid(int src, const pl_grids_pat_t *user);
uint8_t pl_grids_src_level(int src, const pl_grids_pat_t *user, uint8_t x, uint8_t y, int part, int step);
// Как pl_grids_step, плюс биты 6-8 - гост дорожки (у карты всегда 0).
uint16_t pl_grids_src_step(int src, const pl_grids_pat_t *user, uint8_t x, uint8_t y, const uint8_t density[3],
                           const uint8_t perturb[3], int step);
// Копия источника в свой рисунок: банк и свои - как есть, карта - то, что звучит в точке x, y при этих плотностях
// (обычный или акцент).
void pl_grids_copy(int src, const pl_grids_pat_t *user, uint8_t x, uint8_t y, const uint8_t density[3],
                   pl_grids_pat_t *out);

// Свои рисунки: правки с проверкой границ (0 - отказ, ничего не тронуто) и blob во flash
// (магия {'G','P','1',1} и PL_GRIDS_USER рисунков подряд).
#define PL_GRIDS_BLOB (4 + PL_GRIDS_USER * (1 + PL_GRIDS_PARTS * PL_GRIDS_STEPS))
void pl_grids_user_init(pl_grids_pat_t user[PL_GRIDS_USER]); // пусто, сетка 16
int pl_grids_user_cell(pl_grids_pat_t user[PL_GRIDS_USER], int slot, int part, int step, int lv);
int pl_grids_user_set(pl_grids_pat_t user[PL_GRIDS_USER], int slot, const pl_grids_pat_t *pat);
size_t pl_grids_user_export(const pl_grids_pat_t user[PL_GRIDS_USER], void *buf, size_t cap); // 0 - не влезло
int pl_grids_user_import(pl_grids_pat_t user[PL_GRIDS_USER], const void *buf, size_t len);
