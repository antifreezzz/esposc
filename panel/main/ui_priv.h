#pragma once
#include <stddef.h>
#include <stdint.h>

#include "esp_lvgl_port.h"
#include "pl_edit.h"
#include "pl_ia.h"
#include "pl_model.h"

// Общее для ui.c и соседних файлов: размеры, цвета, помощники LVGL.

#define UI_W 480
#define UI_TOP_H PL_IA_TOP_H
#define UI_WORK_Y (PL_IA_TOP_H + PL_IA_TABS_H)
#define UI_WORK_H (UI_W - UI_WORK_Y - PL_IA_DOCK_H)
#define UI_DOCK_Y (UI_W - PL_IA_DOCK_H)
#define UI_RING_DY 18 // центр вращения пальцем ниже центра экрана: посередине рабочей области

// Стиль «Б. Тёплый аналог» (docs/design/panel-mockups.html): тёплый графит, кремовый текст, акценты-лампы.
#define C_BG 0x13110e
#define C_BAR 0x1b1814
#define C_TILE 0x1d1a15
#define C_TILE_EMPTY 0x171410
#define C_TILE_BRD 0x2b2720
#define C_FIELD 0x171410 // поле XY, карта Grids, диск захвата
#define C_RING 0x3a342a  // рамка диска захвата
#define C_TOAST 0x1a1713
#define C_DIM 0xa99f8a
#define C_MUTED 0x5f574a
#define C_TEXT 0xf1e7d0
#define C_ACCENT 0xff9d2e
#define C_TRACK 0x2b2720
#define C_BSP 0x8fd3e8
#define C_OK 0xa8d672
#define C_WARN 0xffb648
#define C_BAD 0xff6b4a
#define C_MOD 0xc4a6e0
#define UI_R 14 // радиус карточек и плиток

// Сплошной цвет между a и b (k256=0 - a, 256 - b): заранее, без прозрачности при отрисовке, как mix() в макете.
static inline uint32_t ui_mix(uint32_t a, uint32_t b, int k256)
{
    uint32_t r = 0;
    for (int s = 0; s < 24; s += 8) {
        int ca = (a >> s) & 0xff, cb = (b >> s) & 0xff;
        r |= (uint32_t)((ca * (256 - k256) + cb * k256 + 128) >> 8) << s;
    }
    return r;
}

// Шрифты Barlow Condensed (tools/fonts/build.sh): 15, 17, 22 - ASCII, 32 и 58 - цифры и заглавные.
LV_FONT_DECLARE(bc_15)
LV_FONT_DECLARE(bc_17)
LV_FONT_DECLARE(bc_22)
LV_FONT_DECLARE(bc_32)
LV_FONT_DECLARE(bc_58)
// Стрелки и крестик - глифы шрифта, не FontAwesome.
#define UI_SYM_LEFT "\xE2\x80\xB9"
#define UI_SYM_RIGHT "\xE2\x80\xBA"
#define UI_SYM_CLOSE "\xC3\x97"

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color);
lv_obj_t *ui_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg);
void ui_show(lv_obj_t *o, int on);
void ui_text(lv_obj_t *l, const char *s);
// Открыть сетку выбора значения ручки штатной страницы (список). Без LVGL: можно из ввода.
void ui_open_pick(int page, int enc);

// ui_custom.c: под локом LVGL, кроме *_input.
void uc_build(lv_obj_t *scr);
// Оверлеи редактора: список целей слота и ход lo/hi; прячутся вне своих состояний.
void uc_edit_draw(const pl_model_t *m, const pl_edit_t *e);
// Ввод в состояниях TARGETS и RANGE (остальные шаги - сетка в ui.c).
void uc_edit_input(pl_model_t *m, pl_edit_t *e, int ev, int down, int x, int y, int x0, int y0);
// XY-пэд своей страницы page: X - слот E2, Y - слот E3. Ввод - без лока LVGL, каждый опрос тача.
void uc_xy_show(int on);
void uc_xy_draw(const pl_model_t *m, int page);
void uc_xy_input(pl_model_t *m, int page, int ev, int down, int x, int y, int x0, int y0, uint32_t ms);
// Палец ушёл с пэда не отпусканием (смена вида или страницы): вернуть оси, если HOLD выкл.
void uc_xy_release(pl_model_t *m);

// ui_mod.c: нижний ряд страницы модулятора, форма и кнопка целей.
void um_build(lv_obj_t *grid);
void um_show(int on);
void um_draw(const pl_model_t *m, int page, uint32_t ms);
int um_targets_at(int x, int y);

// ui_petri.c: экран чашки Петри (сетка, орбита, жители; тап - капля). Ввод - без лока LVGL.
void up_build(lv_obj_t *scr);
void up_show(int on);
void up_draw(const pl_model_t *m, uint32_t ms);
void up_input(int down, int x, int y, int x0, int y0);

// ui_grids.c: экран Grids. Ввод - без лока LVGL, каждый опрос тача.
void ug_build(lv_obj_t *scr);
void ug_show(int on);
void ug_draw(const pl_model_t *m);
void ug_input(pl_model_t *m, int down, int x, int y, int x0, int y0, uint32_t ms);
// Палец ушёл с экрана не отпусканием (смена вида): отпустить сбивку и карту.
void ug_release(pl_model_t *m);

// ui_seq.c: экран секвенсера (такт полосами, песня строками). Ввод - без лока LVGL, каждый опрос тача.
void us_build(lv_obj_t *scr);
void us_show(int on);
void us_set_synth(int synth);
int us_picking(void);
void us_close_pick(void);
void us_draw(const pl_model_t *m);
void us_input(pl_model_t *m, int down, int x, int y, int x0, int y0, uint32_t ms);
void us_release(void); // палец ушёл с экрана не отпусканием (смена вида)

// ui_perf.c: экран PERF (дом панели). Ввод - без лока LVGL, каждый опрос тача.
void upf_build(lv_obj_t *scr);
void upf_show(int on);
void upf_draw(const pl_model_t *m, uint32_t ms);
void upf_input(pl_model_t *m, int ev, int down, int x, int y, int x0, int y0, int x1, int y1, uint32_t held_ms,
               uint32_t ms);
void upf_release(pl_model_t *m);
int upf_morph(void);
void upf_set_morph(int bars);
void ui_open_morph(void);
