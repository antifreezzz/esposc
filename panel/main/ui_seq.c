// Экран секвенсера: вид ТАКТ (тулбар, такты A-H, два яруса по 8 полос) и вид ПЕСНЯ (8 строк тактов с повтором).
// Полосы рисуются пальцем, кнопки срабатывают в начале касания, как на экране Grids.
#include <stdio.h>
#include <string.h>

#include "link.h"
#include "pl_labels.h"
#include "pl_pick.h"
#include "pl_seq_ui.h"
#include "ui_priv.h"

#define C_CELL 0x26221c
#define C_REST 0x5f574a
#define BPM_PX 3 // пикселей хода пальца на 1 BPM
#define CELL_CHOICES (PL_SEQ_PATS + 1) // A-H и «пусто»
#define REP_CHOICES 16

enum { PICK_NONE, PICK_CELL, PICK_REP };

static struct {
    lv_obj_t *box, *tool, *tier[2], *song, *mark, *cell_mark, *pick;
    int synth, view, brush;
    int touch, id, arg, row;
    pl_seq_drag_t drag;
    int bpm0, bpm_drag; // темп в начале хода пальца; идёт ход
    int picking, pick_row, pick_cell;
    const pl_model_t *m; // для отрисовки: задаётся в us_draw под локом
    // нарисовано
    uint8_t drawn_pat[2][8], drawn_song[PL_SEQ_ROWS * (PL_SEQ_CELLS + 1)];
    int drawn_tool, drawn_tier_ok, drawn_song_ok, drawn_mark, drawn_cell_mark, drawn_pick;
} g;

static const char *const BRUSH[PL_SEQ_BRUSHES] = {"NOTE", "ACC", "SLIDE"};

static const pl_seq_dump_t *seq(const pl_model_t *m)
{
    return (m->seq_have >> g.synth & 1) ? &m->seq[g.synth] : NULL;
}

static uint32_t synth_color(void)
{
    return pl_page_color(PL_PAGE_SYNTH1 + g.synth);
}

static int shown_bpm(const pl_model_t *m)
{
    const pl_seq_dump_t *d = seq(m);
    if (!d) {
        return 0;
    }
    int pos = m->seq_pos[g.synth].bpm;
    return g.bpm_drag || pos == 0 ? d->bpm : pos; // свой ход пальца - сразу, иначе темп, по которому идём
}

static void area_of(lv_obj_t *o, int x, int y, int w, int h, lv_area_t *a)
{
    lv_area_t base;
    lv_obj_get_coords(o, &base);
    a->x1 = base.x1 + x;
    a->y1 = base.y1 + y;
    a->x2 = a->x1 + w - 1;
    a->y2 = a->y1 + h - 1;
}

// Кнопка: плашка, рамка (подсвечена - цветом c) и подпись по центру.
static void button(lv_layer_t *layer, const lv_area_t *a, const char *text, int on, uint32_t c)
{
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.radius = 10;
    f.color = lv_color_hex(C_TILE);
    lv_draw_fill(layer, &f, a);
    lv_draw_border_dsc_t b;
    lv_draw_border_dsc_init(&b);
    b.radius = 10;
    b.width = on ? 2 : 1;
    b.color = lv_color_hex(on ? c : C_TILE_BRD);
    lv_draw_border(layer, &b, a);
    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.font = &bc_17;
    l.align = LV_TEXT_ALIGN_CENTER;
    l.text = text;
    l.text_local = 1;
    l.color = lv_color_hex(on ? c : C_TEXT);
    lv_area_t ta = {a->x1 + 2, a->y1 + (a->y2 - a->y1 + 1 - 16) / 2, a->x2 - 2, a->y2};
    lv_draw_label(layer, &l, &ta);
}

// Элемент раскладки: координаты pl_seq_* - экранные, как и у области отрисовки.
static void rect_area(int id, int idx, int row, lv_area_t *a)
{
    int x, y, w, h;
    pl_seq_rect(g.view, id, idx, row, &x, &y, &w, &h);
    *a = (lv_area_t){x, y, x + w - 1, y + h - 1};
}

