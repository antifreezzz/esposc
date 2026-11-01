#pragma once
#include <stdint.h>

#include "esp_lvgl_port.h"
#include "pl_edit.h"
#include "pl_model.h"

// Общее для ui.c и ui_custom.c: размеры, цвета, помощники LVGL.

#define UI_W 480
#define UI_TOP_H 40
#define UI_RING_DY 18 // центр вращения пальцем ниже центра экрана: посередине рабочей области

#define C_BG 0x0b0c0e
#define C_BAR 0x14161b
#define C_TILE 0x161922
#define C_TILE_EMPTY 0x0f1116
#define C_TILE_BRD 0x252a35
#define C_DIM 0x9aa0ad
#define C_MUTED 0x4a505c
#define C_TEXT 0xeeeeee
#define C_ACCENT 0xff9900
#define C_TRACK 0x242832
#define C_BSP 0x66ccff

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color);
lv_obj_t *ui_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg);
void ui_show(lv_obj_t *o, int on);
void ui_text(lv_obj_t *l, const char *s);
// Плитка p 0..9 раскладки меню 2x5 (координаты внутри рабочей области под шапкой).
void ui_tile_xy(int p, int *x, int *y);
int ui_tile_at(int x, int y); // по точке экрана, -1 мимо
#define UI_TILE_W 222
#define UI_TILE_H 74

// ui_custom.c: под локом LVGL, кроме *_input.
void uc_build(lv_obj_t *scr);
void uc_scenes_show(int on);
void uc_scenes_draw(const pl_model_t *m);
// Касание на экране сцен: ev из pl_press_step, (x0, y0) начало, (x1, y1) конец, held_ms - длина касания.
void uc_scenes_input(const pl_model_t *m, int ev, int x0, int y0, int x1, int y1, uint32_t held_ms);
// Оверлеи редактора: список целей слота и ход lo/hi; прячутся вне своих состояний.
void uc_edit_draw(const pl_model_t *m, const pl_edit_t *e);
// Ввод в состояниях TARGETS и RANGE (остальные шаги - сетка и меню в ui.c).
void uc_edit_input(pl_model_t *m, pl_edit_t *e, int ev, int down, int x, int y, int x0, int y0);
