// Экран Grids: вкл, сбивка, источник рисунка (карта X/Y, банк, свои), плотности, хаос и свинг, режимы рук и сбивок,
// сетка шагов с бегущим шагом. Поверх - редактор своего рисунка: один такт в два яруса.
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
#define C_GHOST 0x4b2c22 // гост: ещё тише
#define C_CELL 0x26221c  // пусто
#define C_BEAT 0x322d25  // пустая клетка на долю в редакторе
#define C_CHAOS 0x7b565f // ползунок хаоса
#define C_SWING 0x5d6f7b // ползунок свинга
#define PARTS 3
#define STEPS PL_GRIDS_STEPS
#define SLIDERS 6
#define ENC_SRC 13
#define ENC_SWING 14
#define SL_SWING 4 // ползунок свинга: список из 4 значений, остальные - 0..127
#define ALL_USER ((1 << PL_GRIDS_USER) - 1)

static const char *const SLIDER_NAME[SLIDERS] = {"KICK", "SNARE", "HAT", "CHAOS", "SWING", "ROLL"};
static const char *const PART_NAME[PARTS] = {"K", "S", "H"};
static const int SEGS[PL_GUI_IDS] = {[PL_GUI_HAT] = 3, [PL_GUI_TIME] = 2, [PL_GUI_EVERY] = 4, [PL_GUI_LEN] = 3};
static const int ED_N[PL_GED_IDS] = {[PL_GED_BACK] = 1, [PL_GED_SLOT] = 4, [PL_GED_BAR] = 2, [PL_GED_TRIP] = 1,
                                     [PL_GED_COPY] = 1, [PL_GED_CLEAR] = 1};

static struct {
    lv_obj_t *box, *on, *on_lbl, *fill, *fill_lbl;
    lv_obj_t *seg[PL_GUI_IDS][4], *seg_lbl[PL_GUI_IDS][4];
    lv_obj_t *src, *src_lbl, *edit, *edit_lbl;
    lv_obj_t *field, *vl, *hl, *dot;
    lv_obj_t *track[SLIDERS], *bar[SLIDERS], *sl_name[SLIDERS], *sl_val[SLIDERS];
    lv_obj_t *grid, *mark; // сетка - один объект, клетки рисует сам: 96 объектов не влезали в кучу LVGL
    pl_xy_t xy;
    int touch, id, arg, sent; // касание: элемент под пальцем с начала, последнее отправленное значение
    int drawn_on, drawn_fill, drawn_seg[PL_GUI_IDS], drawn_sl[SLIDERS], drawn_xy[2], drawn_step, drawn_src;
    uint16_t drawn_mask[STEPS];
    int drawn_grid, have_mask;
    // Редактор. Состояние меняет ввод (без лока LVGL), рисует ug_draw.
    lv_obj_t *ed, *ed_btn[PL_GED_IDS][4], *ed_lbl[PL_GED_IDS][4], *ed_grid, *ed_mark;
    int ed_open, ed_slot, ed_bar, ed_from; // ed_from - источник, с которого пришли: его копирует COPY
    int ed_shown, ed_drawn_slot, ed_drawn_bar, ed_drawn_from, ed_drawn_step, ed_drawn_trip;
    pl_grids_pat_t ed_pat; // рисунок на экране редактора
    int ed_have;
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
            if (!pl_gui_cell_at(g.drawn_grid, s, p, &x, &y, &w, &h)) {
                continue;
            }
            int hit = g.drawn_mask[s] >> p & 1, acc = g.drawn_mask[s] >> (3 + p) & 1;
            int ghost = g.drawn_mask[s] >> (6 + p) & 1;
            d.color = lv_color_hex(acc ? C_ACC : ghost ? C_GHOST : hit ? C_HIT : C_CELL);
            lv_area_t a = {base.x1 + x - ox, base.y1 + y - oy, base.x1 + x - ox + w - 1, base.y1 + y - oy + h - 1};
            lv_draw_fill(layer, &d, &a);
        }
    }
}

