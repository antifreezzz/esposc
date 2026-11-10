// Оверлеи редактора своих страниц (список целей слота, ход lo/hi) и XY-пэд своей страницы.
#include <stdio.h>

#include "link.h"
#include "pl_gesture.h"
#include "pl_labels.h"
#include "pl_mod.h"
#include "pl_view.h"
#include "pl_xy.h"
#include "ui_priv.h"

#define ROW_Y0 10
#define ROW_DY 72
#define ROW_H 62
#define ROW_X_DEL 400 // правее - крестик «убрать цель»
#define RANGE_H 150   // верх оверлея хода: плитки LO и HI, ниже - зона вращения
#define SIDE_W 222
// XY-пэд в координатах экрана между вкладками и доком: поле, под ним полоса с HOLD и подписями осей.
#define XY_X 8
#define XY_Y (UI_WORK_Y + 6)
#define XY_W (UI_W - 16)
#define XY_H 292
#define XY_STRIP_Y (XY_Y + XY_H + 6)
#define XY_HOLD_W 100
#define XY_DOT 24

static struct {
    lv_obj_t *ed_box, *row[PL_TARGETS + 1], *row_name[PL_TARGETS + 1], *row_sub[PL_TARGETS + 1], *row_del[PL_TARGETS];
    lv_obj_t *rng_box, *side[2], *side_val[2], *rng_name;
    int drawn_which;
    pl_turn_t turn;
    int turning, sent, mod_hold;
    lv_obj_t *xy_box, *xy_field, *xy_vl, *xy_hl, *xy_dot, *xy_hold, *xy_hold_lbl, *xy_lab[2];
    pl_xy_t xy;
    int xy_drawn[2], xy_hold_drawn, xy_page; // xy_page - чья страница под пальцем, -1 нет касания
} c;