static void tool_draw(lv_event_t *e)
{
    const pl_model_t *m = g.m;
    const pl_seq_dump_t *d = m ? seq(m) : NULL;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    char buf[16];
    uint32_t sc = synth_color();
    snprintf(buf, sizeof buf, "S%d", g.synth + 1);
    rect_area(PL_ST_SYNTH, 0, 0, &a);
    button(layer, &a, buf, 1, sc);
    rect_area(PL_ST_VIEW, 0, 0, &a);
    button(layer, &a, g.view == PL_SEQ_PATTERN ? "PATTERN" : "SONG", 0, 0);
    snprintf(buf, sizeof buf, d ? "%d BPM" : "-", shown_bpm(m));
    rect_area(PL_ST_BPM, 0, 0, &a);
    button(layer, &a, buf, g.bpm_drag, C_ACCENT);
    if (g.view != PL_SEQ_PATTERN) {
        return;
    }
    int oct = d ? d->oct[d->edit] - 2 : 0;
    rect_area(PL_ST_OCT_DN, 0, 0, &a);
    button(layer, &a, "OCT-", oct < 0, sc);
    rect_area(PL_ST_OCT_UP, 0, 0, &a);
    button(layer, &a, "OCT+", oct > 0, sc);
    rect_area(PL_ST_BRUSH, 0, 0, &a);
    button(layer, &a, BRUSH[g.brush], g.brush != PL_SEQ_BRUSH_NOTE, C_ACCENT);
    for (int p = 0; p < PL_SEQ_PATS; p++) {
        char name[2] = {(char)('A' + p), 0};
        rect_area(PL_ST_PAT, p, 0, &a);
        button(layer, &a, name, d && d->edit == p, sc);
    }
}

static int tool_key(const pl_model_t *m)
{
    const pl_seq_dump_t *d = seq(m);
    int k = g.synth | g.view << 1 | g.brush << 2 | g.bpm_drag << 4 | (d ? 1 : 0) << 5;
    if (d) {
        k |= d->edit << 6 | d->oct[d->edit] << 9 | shown_bpm(m) << 12;
    }
    return k;
}

// Ярус шагов: полоса до своей ступени, яркая шапка - акцент, перемычка к соседу - слайд, точка внизу - пауза.
static void tier_draw(lv_event_t *e)
{
    int tier = (int)(intptr_t)lv_event_get_user_data(e);
    const pl_seq_dump_t *d = g.m ? seq(g.m) : NULL;
    if (!d) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    const uint8_t *pat = d->pat[d->edit];
    uint32_t sc = synth_color();
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    for (int i = 0; i < 8; i++) {
        int s = tier * 8 + i, x, y, w, h;
        pl_seq_rect(PL_SEQ_PATTERN, PL_ST_BAR, s, 0, &x, &y, &w, &h);
        int top = y;
        lv_area_t col;
        area_of(lv_event_get_target_obj(e), x + 3, 0, w - 6, h, &col);
        f.radius = 6;
        f.color = lv_color_hex((i / 4) % 2 ? C_TILE_EMPTY : C_TILE); // доли по 4 шага
        lv_draw_fill(layer, &f, &col);
        int deg = pat[s] & PL_SEQ_DEG;
        if (deg == PL_SEQ_REST) {
            lv_area_t dot;
            area_of(lv_event_get_target_obj(e), x + w / 2 - 4, h - 13, 8, 8, &dot);
            f.radius = 4;
            f.color = lv_color_hex(C_REST);
            lv_draw_fill(layer, &f, &dot);
            continue;
        }
        // Верх полосы - верх клетки её уровня.
        int yt = 0;
        while (yt < h && pl_seq_level(s, top + yt) > deg + 1) {
            yt++;
        }
        int yb = 0;
        while (yb < h && pl_seq_level(s, top + yb) > 0) {
            yb++;
        }
        lv_area_t bar;
        area_of(lv_event_get_target_obj(e), x + 8, yt, w - 16, yb - yt, &bar);
        f.radius = 4;
        f.color = lv_color_hex(ui_mix(C_BG, sc, 150));
        lv_draw_fill(layer, &f, &bar);
        lv_area_t cap;
        area_of(lv_event_get_target_obj(e), x + 8, yt, w - 16, 8, &cap);
        f.color = lv_color_hex(pat[s] & PL_SEQ_ACC ? C_ACCENT : sc);
        lv_draw_fill(layer, &f, &cap);
        if ((pat[s] & PL_SEQ_SLIDE) && i < 7) {
            lv_area_t br;
            area_of(lv_event_get_target_obj(e), x + w - 8, yt + 2, 16, 4, &br);
            f.radius = 2;
            f.color = lv_color_hex(C_TEXT);
            lv_draw_fill(layer, &f, &br);
        }
    }
}