// Клетки редактора: сила удара - шириной заливки (гост треть, удар две трети, акцент целиком) и цветом.
static void ed_grid_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t base;
    lv_obj_get_coords(g.ed_grid, &base);
    int ox, oy, x, y, w, h;
    pl_ged_cell(0, 0, 0, &ox, &oy, &w, &h);
    lv_draw_fill_dsc_t d;
    lv_draw_fill_dsc_init(&d);
    d.radius = 3;
    int trip = g.ed_pat.grid == PL_GRIDS_12T, beat = trip ? 3 : 4;
    for (int p = 0; p < PARTS; p++) {
        for (int c = 0; c < 16; c++) {
            if (!pl_ged_cell(g.ed_pat.grid, c, p, &x, &y, &w, &h)) {
                continue;
            }
            int lv = g.ed_pat.lv[p * STEPS + g.ed_drawn_bar * 16 + c];
            lv_area_t a = {base.x1 + x - ox, base.y1 + y - oy, base.x1 + x - ox + w - 1, base.y1 + y - oy + h - 1};
            d.color = lv_color_hex(c % beat == 0 ? C_BEAT : C_CELL);
            lv_draw_fill(layer, &d, &a);
            if (lv > 0) {
                int acc = lv > PL_GRIDS_ACCENT, ghost = lv < PL_GRIDS_GHOST;
                a.x2 = a.x1 - 1 + (acc ? w : ghost ? w / 3 : 2 * w / 3);
                d.color = lv_color_hex(acc ? C_ACC : ghost ? C_GHOST : C_HIT);
                lv_draw_fill(layer, &d, &a);
            }
        }
    }
}