static lv_obj_t *tile(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *t = ui_box(parent, x, y, w, h, C_TILE);
    lv_obj_set_style_radius(t, UI_R, 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_border_color(t, lv_color_hex(C_TILE_BRD), 0);
    return t;
}

static void border(lv_obj_t *t, int on, uint32_t color)
{
    lv_obj_set_style_border_width(t, on ? 2 : 1, 0);
    lv_obj_set_style_border_color(t, lv_color_hex(on ? color : C_TILE_BRD), 0);
}

void uc_build(lv_obj_t *scr)
{
    // Редактор: цели слота строками, последняя строка - добавить.
    c.ed_box = ui_box(scr, 0, UI_TOP_H, UI_W, UI_W - UI_TOP_H, C_BG);
    for (int i = 0; i <= PL_TARGETS; i++) {
        c.row[i] = tile(c.ed_box, 12, ROW_Y0 + i * ROW_DY, UI_W - 24, ROW_H);
        c.row_name[i] = ui_label(c.row[i], &bc_22, i < PL_TARGETS ? C_TEXT : C_ACCENT);
        lv_obj_align(c.row_name[i], LV_ALIGN_TOP_LEFT, 16, 8);
        c.row_sub[i] = ui_label(c.row[i], &bc_15, C_DIM);
        lv_obj_align(c.row_sub[i], LV_ALIGN_BOTTOM_LEFT, 17, -8);
        if (i < PL_TARGETS) {
            c.row_del[i] = ui_label(c.row[i], &bc_22, C_BAD);
            lv_label_set_text(c.row_del[i], UI_SYM_CLOSE);
            lv_obj_align(c.row_del[i], LV_ALIGN_RIGHT_MID, -18, 0);
        }
    }
    lv_label_set_text(c.row_name[PL_TARGETS], "+  ADD TARGET");
    lv_label_set_text(c.row_sub[PL_TARGETS], "PAGE, THEN KNOB");

    // Ход цели: плитки LO и HI сверху (тап выбирает), ниже - вращение пальцем вокруг центра.
    c.rng_box = ui_box(scr, 0, UI_TOP_H, UI_W, UI_W - UI_TOP_H, C_BG);
    for (int s = 0; s < 2; s++) {
        c.side[s] = tile(c.rng_box, 12 + s * 234, 10, SIDE_W, RANGE_H - 20);
        lv_obj_t *t = ui_label(c.side[s], &bc_22, C_DIM);
        lv_label_set_text(t, s ? "HI (127)" : "LO (0)");
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 16, 12);
        c.side_val[s] = ui_label(c.side[s], &bc_58, C_ACCENT);
        lv_obj_align(c.side_val[s], LV_ALIGN_BOTTOM_LEFT, 16, -10);
    }
    c.rng_name = ui_label(c.rng_box, &bc_22, C_TEXT);
    lv_obj_align(c.rng_name, LV_ALIGN_TOP_MID, 0, RANGE_H + 30);
    lv_obj_t *hint = ui_label(c.rng_box, &bc_17, C_DIM);
    lv_label_set_text(hint, "TURN AROUND THE CENTER");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, RANGE_H + 70);
    c.drawn_which = -1;

    // XY-пэд своей страницы: перекрестие и точка - живые значения слотов E2 (X) и E3 (Y).
    c.xy_box = ui_box(scr, 0, UI_WORK_Y, UI_W, UI_WORK_H, C_BG);
    c.xy_field = ui_box(c.xy_box, XY_X, XY_Y - UI_WORK_Y, XY_W, XY_H, C_FIELD);
    lv_obj_set_style_radius(c.xy_field, UI_R, 0);
    lv_obj_set_style_border_width(c.xy_field, 1, 0);
    lv_obj_set_style_border_color(c.xy_field, lv_color_hex(C_TILE_BRD), 0);
    c.xy_vl = ui_box(c.xy_field, 0, 0, 2, XY_H, C_TRACK);
    c.xy_hl = ui_box(c.xy_field, 0, 0, XY_W, 2, C_TRACK);
    c.xy_dot = ui_box(c.xy_field, 0, 0, XY_DOT, XY_DOT, C_ACCENT);
    lv_obj_set_style_radius(c.xy_dot, XY_DOT / 2, 0);
    c.xy_hold = tile(c.xy_box, XY_X, XY_STRIP_Y - UI_WORK_Y, XY_HOLD_W, UI_WORK_Y + UI_WORK_H - XY_STRIP_Y - 6);
    c.xy_hold_lbl = ui_label(c.xy_hold, &bc_17, C_DIM);
    lv_label_set_text(c.xy_hold_lbl, "HOLD");
    lv_obj_center(c.xy_hold_lbl);
    for (int a = 0; a < 2; a++) {
        c.xy_lab[a] = ui_label(c.xy_box, &bc_17, C_TEXT);
        lv_obj_set_size(c.xy_lab[a], UI_W - XY_HOLD_W - 32, 18);
        lv_label_set_long_mode(c.xy_lab[a], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(c.xy_lab[a], XY_X + XY_HOLD_W + 14, XY_STRIP_Y - UI_WORK_Y + 3 + a * 22);
    }
    pl_xy_init(&c.xy, XY_X, XY_Y, XY_W, XY_H);
    c.xy_drawn[0] = c.xy_drawn[1] = -1;
    c.xy_hold_drawn = -1;
    c.xy_page = -1;

    ui_show(c.xy_box, 0);
    ui_show(c.ed_box, 0);
    ui_show(c.rng_box, 0);
}

static int count(const pl_model_t *m, const pl_edit_t *e)
{
    int n = 0;
    while (n < PL_TARGETS && m->user_t[e->up][e->enc][n].pe != PL_NONE) {
        n++;
    }
    return n;
}

static void target_text(const pl_model_t *m, int pe, int lo, int hi, char *name, char *sub)
{
    int page = pe >> 4, enc = pe & 15;
    if (page >= PL_PAGES) {
        name[0] = sub[0] = 0;
        return;
    }
    const char *k = pl_knob_label(page, enc, m->raw[page]);
    snprintf(name, 40, "%s / %s", pl_page_name(page), k ? k : "?");
    char a[12], b[12];
    const char *ta = pl_value_text(page, enc, lo, m->raw[page], a);
    const char *tb = pl_value_text(page, enc, hi, m->raw[page], b);
    snprintf(sub, 40, "%s .. %s", ta, tb);
}

static void mod_targets_draw(const pl_model_t *m, const pl_edit_t *e)
{
    for (int i = 0; i < PL_MOD_TARGETS; i++) {
        pl_mod_target_t t = m->mod_t[e->up][i];
        int on = t.page < PL_PAGES && t.enc > 0 && t.enc < PL_KNOBS;
        ui_show(c.row[i], 1);
        ui_show(c.row_del[i], on);
        char name[40], sub[40];
        if (on) {
            const char *knob = pl_knob_label(t.page, t.enc, m->raw[t.page]);
            snprintf(name, sizeof name, "%s / %s", pl_page_name(t.page), knob ? knob : "?");
            snprintf(sub, sizeof sub, "BASE %d  NOW %d  HOLD TO CLEAR", m->raw[t.page][t.enc],
                     pl_mod_effective(m, t.page, t.enc));
        } else {
            snprintf(name, sizeof name, "TARGET %d", i + 1);
            snprintf(sub, sizeof sub, "TAP TO ASSIGN");
        }
        ui_text(c.row_name[i], name);
        ui_text(c.row_sub[i], sub);
    }
    ui_show(c.row[PL_TARGETS], 0);
}