static void song_draw(lv_event_t *e)
{
    const pl_seq_dump_t *d = g.m ? seq(g.m) : NULL;
    if (!d) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    uint32_t sc = synth_color();
    char buf[8];
    for (int r = 0; r < PL_SEQ_ROWS; r++) {
        lv_area_t a;
        for (int c = 0; c < PL_SEQ_CELLS; c++) {
            int p = d->cell[r][c];
            buf[0] = p < PL_SEQ_PATS ? (char)('A' + p) : 0;
            buf[1] = 0;
            rect_area(PL_ST_CELL, c, r, &a);
            button(layer, &a, buf, p < PL_SEQ_PATS, sc);
        }
        snprintf(buf, sizeof buf, "x%d", d->rep[r]);
        rect_area(PL_ST_REP, 0, r, &a);
        button(layer, &a, buf, 0, 0);
    }
}

// Сетка выбора: такт ячейки (A-H и «-») или повтор строки (x1..x16), как выбор из списка на страницах.
static void pick_draw(lv_event_t *e)
{
    const pl_seq_dump_t *d = g.m ? seq(g.m) : NULL;
    if (!d || g.picking == PICK_NONE) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    int n = g.picking == PICK_CELL ? CELL_CHOICES : REP_CHOICES;
    int cur = g.picking == PICK_CELL ? (d->cell[g.pick_row][g.pick_cell] < PL_SEQ_PATS ? d->cell[g.pick_row][g.pick_cell]
                                                                                        : PL_SEQ_PATS)
                                     : d->rep[g.pick_row] - 1;
    char buf[8];
    for (int s = 0; s < n; s++) {
        int x, y, w, h;
        pl_pick_rect(n, s, &x, &y, &w, &h);
        lv_area_t a;
        area_of(g.pick, x, y - UI_TOP_H, w, h, &a);
        if (g.picking == PICK_CELL) {
            snprintf(buf, sizeof buf, "%c", s < PL_SEQ_PATS ? 'A' + s : '-');
        } else {
            snprintf(buf, sizeof buf, "x%d", s + 1);
        }
        button(layer, &a, buf, s == cur, synth_color());
    }
}

void us_build(lv_obj_t *scr)
{
    g.box = ui_box(scr, 0, UI_TOP_H, UI_W, UI_W - UI_TOP_H, C_BG);
    g.tool = ui_box(g.box, 0, 0, UI_W, 92 + 40 + 4 - UI_TOP_H, C_BG); // тулбар и такты A-H
    lv_obj_add_event_cb(g.tool, tool_draw, LV_EVENT_DRAW_MAIN, NULL);
    for (int t = 0; t < 2; t++) {
        int x, y, w, h;
        pl_seq_rect(PL_SEQ_PATTERN, PL_ST_BAR, t * 8, 0, &x, &y, &w, &h);
        g.tier[t] = ui_box(g.box, 0, y - UI_TOP_H, UI_W, h, C_BG);
        lv_obj_add_event_cb(g.tier[t], tier_draw, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)t);
    }
    g.mark = ui_box(g.box, 0, 0, 44, 4, C_ACCENT); // бегущий шаг над ярусом
    lv_obj_set_style_radius(g.mark, 2, 0);
    g.song = ui_box(g.box, 0, 92 - UI_TOP_H, UI_W, UI_W - 92, C_BG);
    lv_obj_add_event_cb(g.song, song_draw, LV_EVENT_DRAW_MAIN, NULL);
    g.cell_mark = ui_box(g.box, 0, 0, 10, 10, C_BG); // играющая ячейка: рамка поверх
    lv_obj_set_style_bg_opa(g.cell_mark, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g.cell_mark, 3, 0);
    lv_obj_set_style_border_color(g.cell_mark, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_radius(g.cell_mark, 10, 0);
    g.pick = ui_box(g.box, 0, 0, UI_W, UI_W - UI_TOP_H, C_BG);
    lv_obj_add_event_cb(g.pick, pick_draw, LV_EVENT_DRAW_MAIN, NULL);
    g.drawn_tool = g.drawn_mark = g.drawn_cell_mark = g.drawn_pick = -1;
    g.drawn_tier_ok = g.drawn_song_ok = 0;
    ui_show(g.box, 0);
}

void us_show(int on)
{
    ui_show(g.box, on);
}

void us_title(const pl_model_t *m, char *buf, size_t n)
{
    const pl_seq_dump_t *d = seq(m);
    if (!d) {
        snprintf(buf, n, "SEQ S%d", g.synth + 1);
    } else if (g.view == PL_SEQ_SONG) {
        snprintf(buf, n, "SEQ S%d SONG", g.synth + 1);
    } else {
        int oct = d->oct[d->edit] - 2;
        snprintf(buf, n, oct ? "SEQ S%d %c OCT%+d" : "SEQ S%d %c", g.synth + 1, 'A' + d->edit, oct);
    }
}