static lv_obj_t *tile(lv_obj_t *parent, int x, int y, int w, int h, lv_obj_t **lbl, const char *text)
{
    lv_obj_t *t = ui_box(parent, x, y - UI_WORK_Y, w, h, C_TILE);
    lv_obj_set_style_radius(t, 10, 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_border_color(t, lv_color_hex(C_TILE_BRD), 0);
    *lbl = ui_label(t, &bc_17, C_DIM);
    lv_label_set_text(*lbl, text);
    lv_obj_center(*lbl);
    return t;
}

static lv_obj_t *tile_at(int id, int idx, lv_obj_t **lbl, const char *text)
{
    int x, y, w, h;
    pl_gui_rect(id, idx, &x, &y, &w, &h);
    return tile(g.box, x, y, w, h, lbl, text);
}

static void lit(lv_obj_t *t, lv_obj_t *lbl, int on, uint32_t color)
{
    lv_obj_set_style_border_width(t, on ? 2 : 1, 0);
    lv_obj_set_style_border_color(t, lv_color_hex(on ? color : C_TILE_BRD), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(on ? color : C_DIM), 0);
}

static void ed_build(void)
{
    static const char *const TEXT[PL_GED_IDS][4] = {[PL_GED_BACK] = {"BACK"},
                                                    [PL_GED_SLOT] = {"U1", "U2", "U3", "U4"},
                                                    [PL_GED_BAR] = {"BAR 1", "BAR 2"},
                                                    [PL_GED_TRIP] = {"16"},
                                                    [PL_GED_COPY] = {"COPY"},
                                                    [PL_GED_CLEAR] = {"CLEAR"}};
    g.ed = ui_box(g.box, 0, 0, UI_W, UI_WORK_H, C_BG);
    int x, y, w, h;
    for (int id = 1; id < PL_GED_IDS; id++) {
        for (int i = 0; i < ED_N[id]; i++) {
            pl_ged_rect(id, i, &x, &y, &w, &h);
            g.ed_btn[id][i] = tile(g.ed, x, y, w, h, &g.ed_lbl[id][i], TEXT[id][i]);
        }
    }
    for (int p = 0; p < 2 * PARTS; p++) { // подписи дорожек у обоих ярусов
        pl_ged_cell(0, p / PARTS * 8, p % PARTS, &x, &y, &w, &h);
        lv_obj_t *l = ui_label(g.ed, &bc_17, C_DIM);
        lv_label_set_text(l, PART_NAME[p % PARTS]);
        lv_obj_set_pos(l, 10, y - UI_WORK_Y + h / 2 - 8);
    }
    int x1, y1;
    pl_ged_cell(0, 0, 0, &x, &y, &w, &h);
    pl_ged_cell(0, 15, PARTS - 1, &x1, &y1, &w, &h);
    g.ed_grid = ui_box(g.ed, x, y - UI_WORK_Y, x1 + w - x, y1 + h - y, C_BG);
    lv_obj_add_event_cb(g.ed_grid, ed_grid_draw, LV_EVENT_DRAW_MAIN, NULL);
    g.ed_mark = ui_box(g.ed, x, y1 + h + 2 - UI_WORK_Y, w, 4, C_ACCENT); // бегущий шаг под своим ярусом
    lv_obj_set_style_radius(g.ed_mark, 2, 0);
    g.ed_shown = g.ed_drawn_slot = g.ed_drawn_bar = g.ed_drawn_from = g.ed_drawn_trip = -1;
    g.ed_drawn_step = -2;
    ui_show(g.ed, 0);
}

void ug_build(lv_obj_t *scr)
{
    g.box = ui_box(scr, 0, UI_WORK_Y, UI_W, UI_WORK_H, C_BG);
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
    g.src = tile_at(PL_GUI_SRC, 0, &g.src_lbl, "MAP");
    g.edit = tile_at(PL_GUI_XY, 0, &g.edit_lbl, "EDIT STEPS"); // у банка и своих - на месте карты
    pl_gui_rect(PL_GUI_XY, 0, &x, &y, &w, &h);
    g.field = ui_box(g.box, x, y - UI_WORK_Y, w, h, C_FIELD);
    lv_obj_set_style_radius(g.field, 10, 0);
    lv_obj_set_style_border_width(g.field, 1, 0);
    lv_obj_set_style_border_color(g.field, lv_color_hex(C_TILE_BRD), 0);
    g.vl = ui_box(g.field, 0, 0, 2, h, C_TRACK);
    g.hl = ui_box(g.field, 0, 0, w, 2, C_TRACK);
    g.dot = ui_box(g.field, 0, 0, DOT, DOT, pl_page_color(PAGE));
    lv_obj_set_style_radius(g.dot, DOT / 2, 0);
    pl_xy_init(&g.xy, x, y, w, h);
    g.xy.hold = 1; // точка остаётся, где отпустил

    for (int i = 0; i < SLIDERS; i++) {
        pl_gui_rect(PL_GUI_SLIDER, i, &x, &y, &w, &h);
        g.track[i] = ui_box(g.box, x, y - UI_WORK_Y, w, h, C_TILE);
        lv_obj_set_style_radius(g.track[i], 10, 0);
        g.bar[i] = ui_box(g.track[i], 0, h, w, 0, i < 3 ? C_HIT : i == 3 ? C_CHAOS : i == SL_SWING ? C_SWING : C_HIT);
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
        lv_obj_set_pos(l, 12, y - UI_WORK_Y + h / 2 - 8);
    }
    int x1, y1;
    pl_gui_cell(0, 0, &x, &y, &w, &h);
    pl_gui_cell(STEPS - 1, PARTS - 1, &x1, &y1, &w, &h);
    g.grid = ui_box(g.box, x, y - UI_WORK_Y, x1 + w - x, y1 + h - y, C_BG);
    lv_obj_add_event_cb(g.grid, grid_draw, LV_EVENT_DRAW_MAIN, NULL);
    pl_gui_cell(0, PARTS - 1, &x, &y, &w, &h);
    g.mark = ui_box(g.box, x, y + h + 2 - UI_WORK_Y, w, 4, C_ACCENT); // бегущий шаг под сеткой
    lv_obj_set_style_radius(g.mark, 2, 0);

    g.drawn_on = g.drawn_fill = g.drawn_src = -1;
    g.drawn_step = -2;
    g.drawn_xy[0] = g.drawn_xy[1] = -1;
    g.have_mask = 0;
    ed_build(); // последним: лежит поверх экрана
    ui_show(g.box, 0);
}

void ug_show(int on)
{
    ui_show(g.box, on);
    if (!on) {
        g.ed_open = 0; // ушли с вкладки: редактор закрыт
    }
}

static void draw_grid(const pl_model_t *m)
{
    uint16_t mask[STEPS];
    int grid = pl_grids_src_grid(m->raw[PAGE][ENC_SRC], m->gpat);
    pl_grids_preview_src(m->raw[PAGE], m->gpat, mask);
    if (!g.have_mask || grid != g.drawn_grid || memcmp(mask, g.drawn_mask, sizeof mask) != 0) {
        memcpy(g.drawn_mask, mask, sizeof mask);
        g.drawn_grid = grid;
        g.drawn_step = -2; // клетки другой ширины: метку шага переставить
        lv_obj_invalidate(g.grid);
        g.have_mask = 1;
    }
    int step = m->raw[PAGE][1] ? m->grids_step : -1;
    if (step != g.drawn_step) {
        int x, y, w, h;
        int ok = step >= 0 && pl_gui_cell_at(grid, step, 0, &x, &y, &w, &h);
        ui_show(g.mark, ok);
        if (ok) {
            lv_obj_set_x(g.mark, x);
            lv_obj_set_width(g.mark, w);
        }
        g.drawn_step = step;
    }
}

static void ed_draw(const pl_model_t *m)
{
    static const uint8_t none[16];
    char buf[12], text[24];
    const pl_grids_pat_t *pat = &m->gpat[g.ed_slot];
    int slot = g.ed_slot, bar = g.ed_bar, from = g.ed_from, trip = pat->grid == PL_GRIDS_12T;
    if (slot != g.ed_drawn_slot) {
        for (int i = 0; i < PL_GRIDS_USER; i++) {
            lit(g.ed_btn[PL_GED_SLOT][i], g.ed_lbl[PL_GED_SLOT][i], i == slot, pl_page_color(PAGE));
        }
    }
    if (bar != g.ed_drawn_bar) {
        for (int i = 0; i < 2; i++) {
            lit(g.ed_btn[PL_GED_BAR][i], g.ed_lbl[PL_GED_BAR][i], i == bar, C_ACCENT);
        }
    }
    if (trip != g.ed_drawn_trip) {
        lit(g.ed_btn[PL_GED_TRIP][0], g.ed_lbl[PL_GED_TRIP][0], trip, C_ACCENT);
        ui_text(g.ed_lbl[PL_GED_TRIP][0], trip ? "12T" : "16");
    }
    if (from != g.ed_drawn_from) {
        snprintf(text, sizeof text, "COPY %s", pl_value_text(PAGE, ENC_SRC, from, none, buf));
        ui_text(g.ed_lbl[PL_GED_COPY][0], text);
    }
    if (!g.ed_have || bar != g.ed_drawn_bar || memcmp(pat, &g.ed_pat, sizeof *pat) != 0) {
        g.ed_pat = *pat;
        g.ed_have = 1;
        g.ed_drawn_step = -2;
        lv_obj_invalidate(g.ed_grid);
    }
    g.ed_drawn_slot = slot, g.ed_drawn_bar = bar, g.ed_drawn_from = from, g.ed_drawn_trip = trip;
    // Бегущий шаг - только когда играет этот рисунок и этот такт.
    int step = m->raw[PAGE][1] && m->raw[PAGE][ENC_SRC] == PL_GRIDS_SRC_USER + slot ? m->grids_step : -1;
    if (step != g.ed_drawn_step) {
        int x, y, w, h;
        int ok = step >= 0 && step / 16 == bar && pl_ged_cell(pat->grid, step % 16, PARTS - 1, &x, &y, &w, &h);
        ui_show(g.ed_mark, ok);
        if (ok) {
            lv_obj_set_pos(g.ed_mark, x, y + h + 2 - UI_WORK_Y);
            lv_obj_set_width(g.ed_mark, w);
        }
        g.ed_drawn_step = step;
    }
}

void ug_draw(const pl_model_t *m)
{
    const uint8_t *raw = m->raw[PAGE];
    uint32_t pc = pl_page_color(PAGE);
    int open = g.ed_open && m->gpat_have == ALL_USER;
    if (open != g.ed_shown) {
        ui_show(g.ed, open);
        g.ed_shown = open;
    }
    if (open) {
        ed_draw(m);
        return; // экран под редактором не виден: дорисуется после BACK
    }
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
    if (raw[ENC_SRC] != g.drawn_src) {
        char buf[12];
        int map = raw[ENC_SRC] == PL_GRIDS_SRC_MAP;
        ui_text(g.src_lbl, pl_value_text(PAGE, ENC_SRC, raw[ENC_SRC], raw, buf));
        lit(g.src, g.src_lbl, !map, pc);
        ui_show(g.field, map); // карта - только у источника MAP, иначе на её месте кнопка редактора
        ui_show(g.edit, !map);
        g.drawn_src = raw[ENC_SRC];
    }
    for (int i = 0; i < SLIDERS; i++) {
        int swing = i == SL_SWING;
        int v = raw[pl_gui_enc(PL_GUI_SLIDER, i)];
        if (v == g.drawn_sl[i]) {
            continue;
        }
        int x, y, w, h;
        pl_gui_rect(PL_GUI_SLIDER, i, &x, &y, &w, &h);
        int bh = swing ? (v > 3 ? 3 : v) * h / 3 : v * h / 127;
        lv_obj_set_pos(g.bar[i], 0, h - bh);
        lv_obj_set_height(g.bar[i], bh);
        char buf[12];
        if (swing) {
            ui_text(g.sl_val[i], pl_value_text(PAGE, ENC_SWING, v, raw, buf));
        } else {
            snprintf(buf, sizeof buf, "%d", v);
            ui_text(g.sl_val[i], buf);
        }
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

static void put_pat(pl_model_t *m, int slot, const pl_grids_pat_t *pat)
{
    if (pl_model_grids_pat(m, slot, pat)) {
        link_grids_pat(slot, pat);
    }
}

// Источник src в рисунок slot: карта - то, что звучит сейчас, банк и свои - как есть.
static void copy_to(pl_model_t *m, int src, int slot)
{
    const uint8_t *raw = m->raw[PAGE];
    const uint8_t dens[3] = {pl_grids_u8(raw[4]), pl_grids_u8(raw[5]), pl_grids_u8(raw[6])};
    pl_grids_pat_t pat;
    pl_grids_copy(src, m->gpat, pl_grids_u8(raw[2]), pl_grids_u8(raw[3]), dens, &pat);
    put_pat(m, slot, &pat);
}

static int pat_empty(const pl_grids_pat_t *p)
{
    for (int i = 0; i < PARTS * STEPS; i++) {
        if (p->lv[i]) {
            return 0;
        }
    }
    return 1;
}

// Открыть редактор. Со своего рисунка - правим его. С карты или банка - копия в первый пустой слот; пустых нет -
// открываем прошлый слот как есть, источник остаётся под кнопкой COPY (молча чужой рисунок не затираем).
static void ed_enter(pl_model_t *m)
{
    if (m->gpat_have != ALL_USER) {
        return; // рисунки с синта ещё не пришли
    }
    int src = m->raw[PAGE][ENC_SRC];
    if (src >= PL_GRIDS_SRC_USER && src < PL_GRIDS_SRCS) {
        g.ed_slot = src - PL_GRIDS_SRC_USER;
    } else {
        g.ed_from = src;
        for (int k = 0; k < PL_GRIDS_USER; k++) {
            if (pat_empty(&m->gpat[k])) {
                g.ed_slot = k;
                copy_to(m, src, k);
                break;
            }
        }
        send(m, ENC_SRC, PL_GRIDS_SRC_USER + g.ed_slot);
    }
    g.ed_bar = 0;
    g.ed_open = 1;
}

// Касание в редакторе: всё срабатывает на нажатии, один раз за касание.
static void ed_press(pl_model_t *m, int x0, int y0)
{
    int slot = g.ed_slot, arg;
    pl_grids_pat_t pat = m->gpat[slot];
    switch (pl_ged_at(pat.grid, x0, y0, &arg)) {
    case PL_GED_BACK:
        g.ed_open = 0;
        break;
    case PL_GED_SLOT:
        g.ed_slot = arg;
        send(m, ENC_SRC, PL_GRIDS_SRC_USER + arg);
        break;
    case PL_GED_BAR:
        g.ed_bar = arg;
        break;
    case PL_GED_TRIP:
        pat.grid = pat.grid == PL_GRIDS_12T ? PL_GRIDS_16 : PL_GRIDS_12T;
        put_pat(m, slot, &pat);
        break;
    case PL_GED_COPY:
        copy_to(m, g.ed_from, slot);
        break;
    case PL_GED_CLEAR:
        memset(pat.lv, 0, sizeof pat.lv);
        put_pat(m, slot, &pat);
        break;
    case PL_GED_CELL: {
        int part = arg / 16, step = g.ed_bar * 16 + arg % 16;
        int lv = pl_ged_next(pat.lv[part * STEPS + step]);
        if (pl_model_grids_cell(m, slot, part, step, lv)) {
            link_grids_cell(slot, part, step, lv);
        }
        break;
    }
    default:
        break;
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
        // Сетка выбора открывается на отпускании: нажатие, открывшее её, не должно в ней же что-то выбрать.
        if (g.touch && g.id == PL_GUI_SRC) {
            ui_open_pick(PAGE, ENC_SRC);
        }
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
        g.sent = -1;
        if (g.ed_open) {
            g.id = PL_GUI_NONE; // касание редактора: до отпускания экран под ним не трогаем
            ed_press(m, x0, y0);
            return;
        }
        g.id = pl_gui_at(x0, y0, &g.arg);
        int map = raw[ENC_SRC] == PL_GRIDS_SRC_MAP;
        if (g.id == PL_GUI_GRID || (g.id == PL_GUI_XY && !map)) {
            g.id = PL_GUI_NONE;
            ed_enter(m);
        } else if (g.id == PL_GUI_ON) {
            send(m, 1, !raw[1]);
        } else if (g.id == PL_GUI_FILL) {
            send(m, 12, 1);
        } else if (SEGS[g.id]) {
            send(m, pl_gui_enc(g.id, g.arg), g.arg);
        }
    }
    if (g.id == PL_GUI_SLIDER) {
        int v = g.arg == SL_SWING ? pl_gui_swing_value(y) : pl_gui_slider_value(y);
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
