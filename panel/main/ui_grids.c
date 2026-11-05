// Экран Grids: вкл, сбивка, карта X/Y, плотности и хаос, режимы рук и сбивок, сетка шагов с бегущим шагом.
#include <stdio.h>
#include <string.h>

#include "link.h"
#include "pl_grids.h"
#include "pl_grids_ui.h"
#include "pl_labels.h"
#include "pl_xy.h"
#include "ui_priv.h"

#define PAGE PL_PAGE_GRIDS
#define DOT 20
#define C_HIT 0x7d3a29   // удар без акцента: цвет страницы, притушенный к фону
#define C_ACC 0xff6b4a   // акцент: цвет страницы Grids
#define C_CELL 0x26221c  // пусто
#define C_CHAOS 0x7b565f // ползунок хаоса
#define PARTS 3
#define STEPS PL_GRIDS_STEPS

static const char *const SLIDER_NAME[4] = {"KICK", "SNARE", "HAT", "CHAOS"};
static const char *const PART_NAME[PARTS] = {"K", "S", "H"};
static const int SEGS[PL_GUI_IDS] = {[PL_GUI_HAT] = 3, [PL_GUI_TIME] = 2, [PL_GUI_EVERY] = 4, [PL_GUI_LEN] = 3};

static struct {
    lv_obj_t *box, *on, *on_lbl, *fill, *fill_lbl;
    lv_obj_t *seg[PL_GUI_IDS][4], *seg_lbl[PL_GUI_IDS][4];
    lv_obj_t *field, *vl, *hl, *dot;
    lv_obj_t *track[4], *bar[4], *sl_name[4], *sl_val[4];
    lv_obj_t *grid, *mark; // сетка - один объект, клетки рисует сам: 96 объектов не влезали в кучу LVGL
    pl_xy_t xy;
    int touch, id, arg, sent; // касание: элемент под пальцем с начала, последнее отправленное значение
    int drawn_on, drawn_fill, drawn_seg[PL_GUI_IDS], drawn_sl[4], drawn_xy[2], drawn_step;
    uint8_t drawn_mask[STEPS];
    int have_mask;
} g;

// Клетки сетки: фон дорожки одной полосой (клетки - промежутки в ней цветом фона), удары поверх.
static void grid_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t base;
    lv_obj_get_coords(g.grid, &base);
    int ox, oy, x, y, w, h;
    pl_gui_cell(0, 0, &ox, &oy, &w, &h);
    lv_draw_fill_dsc_t d;
    lv_draw_fill_dsc_init(&d);
    d.radius = 2;
    for (int p = 0; p < PARTS; p++) {
        for (int s = 0; s < STEPS; s++) {
            int hit = g.drawn_mask[s] >> p & 1, acc = g.drawn_mask[s] >> (3 + p) & 1;
            pl_gui_cell(s, p, &x, &y, &w, &h);
            d.color = lv_color_hex(acc ? C_ACC : hit ? C_HIT : C_CELL);
            lv_area_t a = {base.x1 + x - ox, base.y1 + y - oy, base.x1 + x - ox + w - 1, base.y1 + y - oy + h - 1};
            lv_draw_fill(layer, &d, &a);
        }
    }
}