void uc_edit_draw(const pl_model_t *m, const pl_edit_t *e)
{
    ui_show(c.ed_box, e->state == PL_EDIT_TARGETS);
    ui_show(c.rng_box, e->state == PL_EDIT_RANGE);
    char name[40], sub[40];
    if (e->state == PL_EDIT_TARGETS && e->mod) {
        mod_targets_draw(m, e);
    } else if (e->state == PL_EDIT_TARGETS) {
        int n = count(m, e);
        for (int i = 0; i < PL_TARGETS; i++) {
            ui_show(c.row[i], i < n);
            if (i < n) {
                ui_show(c.row_del[i], 1);
                const pl_target_t *t = &m->user_t[e->up][e->enc][i];
                target_text(m, t->pe, t->lo, t->hi, name, sub);
                ui_text(c.row_name[i], name);
                ui_text(c.row_sub[i], sub);
            }
        }
        lv_obj_set_y(c.row[PL_TARGETS], ROW_Y0 + n * ROW_DY);
        ui_show(c.row[PL_TARGETS], n < PL_TARGETS);
    } else if (e->state == PL_EDIT_RANGE) {
        int page = e->pe >> 4, enc = e->pe & 15;
        char buf[12];
        if (page < PL_PAGES) {
            ui_text(c.side_val[0], pl_value_text(page, enc, e->lo, m->raw[page], buf));
            ui_text(c.side_val[1], pl_value_text(page, enc, e->hi, m->raw[page], buf));
            target_text(m, e->pe, e->lo, e->hi, name, sub);
            ui_text(c.rng_name, name);
        }
        if (e->which != c.drawn_which) {
            border(c.side[0], e->which == 0, C_ACCENT);
            border(c.side[1], e->which == 1, C_ACCENT);
            c.drawn_which = e->which;
        }
    }
}

void uc_edit_input(pl_model_t *m, pl_edit_t *e, int ev, int down, int x, int y, int x0, int y0)
{
    int tap = pl_press_tap(ev); // обычное нажатие пальцем дольше 50 мс - это RELEASE, не TAP
    pl_custom_edit_t out;
    int wy = y0 - UI_TOP_H;
    if (e->mod) {
        if (e->state != PL_EDIT_TARGETS) {
            return;
        }
        int row = wy >= ROW_Y0 ? (wy - ROW_Y0) / ROW_DY : -1;
        int inside = row >= 0 && row < PL_MOD_TARGETS && (wy - ROW_Y0) % ROW_DY < ROW_H;
        if (!down && c.mod_hold) {
            c.mod_hold = 0;
            return;
        }
        if (!down && !tap) {
            c.mod_hold = 0;
            return;
        }
        if (!inside) {
            return;
        }
        if (ev == PL_PRESS_HOLD) {
            pl_mod_edit_t mout;
            c.mod_hold = 1;
            if (pl_edit_mod_remove(e, m, row, &mout)) {
                link_mod_edit(&mout);
            }
        } else if (tap) {
            pl_edit_mod_row(e, m, row);
        }
        return;
    }
    if (e->state == PL_EDIT_TARGETS) {
        int row = wy >= ROW_Y0 ? (wy - ROW_Y0) / ROW_DY : -1;
        if (!tap || row < 0 || (wy - ROW_Y0) % ROW_DY >= ROW_H) {
            return;
        }
        int n = count(m, e);
        if (row < n && x0 >= ROW_X_DEL) {
            if (pl_edit_remove(e, row, &out)) {
                link_custom_edit(&out);
            }
        } else if (row < n) {
            pl_edit_open(e, m, row);
            c.turning = 0;
        } else if (row == n) {
            pl_edit_add(e, m);
        }
        return;
    }
    if (e->state != PL_EDIT_RANGE) {
        return;
    }
    if (wy < RANGE_H) {
        if (tap) {
            e->which = x0 >= UI_W / 2; // плитка LO или HI
        }
        return;
    }
    if (down && !c.turning && (ev == PL_PRESS_HOLD || ev == PL_PRESS_NONE)) {
        int page = e->pe >> 4, enc = e->pe & 15, lo, hi;
        pl_knob_range(page, enc, m->raw[page], &lo, &hi);
        c.sent = e->which ? e->hi : e->lo;
        pl_turn_begin(&c.turn, x, y - UI_RING_DY, c.sent, lo, hi);
        c.turning = 1;
    }
    if (!down) {
        c.turning = 0;
        return;
    }
    int v = pl_turn_move(&c.turn, x, y - UI_RING_DY);
    if (v != c.sent && pl_edit_range(e, m, v, &out)) {
        link_custom_edit(&out);
        c.sent = v;
    }
}