const char *us_action(const pl_model_t *m)
{
    const pl_seq_dump_t *d = seq(m);
    return !d ? "" : d->run ? "STOP" : "PLAY";
}

void us_play(pl_model_t *m)
{
    const pl_seq_dump_t *d = seq(m);
    if (d && pl_model_seq_ctrl(m, g.synth, !d->run, d->edit)) {
        link_seq_ctrl(g.synth, m->seq[g.synth].run, d->edit);
    }
}

void us_draw(const pl_model_t *m)
{
    g.m = m;
    const pl_seq_dump_t *d = seq(m);
    int pattern = g.view == PL_SEQ_PATTERN && !g.picking;
    ui_show(g.tier[0], pattern);
    ui_show(g.tier[1], pattern);
    ui_show(g.song, g.view == PL_SEQ_SONG && !g.picking);
    ui_show(g.pick, g.picking != PICK_NONE);
    ui_show(g.tool, !g.picking);
    int k = tool_key(m);
    if (k != g.drawn_tool) {
        g.drawn_tool = k;
        lv_obj_invalidate(g.tool);
    }
    if (d && pattern) {
        const uint8_t *pat = d->pat[d->edit];
        for (int t = 0; t < 2; t++) {
            if (!g.drawn_tier_ok || memcmp(g.drawn_pat[t], pat + t * 8, 8) != 0) {
                memcpy(g.drawn_pat[t], pat + t * 8, 8);
                lv_obj_invalidate(g.tier[t]);
            }
        }
        g.drawn_tier_ok = 1;
    } else if (!d) {
        g.drawn_tier_ok = 0;
    }
    if (d && g.view == PL_SEQ_SONG) {
        uint8_t key[sizeof g.drawn_song];
        for (int r = 0; r < PL_SEQ_ROWS; r++) {
            memcpy(key + r * (PL_SEQ_CELLS + 1), d->cell[r], PL_SEQ_CELLS);
            key[r * (PL_SEQ_CELLS + 1) + PL_SEQ_CELLS] = d->rep[r];
        }
        if (!g.drawn_song_ok || memcmp(key, g.drawn_song, sizeof key) != 0) {
            memcpy(g.drawn_song, key, sizeof key);
            lv_obj_invalidate(g.song);
            g.drawn_song_ok = 1;
        }
    }
    int pick_key = g.picking ? (g.picking << 16 | g.pick_row << 8 | g.pick_cell) ^
                                   (d ? (d->cell[g.pick_row][g.pick_cell] << 20 | d->rep[g.pick_row] << 28) : 0)
                             : 0;
    if (pick_key != g.drawn_pick) {
        g.drawn_pick = pick_key;
        lv_obj_invalidate(g.pick);
    }
    // Бегущий шаг: на виде такта, если играет такт экрана; на песне - рамка играющей ячейки.
    const pl_seq_pos_t *p = &m->seq_pos[g.synth];
    int playing = d && d->run;
    int mark = pattern && playing && p->pat == d->edit ? p->step : -1;
    if (mark != g.drawn_mark) {
        ui_show(g.mark, mark >= 0);
        if (mark >= 0) {
            int x, y, w, h;
            pl_seq_rect(PL_SEQ_PATTERN, PL_ST_BAR, mark, 0, &x, &y, &w, &h);
            lv_obj_set_pos(g.mark, x + 8, y - UI_TOP_H - 5);
        }
        g.drawn_mark = mark;
    }
    int cm = g.view == PL_SEQ_SONG && !g.picking && playing && p->row < PL_SEQ_ROWS && p->cell < PL_SEQ_CELLS &&
                     d->cell[p->row][p->cell] < PL_SEQ_PATS
                 ? p->row * PL_SEQ_CELLS + p->cell
                 : -1;
    if (cm != g.drawn_cell_mark) {
        ui_show(g.cell_mark, cm >= 0);
        if (cm >= 0) {
            int x, y, w, h;
            pl_seq_rect(PL_SEQ_SONG, PL_ST_CELL, cm % PL_SEQ_CELLS, cm / PL_SEQ_CELLS, &x, &y, &w, &h);
            lv_obj_set_pos(g.cell_mark, x, y - UI_TOP_H);
            lv_obj_set_size(g.cell_mark, w, h);
        }
        g.drawn_cell_mark = cm;
    }
}