static lv_obj_t *tile_at(int id, int idx, lv_obj_t **lbl, const char *text)
{
    int x, y, w, h;
    pl_gui_rect(id, idx, &x, &y, &w, &h);
    lv_obj_t *t = ui_box(g.box, x, y - UI_TOP_H, w, h, C_TILE);
    lv_obj_set_style_radius(t, 10, 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_border_color(t, lv_color_hex(C_TILE_BRD), 0);
    *lbl = ui_label(t, &bc_17, C_DIM);
    lv_label_set_text(*lbl, text);
    lv_obj_center(*lbl);
    return t;
}

static void lit(lv_obj_t *t, lv_obj_t *lbl, int on, uint32_t color)
{
    lv_obj_set_style_border_width(t, on ? 2 : 1, 0);
    lv_obj_set_style_border_color(t, lv_color_hex(on ? color : C_TILE_BRD), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(on ? color : C_DIM), 0);
}

void ug_build(lv_obj_t *scr)
{
    g.box = ui_box(scr, 0, UI_TOP_H, UI_W, UI_W - UI_TOP_H, C_BG);
    g.on = tile_at(PL_GUI_ON, 0, &g.on_lbl, "OFF");
    g.fill = tile_at(PL_GUI_FILL, 0, &g.fill_lbl, "FILL");
    static const uint8_t SEG_ENC[PL_GUI_IDS] = {[PL_GUI_HAT] = 8, [PL_GUI_TIME] = 9, [PL_GUI_EVERY] = 10, [PL_GUI_LEN] = 11};
    static const uint8_t none[16];
    char buf[12];
    for (int id = 0; id < PL_GUI_IDS; id++) {
        for (int i = 0; i < SEGS[id]; i++) {
            g.seg[id][i] = tile_at(id, i, &g.seg_lbl[id][i], pl_value_text(PAGE, SEG_ENC[id], i, none, buf));
        }
        g.drawn_seg[id] = -1;
    }

    int x, y, w, h;
    pl_gui_rect(PL_GUI_XY, 0, &x, &y, &w, &h);
    g.field = ui_box(g.box, x, y - UI_TOP_H, w, h, C_FIELD);
    lv_obj_set_style_radius(g.field, 10, 0);
    lv_obj_set_style_border_width(g.field, 1, 0);
    lv_obj_set_style_border_color(g.field, lv_color_hex(C_TILE_BRD), 0);
    g.vl = ui_box(g.field, 0, 0, 2, h, C_TRACK);
    g.hl = ui_box(g.field, 0, 0, w, 2, C_TRACK);
    g.dot = ui_box(g.field, 0, 0, DOT, DOT, pl_page_color(PAGE));
    lv_obj_set_style_radius(g.dot, DOT / 2, 0);
    pl_xy_init(&g.xy, x, y, w, h);
    g.xy.hold = 1; // точка остаётся, где отпустил

    for (int i = 0; i < 4; i++) {
        pl_gui_rect(PL_GUI_SLIDER, i, &x, &y, &w, &h);
        g.track[i] = ui_box(g.box, x, y - UI_TOP_H, w, h, C_TILE);
        lv_obj_set_style_radius(g.track[i], 10, 0);
        g.bar[i] = ui_box(g.track[i], 0, h, w, 0, i < 3 ? C_HIT : C_CHAOS);
        lv_obj_set_style_radius(g.bar[i], 10, 0);
        g.sl_name[i] = ui_label(g.track[i], &bc_15, C_DIM);
        lv_label_set_text(g.sl_name[i], SLIDER_NAME[i]);
        lv_obj_align(g.sl_name[i], LV_ALIGN_TOP_MID, 0, 6);
        g.sl_val[i] = ui_label(g.track[i], &bc_17, C_TEXT);
        lv_obj_align(g.sl_val[i], LV_ALIGN_BOTTOM_MID, 0, -6);
        g.drawn_sl[i] = -1;
    }

    for (int p = 0; p < PARTS; p++) {
        pl_gui_cell(0, p, &x, &y, &w, &h);
        lv_obj_t *l = ui_label(g.box, &bc_17, C_DIM);
        lv_label_set_text(l, PART_NAME[p]);
        lv_obj_set_pos(l, 12, y - UI_TOP_H + h / 2 - 8);
    }
    int x1, y1;
    pl_gui_cell(0, 0, &x, &y, &w, &h);
    pl_gui_cell(STEPS - 1, PARTS - 1, &x1, &y1, &w, &h);
    g.grid = ui_box(g.box, x, y - UI_TOP_H, x1 + w - x, y1 + h - y, C_BG);
    lv_obj_add_event_cb(g.grid, grid_draw, LV_EVENT_DRAW_MAIN, NULL);
    pl_gui_cell(0, PARTS - 1, &x, &y, &w, &h);
    g.mark = ui_box(g.box, x, y + h + 2 - UI_TOP_H, w, 4, C_ACCENT); // бегущий шаг под сеткой
    lv_obj_set_style_radius(g.mark, 2, 0);

    g.drawn_on = g.drawn_fill = g.drawn_step = -1;
    g.drawn_xy[0] = g.drawn_xy[1] = -1;
    g.have_mask = 0;
    ui_show(g.box, 0);
}

void ug_show(int on)
{
    ui_show(g.box, on);
}

static void draw_grid(const pl_model_t *m)
{
    uint8_t mask[STEPS];
    pl_grids_preview(m->raw[PAGE], mask);
    if (!g.have_mask || memcmp(mask, g.drawn_mask, sizeof mask) != 0) {
        memcpy(g.drawn_mask, mask, sizeof mask);
        lv_obj_invalidate(g.grid);
        g.have_mask = 1;
    }
    int step = m->raw[PAGE][1] ? m->grids_step : -1;
    if (step != g.drawn_step) {
        ui_show(g.mark, step >= 0);
        if (step >= 0) {
            int x, y, w, h;
            pl_gui_cell(step, 0, &x, &y, &w, &h);
            lv_obj_set_x(g.mark, x);
        }
        g.drawn_step = step;
    }
}

void ug_draw(const pl_model_t *m)
{
    const uint8_t *raw = m->raw[PAGE];
    uint32_t pc = pl_page_color(PAGE);
    int on = raw[1] != 0;
    if (on != g.drawn_on) {
        lit(g.on, g.on_lbl, on, pc);
        ui_text(g.on_lbl, on ? "ON" : "OFF");
        g.drawn_on = on;
    }
    int fill = raw[12] != 0 || (g.touch && g.id == PL_GUI_FILL);
    if (fill != g.drawn_fill) {
        lit(g.fill, g.fill_lbl, fill, C_ACCENT);
        g.drawn_fill = fill;
    }
    static const uint8_t SEG_ENC[PL_GUI_IDS] = {[PL_GUI_HAT] = 8, [PL_GUI_TIME] = 9, [PL_GUI_EVERY] = 10, [PL_GUI_LEN] = 11};
    for (int id = 0; id < PL_GUI_IDS; id++) {
        if (!SEGS[id] || raw[SEG_ENC[id]] == g.drawn_seg[id]) {
            continue;
        }
        for (int i = 0; i < SEGS[id]; i++) {
            lit(g.seg[id][i], g.seg_lbl[id][i], i == raw[SEG_ENC[id]], pc);
        }
        g.drawn_seg[id] = raw[SEG_ENC[id]];
    }
    for (int i = 0; i < 4; i++) {
        int v = raw[4 + i];
        if (v == g.drawn_sl[i]) {
            continue;
        }
        int x, y, w, h;
        pl_gui_rect(PL_GUI_SLIDER, i, &x, &y, &w, &h);
        int bh = v * h / 127;
        lv_obj_set_pos(g.bar[i], 0, h - bh);
        lv_obj_set_height(g.bar[i], bh);
        char buf[8];
        snprintf(buf, sizeof buf, "%d", v);
        ui_text(g.sl_val[i], buf);
        g.drawn_sl[i] = v;
    }
    if (raw[2] != g.drawn_xy[0] || raw[3] != g.drawn_xy[1]) {
        int x, y;
        pl_xy_point(&g.xy, pl_xy_pos(&g.xy, 0, raw[2]), pl_xy_pos(&g.xy, 1, raw[3]), &x, &y);
        x -= g.xy.x0;
        y -= g.xy.y0;
        lv_obj_set_x(g.vl, x - 1);
        lv_obj_set_y(g.hl, y - 1);
        lv_obj_set_pos(g.dot, x - DOT / 2, y - DOT / 2);
        g.drawn_xy[0] = raw[2];
        g.drawn_xy[1] = raw[3];
    }
    draw_grid(m);
}

static void send(pl_model_t *m, int enc, int v)
{
    if (pl_model_local_set(m, PAGE, enc, v)) {
        link_set(PAGE, enc, v);
    }
}

void ug_release(pl_model_t *m)
{
    if (!g.touch) {
        return;
    }
    if (g.id == PL_GUI_FILL) {
        send(m, 12, 0); // кнопка отпущена: следующее нажатие снова запустит сбивку
    } else if (g.id == PL_GUI_XY) {
        int out[2], mask = pl_xy_release(&g.xy, out);
        for (int a = 0; a < 2; a++) {
            if (mask >> a & 1) {
                send(m, 2 + a, out[a]);
            }
        }
    }
    g.touch = 0;
}

void ug_input(pl_model_t *m, int down, int x, int y, int x0, int y0, uint32_t ms)
{
    if (!down) {
        ug_release(m);
        return;
    }
    if (!m->have) {
        return; // без снимка значения не настоящие
    }
    const uint8_t *raw = m->raw[PAGE];
    if (!g.touch) {
        // Начало касания: элемент под пальцем, кнопки и сегменты срабатывают сразу.
        g.touch = 1;
        g.id = pl_gui_at(x0, y0, &g.arg);
        g.sent = -1;
        if (g.id == PL_GUI_ON) {
            send(m, 1, !raw[1]);
        } else if (g.id == PL_GUI_FILL) {
            send(m, 12, 1);
        } else if (SEGS[g.id]) {
            send(m, pl_gui_enc(g.id, g.arg), g.arg);
        }
    }
    if (g.id == PL_GUI_SLIDER) {
        int v = pl_gui_slider_value(y);
        if (v != g.sent) {
            send(m, pl_gui_enc(g.id, g.arg), v);
            g.sent = v;
        }
    } else if (g.id == PL_GUI_XY) {
        int cur[2] = {raw[2], raw[3]}, out[2];
        int mask = pl_xy_touch(&g.xy, x, y, cur, ms, out);
        for (int a = 0; a < 2; a++) {
            if (mask >> a & 1) {
                send(m, 2 + a, out[a]);
            }
        }
    }
}