void uc_xy_show(int on)
{
    ui_show(c.xy_box, on);
}

// Живые значения осей (-1 - слот пуст) и их ход в c.xy.
static void xy_axes(const pl_model_t *m, int page, int cur[2], pl_cell_t cell[2])
{
    for (int a = 0; a < 2; a++) {
        pl_view_cell(m, page, 1 + a, &cell[a]);
        cur[a] = cell[a].label && m->have ? cell[a].v : -1; // до снимка значения не настоящие
        if (cur[a] >= 0) {
            pl_xy_range(&c.xy, a, cell[a].lo, cell[a].hi);
        }
    }
}

void uc_xy_draw(const pl_model_t *m, int page)
{
    int cur[2];
    pl_cell_t cell[2];
    xy_axes(m, page, cur, cell);
    int pos[2];
    for (int a = 0; a < 2; a++) {
        pos[a] = cur[a] < 0 ? 64 : pl_xy_pos(&c.xy, a, cur[a]);
        char buf[48];
        if (cur[a] < 0) {
            snprintf(buf, sizeof buf, "%c  E%d  -", a ? 'Y' : 'X', a + 2);
        } else {
            snprintf(buf, sizeof buf, "%c  E%d  %s  %s", a ? 'Y' : 'X', a + 2, cell[a].label, cell[a].text);
        }
        ui_text(c.xy_lab[a], buf);
    }
    if (pos[0] != c.xy_drawn[0] || pos[1] != c.xy_drawn[1]) {
        int x, y;
        pl_xy_point(&c.xy, pos[0], pos[1], &x, &y);
        x -= XY_X;
        y -= XY_Y;
        lv_obj_set_x(c.xy_vl, x - 1);
        lv_obj_set_y(c.xy_hl, y - 1);
        lv_obj_set_pos(c.xy_dot, x - XY_DOT / 2, y - XY_DOT / 2);
        c.xy_drawn[0] = pos[0];
        c.xy_drawn[1] = pos[1];
    }
    ui_show(c.xy_dot, cur[0] >= 0 || cur[1] >= 0);
    if (c.xy.hold != c.xy_hold_drawn) {
        border(c.xy_hold, c.xy.hold, C_ACCENT);
        lv_obj_set_style_text_color(c.xy_hold_lbl, lv_color_hex(c.xy.hold ? C_ACCENT : C_DIM), 0);
        c.xy_hold_drawn = c.xy.hold;
    }
}

static void xy_send(pl_model_t *m, int page, int mask, const int out[2])
{
    for (int a = 0; a < 2; a++) {
        if ((mask >> a & 1) && pl_model_local_set(m, page, 1 + a, out[a])) {
            link_set(page, 1 + a, out[a]);
        }
    }
}

void uc_xy_release(pl_model_t *m)
{
    int out[2];
    if (c.xy_page >= 0) {
        xy_send(m, c.xy_page, pl_xy_release(&c.xy, out), out);
        c.xy_page = -1;
    }
}

void uc_xy_input(pl_model_t *m, int page, int ev, int down, int x, int y, int x0, int y0, uint32_t ms)
{
    if (!down) {
        if (c.xy_page < 0 && pl_press_tap(ev) && x0 >= XY_X && x0 < XY_X + XY_HOLD_W && y0 >= XY_STRIP_Y &&
            y0 < UI_WORK_Y + UI_WORK_H) {
            c.xy.hold = !c.xy.hold;
        }
        uc_xy_release(m);
        return;
    }
    if (c.xy_page < 0 && !pl_xy_inside(&c.xy, x0, y0)) {
        return; // касание началось мимо поля
    }
    int cur[2], out[2];
    pl_cell_t cell[2];
    xy_axes(m, page, cur, cell);
    if (c.xy_page < 0 && cur[0] < 0 && cur[1] < 0) {
        return;
    }
    c.xy_page = page;
    xy_send(m, page, pl_xy_touch(&c.xy, x, y, cur, ms, out), out);
}