static void send_step(pl_model_t *m, int step)
{
    const pl_seq_dump_t *d = seq(m);
    link_seq_step(g.synth, d->edit, step, d->pat[d->edit][step]);
}

static void set_oct(pl_model_t *m, int dir)
{
    const pl_seq_dump_t *d = seq(m);
    int oct = d->oct[d->edit] - 2 + dir;
    if (pl_model_seq_oct(m, g.synth, d->edit, oct)) {
        link_seq_oct(g.synth, d->edit, oct);
    }
}

static void pick_input(pl_model_t *m, int x0, int y0)
{
    const pl_seq_dump_t *d = seq(m);
    int n = g.picking == PICK_CELL ? CELL_CHOICES : REP_CHOICES;
    int s = pl_pick_at(n, x0, y0);
    if (s >= 0) {
        uint8_t cells[PL_SEQ_CELLS];
        memcpy(cells, d->cell[g.pick_row], sizeof cells);
        int rep = d->rep[g.pick_row];
        if (g.picking == PICK_CELL) {
            cells[g.pick_cell] = s < PL_SEQ_PATS ? (uint8_t)s : PL_SEQ_EMPTY;
        } else {
            rep = s + 1;
        }
        if (pl_model_seq_row(m, g.synth, g.pick_row, cells, rep)) {
            link_seq_row(g.synth, g.pick_row, cells, rep);
        }
    }
    g.picking = PICK_NONE; // тап мимо сетки - закрыть без выбора
}

void us_release(void)
{
    g.touch = 0;
    g.bpm_drag = 0;
}

void us_input(pl_model_t *m, int down, int x, int y, int x0, int y0, uint32_t ms)
{
    (void)ms;
    if (!down) {
        us_release();
        return;
    }
    const pl_seq_dump_t *d = seq(m);
    if (!g.touch) {
        // Начало касания: элемент под пальцем, кнопки срабатывают сразу.
        g.touch = 1;
        if (g.picking) {
            g.id = PL_ST_NONE;
            if (d) {
                pick_input(m, x0, y0);
            } else {
                g.picking = PICK_NONE;
            }
            return;
        }
        g.id = pl_seq_at(g.view, x0, y0, &g.arg, &g.row);
        if (g.id == PL_ST_SYNTH) {
            g.synth ^= 1;
            g.drawn_tier_ok = g.drawn_song_ok = 0;
        } else if (g.id == PL_ST_VIEW) {
            g.view = g.view == PL_SEQ_PATTERN ? PL_SEQ_SONG : PL_SEQ_PATTERN;
            g.drawn_tier_ok = g.drawn_song_ok = 0;
        } else if (!d) {
            g.id = PL_ST_NONE; // дампа синта ещё нет: значения не настоящие
        } else if (g.id == PL_ST_BRUSH) {
            g.brush = (g.brush + 1) % PL_SEQ_BRUSHES;
        } else if (g.id == PL_ST_OCT_DN || g.id == PL_ST_OCT_UP) {
            set_oct(m, g.id == PL_ST_OCT_UP ? 1 : -1);
        } else if (g.id == PL_ST_PAT) {
            if (pl_model_seq_ctrl(m, g.synth, d->run, g.arg)) {
                link_seq_ctrl(g.synth, d->run, g.arg);
            }
        } else if (g.id == PL_ST_BAR) {
            pl_seq_drag_begin(&g.drag, g.brush);
        } else if (g.id == PL_ST_BPM) {
            g.bpm0 = d->bpm;
        } else if (g.id == PL_ST_CELL || g.id == PL_ST_REP) {
            g.picking = g.id == PL_ST_CELL ? PICK_CELL : PICK_REP;
            g.pick_row = g.row;
            g.pick_cell = g.arg;
        }
    }
    if (!d) {
        return;
    }
    if (g.id == PL_ST_BAR) {
        int s = pl_seq_drag(&g.drag, m->seq[g.synth].pat[d->edit], x, y);
        if (s >= 0) {
            send_step(m, s);
        }
    } else if (g.id == PL_ST_BPM) {
        int bpm = g.bpm0 + (y0 - y) / BPM_PX; // вверх - быстрее
        bpm = bpm < 60 ? 60 : bpm > 200 ? 200 : bpm;
        g.bpm_drag = 1;
        if (bpm != d->bpm && pl_model_seq_bpm(m, bpm)) {
            link_seq_bpm(bpm); // слышно сразу; во flash синт пишет в тишине
        }
    }
}
