#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "link.h"
#include "pl_gesture.h"
#include "pl_ia.h"
#include "pl_labels.h"
#include "pl_mix.h"
#include "pl_nav.h"
#include "pl_pick.h"
#include "pl_seq_ui.h"
#include "pl_view.h"
#include "ui_priv.h"

// Каркас по инструментам: шапка 40 px (группа и сводка, PLAY с темпом, режим, загрузка ядер),
// полоса вкладок 36 px, рабочая область 356 px (у PERF без вкладок - 392 px), док 48 px внизу.
// Шапка, вкладки, док и сетка выбора рисуются сами в LV_EVENT_DRAW_MAIN без россыпи объектов LVGL.

#define W UI_W
#define TOP_H UI_TOP_H
#define WORK_Y UI_WORK_Y
#define WORK_H UI_WORK_H
#define DOCK_Y (W - PL_IA_DOCK_H)
#define RING_DY UI_RING_DY
#define KNOB_D 48   // дуга внутри карточки 150x66
#define BIG_BOX 244 // круглый диск подложки под большую дугу в центре
#define BIG_D 210   // большая дуга захваченной ручки
#define KNOBS 15    // энкодеры 2..16 (ячейки 0..14)
#define FLASH_MS 150
#define LOAD_WARN 85
#define LOAD_STALE_MS 2000

enum { OVL_NONE, OVL_VAL, OVL_USER, OVL_MODE, OVL_MORPH };

typedef struct {
    lv_obj_t *card, *arc, *val, *name, *sub, *pdot, *empty_dot;
    int v, held, grabbed, kind, lit; // нарисовано
    char pill[12]; // подпись кнопки переключателя или триггера (рисует card_draw, без объектов LVGL)
    int lo, hi, bipolar;
    uint32_t color, src_color;
    char srcs[12]; // страница-источник (MIX, своя страница), "" - нет
    char lab[20];  // подпись карточки, "" - пустой слот
} knob_t;

static struct {
    int group;              // открытая группа дока PL_GRP_*
    int tab[PL_GROUPS];     // последняя открытая вкладка каждой группы
    int user;               // какая своя страница открыта на вкладке USER (0..5)
    int page;               // открытая страница ручек (0..22)
    int dish;               // 1 - на странице с чашкой открыт экран чашки вместо карточек
    int overlay;            // OVL_* поверх экрана
    int grab;               // индекс ручки 0..14 или -1
    pl_press_t press;
    pl_turn_t turn;
    pl_edit_t ed;
    int sent;
    int play_touch, bpm_drag, bpm0;
    uint8_t xy_view; // бит своей страницы: вместо сетки XY-пэд
    lv_obj_t *bar, *tabs, *dock, *grid, *pick;
    knob_t knob[KNOBS];
    lv_obj_t *big_box, *big, *big_val, *big_name, *big_sub, *bsp_mark;
    uint32_t big_color;
    int big_sym, drawn_grab;
    lv_obj_t *toast, *toast_name, *toast_val;
    lv_obj_t *chip_page[PL_TARGETS], *chip_val[PL_TARGETS];
    uint32_t toast_color;
    int toast_tall;
    int pick_page, pick_enc, pick_tp, pick_te;
    int drawn_tabs, drawn_dock, drawn_pick;
    char drawn_bar[96];
    int flash_cell;
    uint32_t flash_until, now;
} u;

static const pl_model_t *pick_m; // модель для отрисовки в событиях LV_EVENT_DRAW_MAIN

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *ui_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *arc(lv_obj_t *parent, int d, int width)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, d, d);
    lv_arc_set_bg_angles(a, 135, 45);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(a, false);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(a, 0, 0);
    return a;
}

void ui_show(lv_obj_t *o, int on)
{
    if (lv_obj_is_hidden(o) == !!on) {
        lv_obj_set_hidden(o, !on);
    }
}

void ui_text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s) != 0) {
        lv_label_set_text(l, s);
    }
}

static int is_user(int page)
{
    return page >= PL_USER_FIRST && page < PL_USER_FIRST + PL_USER_PAGES;
}

static int editing(void)
{
    return u.ed.state != PL_EDIT_OFF;
}

static int overlay_on(void)
{
    return editing() || u.overlay != OVL_NONE || us_picking();
}

static int bar_kind(void)
{
    return overlay_on() ? PL_BAR_OVERLAY : PL_BAR_MAIN;
}

static uint32_t group_color(int g)
{
    switch (g) {
    case PL_GRP_DRUMS: return pl_page_color(1);
    case PL_GRP_SYN1: return pl_page_color(PL_PAGE_SYNTH1);
    case PL_GRP_SYN2: return pl_page_color(PL_PAGE_SYNTH1 + 1);
    case PL_GRP_FX: return pl_page_color(8);
    case PL_GRP_MOD: return C_MOD;
    default: return C_TEXT;
    }
}

static const pl_tab_t *cur_tab(void)
{
    return pl_ia_tab(u.group, u.tab[u.group]);
}

// На экране XY-пэд вместо сетки: вкладка USER с видом XY, не в оверлее.
static int xy_on(void)
{
    const pl_tab_t *t = cur_tab();
    return !overlay_on() && t && t->kind == PL_TAB_USER && (u.xy_view >> u.user & 1);
}

static int view_page(void)
{
    return u.ed.state == PL_EDIT_PICK_KNOB ? u.ed.page : u.page;
}

// Какие кнопки действий стоят правее вкладок у открытой вкладки (до двух).
static int tab_actions(const pl_model_t *m, const char *acts[2], int on[2])
{
    const pl_tab_t *t = cur_tab();
    if (!t || !pl_ia_tabs(u.group)) {
        return 0;
    }
    if (t->kind == PL_TAB_USER) {
        acts[0] = "XY", on[0] = (u.xy_view >> u.user & 1) != 0;
        if (m && (m->user_have >> u.user & 1)) {
            acts[1] = "EDIT", on[1] = 0;
            return 2;
        }
        return 1;
    }
    if (u.dish) {
        acts[0] = "KNOBS", on[0] = 0;
        return 1;
    }
    if (t->kind == PL_TAB_PAGE && m && pl_view_dish(m, u.page)) {
        acts[0] = "DISH", on[0] = 0;
        return 1;
    }
    return 0;
}

static void draw_pill(lv_layer_t *layer, const lv_area_t *a, const char *text, int on, uint32_t c, int radius)
{
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.radius = radius;
    f.color = lv_color_hex(C_TILE);
    lv_draw_fill(layer, &f, a);
    lv_draw_border_dsc_t b;
    lv_draw_border_dsc_init(&b);
    b.radius = radius;
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

// Заголовок и сводка в основной шапке: имя группы и что внутри (тип синта, модель ударки, дилэй, своя страница).
static void main_title(const pl_model_t *m, const char **title, char sub[24])
{
    sub[0] = 0;
    const pl_tab_t *t = cur_tab();
    if (u.group == PL_GRP_SYN1 || u.group == PL_GRP_SYN2) {
        int k = u.group - PL_GRP_SYN1;
        *title = k ? "SYNTH 2" : "SYNTH 1";
        int sp = t && t->page == PL_PAGE_LAYER1 + k ? t->page : PL_PAGE_SYNTH1 + k;
        if (m && m->have) {
            snprintf(sub, 24, "%s", pl_page_summary(sp, m->raw[sp], sub));
        }
        return;
    }
    *title = pl_ia_group_name(u.group);
    if (!m || !t) {
        return;
    }
    if (t->kind == PL_TAB_USER) {
        snprintf(sub, 24, "%s", pl_user_name(u.user));
    } else if (m->have) {
        int sp = t->page < PL_PAGES ? t->page : 1;
        snprintf(sub, 24, "%s", pl_page_summary(sp, m->raw[sp], sub));
    }
}

static void overlay_title(const pl_model_t *m, char nav[40], uint32_t *nc, const char **act)
{
    static const char *const STEP[] = {"", "EDIT: TAP A KNOB", "TARGETS", "PICK PAGE", "PICK KNOB", "RANGE"};
    *nc = C_ACCENT;
    *act = "";
    if (editing()) {
        snprintf(nav, 40, "%s", STEP[u.ed.state]);
        if (u.ed.state == PL_EDIT_TARGETS) {
            snprintf(nav, 40, "E%d TARGETS", u.ed.enc + 1);
        } else if (u.ed.state == PL_EDIT_PICK_KNOB) {
            snprintf(nav, 40, "PICK: %s", pl_page_name(u.ed.page));
        }
        *act = u.ed.state == PL_EDIT_SLOTS ? "DONE" : "BACK";
    } else if (u.overlay == OVL_VAL && m) {
        pl_cell_t c;
        pl_view_cell(m, u.pick_page, u.pick_enc, &c);
        snprintf(nav, 40, "%s / %s", pl_view_page_name(u.pick_page), c.label ? c.label : "?");
        *nc = pl_view_page_color(u.pick_page);
    } else if (u.overlay == OVL_USER) {
        snprintf(nav, 40, "USER PAGE");
        *nc = pl_view_page_color(PL_USER_FIRST);
    } else if (u.overlay == OVL_MODE) {
        snprintf(nav, 40, "MODE");
        *nc = C_TEXT;
    } else if (u.overlay == OVL_MORPH) {
        snprintf(nav, 40, "SCENE MORPH");
        *nc = C_ACCENT;
    } else {
        snprintf(nav, 40, "SEQ");
        *nc = group_color(u.group);
    }
}

static int seq_running(const pl_model_t *m)
{
    return m && (((m->seq_have & 1) && m->seq[0].run) || ((m->seq_have & 2) && m->seq[1].run));
}

static int current_tempo(const pl_model_t *m, int *ext)
{
    *ext = 0;
    if (!m) {
        return 0;
    }
    int dump = (m->seq_have & 1) ? m->seq[0].bpm : (m->seq_have & 2) ? m->seq[1].bpm : 0;
    int pos = m->seq_pos[0].bpm ? m->seq_pos[0].bpm : m->seq_pos[1].bpm;
    if (u.bpm_drag && dump > 0) {
        return dump;
    }
    return pl_nav_tempo(dump, pos, ext);
}

static void bar_draw(lv_event_t *e)
{
    const pl_model_t *m = pick_m;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.color = lv_color_hex(C_TILE_BRD);
    lv_area_t line = {0, TOP_H - 1, W - 1, TOP_H - 1};
    lv_draw_fill(layer, &f, &line);

    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.text_local = 1;

    if (bar_kind() == PL_BAR_OVERLAY) {
        char nav[40];
        uint32_t nc;
        const char *act;
        overlay_title(m, nav, &nc, &act);
        int x0, x1;
        pl_nav_rect(PL_BAR_OVERLAY, PL_NAV_CLOSE, &x0, &x1);
        lv_area_t ca = {x0, 5, x1 - 1, TOP_H - 6};
        draw_pill(layer, &ca, UI_SYM_CLOSE " CLOSE", 0, 0, 8);
        if (act[0]) {
            pl_nav_rect(PL_BAR_OVERLAY, PL_NAV_ACTION, &x0, &x1);
            lv_area_t aa = {x0, 5, x1 - 1, TOP_H - 6};
            draw_pill(layer, &aa, act, 0, 0, 8);
        }
        pl_nav_title(PL_BAR_OVERLAY, &x0, &x1);
        l.font = &bc_22;
        l.align = LV_TEXT_ALIGN_CENTER;
        l.color = lv_color_hex(nc);
        l.text = nav;
        lv_area_t ta = {x0, 8, x1 - 1, TOP_H - 1};
        lv_draw_label(layer, &l, &ta);
        lv_point_t sz;
        lv_text_get_size(&sz, nav, &bc_22, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x > 0) {
            int cx = (x0 + x1) / 2;
            f.color = lv_color_hex(nc);
            f.radius = 2;
            lv_area_t ul = {cx - sz.x / 2, TOP_H - 4, cx + sz.x / 2, TOP_H - 2};
            lv_draw_fill(layer, &f, &ul);
        }
        return;
    }

    const char *title;
    char sub[24];
    main_title(m, &title, sub);
    l.font = &bc_22;
    l.align = LV_TEXT_ALIGN_LEFT;
    l.color = lv_color_hex(group_color(u.group));
    l.text = title;
    lv_area_t ta = {12, 8, 224, TOP_H - 1};
    lv_draw_label(layer, &l, &ta);
    if (sub[0]) {
        lv_point_t sz;
        lv_text_get_size(&sz, title, &bc_22, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        l.font = &bc_17;
        l.color = lv_color_hex(C_DIM);
        l.text = sub;
        lv_area_t sa = {12 + sz.x + 10, 11, 224, TOP_H - 1};
        lv_draw_label(layer, &l, &sa);
    }

    // Плашка PLAY с треугольником/квадратом и темпом.
    int x0, x1, ext = 0;
    pl_nav_rect(PL_BAR_MAIN, PL_NAV_PLAY, &x0, &x1);
    int run = seq_running(m), bpm = current_tempo(m, &ext);
    lv_area_t pa = {x0, 6, x1 - 1, TOP_H - 7};
    f.radius = 8;
    f.color = lv_color_hex(C_TILE);
    lv_draw_fill(layer, &f, &pa);
    lv_draw_border_dsc_t b;
    lv_draw_border_dsc_init(&b);
    b.radius = 8;
    b.width = u.bpm_drag ? 2 : 1;
    b.color = lv_color_hex(u.bpm_drag ? C_ACCENT : run ? C_OK : C_TILE_BRD);
    lv_draw_border(layer, &b, &pa);
    if (run) {
        lv_draw_triangle_dsc_t tri;
        lv_draw_triangle_dsc_init(&tri);
        tri.color = lv_color_hex(C_OK);
        tri.p[0] = (lv_point_precise_t){x0 + 10, 13};
        tri.p[1] = (lv_point_precise_t){x0 + 10, 25};
        tri.p[2] = (lv_point_precise_t){x0 + 21, 19};
        lv_draw_triangle(layer, &tri);
    } else {
        f.radius = 2;
        f.color = lv_color_hex(C_DIM);
        lv_area_t sq = {x0 + 10, 14, x0 + 19, 23};
        lv_draw_fill(layer, &f, &sq);
    }
    char bpm_buf[12];
    snprintf(bpm_buf, sizeof bpm_buf, bpm > 0 ? "%d" : "-", bpm);
    l.font = &bc_17;
    l.align = LV_TEXT_ALIGN_CENTER;
    l.color = lv_color_hex(u.bpm_drag ? C_ACCENT : ext ? C_BSP : C_TEXT);
    l.text = bpm_buf;
    lv_area_t ba = {x0 + 24, 11, x1 - 3, TOP_H - 7};
    lv_draw_label(layer, &l, &ba);

    // Плашка режима с цветом статуса.
    pl_nav_rect(PL_BAR_MAIN, PL_NAV_MODE, &x0, &x1);
    lv_area_t ma = {x0, 6, x1 - 1, TOP_H - 7};
    f.radius = 8;
    f.color = lv_color_hex(C_TILE);
    lv_draw_fill(layer, &f, &ma);
    b.width = 1;
    b.color = lv_color_hex(C_TILE_BRD);
    lv_draw_border(layer, &b, &ma);
    int linked = m && pl_model_linked(m, u.now);
    const char *st = m ? pl_view_status(m, linked) : "NO LINK";
    uint32_t sc = !linked ? C_BAD : m->mode || m->demo >= PL_DEMO_DRONE ? C_OK : C_WARN;
    l.color = lv_color_hex(sc);
    l.text = st;
    lv_area_t sta = {x0 + 2, 11, x1 - 3, TOP_H - 7};
    lv_draw_label(layer, &l, &sta);

    // Загрузка ядер двумя строками справа.
    if (m && m->have_load && u.now - m->load_ms <= LOAD_STALE_MS) {
        const pl_load_t *ld = &m->load;
        int worst = ld->c1_worst > ld->c0_worst ? ld->c1_worst : ld->c0_worst;
        uint32_t lc = (worst >= 100 || ld->underruns) ? C_BAD : worst >= LOAD_WARN ? C_WARN : C_DIM;
        char c1[16], c0[16];
        if (ld->guard) {
            snprintf(c1, sizeof c1, "G%d %d%%", ld->guard, ld->c1_worst);
        } else {
            snprintf(c1, sizeof c1, "C1 %d%%", ld->c1_worst);
        }
        snprintf(c0, sizeof c0, "C0 %d%%", ld->c0_worst);
        l.font = &bc_15;
        l.align = LV_TEXT_ALIGN_RIGHT;
        l.color = lv_color_hex(lc);
        l.text = c1;
        lv_area_t la1 = {402, 4, 470, 20};
        lv_draw_label(layer, &l, &la1);
        l.text = c0;
        lv_area_t la2 = {402, 21, 470, 37};
        lv_draw_label(layer, &l, &la2);
    }
}

static void tabs_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t base;
    lv_obj_get_coords(u.tabs, &base);
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.color = lv_color_hex(C_TILE_BRD);
    lv_area_t line = {base.x1, base.y2, base.x2, base.y2};
    lv_draw_fill(layer, &f, &line);

    uint32_t pc = group_color(u.group);
    int ntabs = pl_ia_tabs(u.group);
    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.text_local = 1;
    for (int t = 0; t < ntabs; t++) {
        int x0, x1;
        pl_ia_tab_rect(u.group, t, &x0, &x1);
        const pl_tab_t *tab = pl_ia_tab(u.group, t);
        int on = t == u.tab[u.group];
        int bsp = pl_ia_tab_bsp(u.group, t, u.user);
        if (bsp >= 0) {
            char num[16];
            snprintf(num, sizeof num, "%d", bsp);
            lv_point_t nsz, tsz;
            lv_text_get_size(&nsz, num, &bc_15, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lv_text_get_size(&tsz, tab->name, &bc_17, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            int sx = x0 + (x1 - x0 - (nsz.x + 4 + tsz.x)) / 2;
            l.font = &bc_15;
            l.align = LV_TEXT_ALIGN_LEFT;
            l.color = lv_color_hex(on ? ui_mix(pc, C_BG, 90) : C_MUTED);
            l.text = num;
            lv_area_t na = {sx, base.y1 + 11, sx + nsz.x + 4, base.y2};
            lv_draw_label(layer, &l, &na);
            l.font = &bc_17;
            l.color = lv_color_hex(on ? pc : C_DIM);
            l.text = tab->name;
            lv_area_t ta = {sx + nsz.x + 4, base.y1 + 9, x1 - 1, base.y2};
            lv_draw_label(layer, &l, &ta);
        } else {
            l.font = &bc_17;
            l.align = LV_TEXT_ALIGN_CENTER;
            l.color = lv_color_hex(on ? pc : C_DIM);
            l.text = tab->name;
            lv_area_t ta = {x0, base.y1 + 9, x1 - 1, base.y2};
            lv_draw_label(layer, &l, &ta);
        }
        if (on) {
            f.radius = 2;
            f.color = lv_color_hex(pc);
            lv_area_t ul = {x0 + 8, base.y2 - 3, x1 - 9, base.y2 - 1};
            lv_draw_fill(layer, &f, &ul);
        }
    }
    const char *acts[2];
    int act_on[2];
    int n = tab_actions(pick_m, acts, act_on);
    for (int i = 0; i < n; i++) {
        int x0, x1;
        pl_nav_act_rect(u.group, n, i, &x0, &x1);
        if (x1 > x0) {
            lv_area_t ba = {x0, base.y1 + 4, x1 - 4, base.y2 - 5};
            draw_pill(layer, &ba, acts[i], act_on[i], C_ACCENT, 8);
        }
    }
}

static void dock_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t base;
    lv_obj_get_coords(u.dock, &base);
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.color = lv_color_hex(C_TILE_BRD);
    lv_area_t line = {base.x1, base.y1, base.x2, base.y1};
    lv_draw_fill(layer, &f, &line);

    int bsp_grp = -1, bsp_tab = -1;
    if (pick_m && pick_m->have && pick_m->mode == 1) {
        pl_ia_find(pick_m->bsp_page, &bsp_grp, &bsp_tab);
    }
    lv_draw_border_dsc_t b;
    lv_draw_border_dsc_init(&b);
    b.radius = 12;
    b.width = 2;
    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.font = &bc_17;
    l.align = LV_TEXT_ALIGN_CENTER;
    l.text_local = 1;

    for (int g = 0; g < PL_GROUPS; g++) {
        int x0, x1;
        pl_ia_dock_rect(g, &x0, &x1);
        int en = pl_ia_group_on(g);
        int on = g == u.group;
        uint32_t pc = group_color(g);
        if (on) {
            lv_area_t ba = {x0 + 4, base.y1 + 5, x1 - 5, base.y2 - 5};
            f.radius = 12;
            f.color = lv_color_hex(C_TILE);
            lv_draw_fill(layer, &f, &ba);
            b.color = lv_color_hex(pc);
            lv_draw_border(layer, &b, &ba);
        } else if (en) {
            f.radius = 2;
            f.color = lv_color_hex(ui_mix(pc, C_BAR, 115));
            lv_area_t bar = {x0 + 34, base.y2 - 8, x0 + 45, base.y2 - 6};
            lv_draw_fill(layer, &f, &bar);
        }
        l.color = lv_color_hex(on ? pc : en ? C_DIM : C_MUTED);
        l.text = pl_ia_group_name(g);
        lv_area_t ta = {x0, base.y1 + 15, x1 - 1, base.y2};
        lv_draw_label(layer, &l, &ta);
        if (g == bsp_grp) {
            f.radius = 4;
            f.color = lv_color_hex(C_BSP);
            lv_area_t dot = {x0 + 62, base.y1 + 9, x0 + 69, base.y1 + 16};
            lv_draw_fill(layer, &f, &dot);
        }
    }
}

static const char *mode_desc(int live, int mode)
{
    if (mode == PL_DEMO_DRONE) {
        return "YOUR KNOBS, DRONE";
    }
    if (live) {
        return "BSP + SEQUENCER";
    }
    switch (mode) {
    case PL_DEMO_METAL: return "DEMO 150 BPM";
    case PL_DEMO_ACID: return "DEMO 125 BPM";
    default: return "YOUR KNOBS, SEQUENCER";
    }
}

// Сетка оверлея: значения списка, выбор USER 1-6, выбор режима или выбор страницы цели в редакторе.
static void pick_draw(lv_event_t *e)
{
    if (!pick_m) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t base;
    lv_obj_get_coords(u.pick, &base);
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.radius = 12;
    f.color = lv_color_hex(C_TILE);
    lv_draw_border_dsc_t b;
    lv_draw_border_dsc_init(&b);
    b.radius = 12;
    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.align = LV_TEXT_ALIGN_CENTER;
    l.text_local = 1;

    if (editing() && u.ed.state == PL_EDIT_PICK_PAGE) {
        for (int p = 0; p < PL_PAGES; p++) {
            int x, y, w, h;
            pl_pick_rect(PL_PAGES, p, &x, &y, &w, &h);
            lv_area_t a = {x, y, x + w - 1, y + h - 1};
            lv_draw_fill(layer, &f, &a);
            b.width = 1;
            b.color = lv_color_hex(C_TILE_BRD);
            lv_draw_border(layer, &b, &a);
            l.font = &bc_17;
            l.color = lv_color_hex(pl_page_color(p));
            l.text = pl_page_name(p);
            lv_area_t ta = {a.x1 + 2, a.y1 + (h - 16) / 2, a.x2 - 2, a.y2};
            lv_draw_label(layer, &l, &ta);
        }
        return;
    }

    if (u.overlay == OVL_USER) {
        uint32_t pc = pl_view_page_color(PL_USER_FIRST);
        for (int up = 0; up < PL_USER_PAGES; up++) {
            int x, y, w, h, on = up == u.user, n = 0;
            pl_pick_rect(PL_USER_PAGES, up, &x, &y, &w, &h);
            lv_area_t a = {x, y, x + w - 1, y + h - 1};
            lv_draw_fill(layer, &f, &a);
            b.width = on ? 2 : 1;
            b.color = lv_color_hex(on ? pc : C_TILE_BRD);
            lv_draw_border(layer, &b, &a);
            for (int enc = 1; enc < PL_KNOBS; enc++) {
                n += pick_m->user_t[up][enc][0].pe != PL_NONE;
            }
            char title[20], sub[24];
            snprintf(title, sizeof title, "%d  %s", pl_page_bsp(PL_USER_FIRST + up), pl_user_name(up));
            snprintf(sub, sizeof sub, !(pick_m->user_have >> up & 1) ? "" : n ? "%d KNOBS" : "EMPTY", n);
            l.font = &bc_22;
            l.color = lv_color_hex(on ? pc : C_TEXT);
            l.text = title;
            lv_area_t ta = {a.x1 + 2, a.y1 + h / 2 - 22, a.x2 - 2, a.y2};
            lv_draw_label(layer, &l, &ta);
            l.font = &bc_15;
            l.color = lv_color_hex(C_DIM);
            l.text = sub;
            lv_area_t sa = {a.x1 + 2, a.y1 + h / 2 + 8, a.x2 - 2, a.y2};
            lv_draw_label(layer, &l, &sa);
        }
        return;
    }

    if (u.overlay == OVL_MODE) {
        int modes[4], n = pl_nav_modes(pick_m->mode, modes);
        int cur = pick_m->mode ? (pick_m->demo == PL_DEMO_DRONE ? PL_DEMO_DRONE : PL_MODE_LIVE) : pick_m->demo;
        for (int i = 0; i < n; i++) {
            int x, y, w, h, on = modes[i] == cur;
            pl_nav_mode_rect(n, i, &x, &y, &w, &h);
            lv_area_t a = {x, y, x + w - 1, y + h - 1};
            lv_draw_fill(layer, &f, &a);
            b.width = on ? 2 : 1;
            b.color = lv_color_hex(on ? C_ACCENT : C_TILE_BRD);
            lv_draw_border(layer, &b, &a);
            l.font = &bc_32;
            l.color = lv_color_hex(on ? C_ACCENT : C_TEXT);
            l.text = pl_nav_mode_name(pick_m->mode, modes[i]);
            lv_area_t ta = {a.x1 + 2, a.y1 + 20, a.x2 - 2, a.y2};
            lv_draw_label(layer, &l, &ta);
            l.font = &bc_15;
            l.color = lv_color_hex(C_DIM);
            l.text = mode_desc(pick_m->mode, modes[i]);
            lv_area_t sa = {a.x1 + 2, a.y1 + 62, a.x2 - 2, a.y2};
            lv_draw_label(layer, &l, &sa);
        }
        return;
    }

    if (u.overlay == OVL_MORPH) {
        int n = 9, cur = upf_morph();
        for (int s = 0; s < n; s++) {
            int x, y, w, h, on = s == cur;
            pl_pick_rect(n, s, &x, &y, &w, &h);
            lv_area_t a = {x, y, x + w - 1, y + h - 1};
            lv_draw_fill(layer, &f, &a);
            b.width = on ? 2 : 1;
            b.color = lv_color_hex(on ? C_ACCENT : C_TILE_BRD);
            lv_draw_border(layer, &b, &a);
            char num[8];
            snprintf(num, sizeof num, "%d", s);
            l.font = &bc_32;
            l.color = lv_color_hex(on ? C_ACCENT : C_TEXT);
            l.text = num;
            lv_area_t ta = {a.x1 + 2, a.y1 + h / 2 - 26, a.x2 - 2, a.y2};
            lv_draw_label(layer, &l, &ta);
            l.font = &bc_15;
            l.color = lv_color_hex(C_DIM);
            l.text = s == 0 ? "AT NEXT BAR" : s == 1 ? "OVER 1 BAR" : "OVER BARS";
            lv_area_t sa = {a.x1 + 2, a.y1 + h / 2 + 12, a.x2 - 2, a.y2};
            lv_draw_label(layer, &l, &sa);
        }
        return;
    }

    if (u.overlay != OVL_VAL) {
        return;
    }
    int page = u.pick_tp, enc = u.pick_te;
    const uint8_t *raw = pick_m->raw[page];
    int cur = raw[enc], n = pl_pick_count(page, enc, raw);
    uint32_t pc = pl_view_page_color(u.pick_page);
    f.radius = 10;
    b.radius = 10;
    l.font = &bc_17;
    char buf[12];
    for (int s = 0; s < n; s++) {
        int t = pl_pick_value(page, enc, raw, s), x, y, w, h;
        pl_pick_rect(n, s, &x, &y, &w, &h);
        lv_area_t a = {x, y, x + w - 1, y + h - 1};
        lv_draw_fill(layer, &f, &a);
        b.width = t == cur ? 2 : 1;
        b.color = lv_color_hex(t == cur ? pc : C_TILE_BRD);
        lv_draw_border(layer, &b, &a);
        l.text = pl_value_text(page, enc, t, raw, buf);
        l.color = lv_color_hex(t == cur ? pc : C_TEXT);
        lv_area_t ta = {a.x1 + 2, a.y1 + (h - 16) / 2, a.x2 - 2, a.y2};
        lv_draw_label(layer, &l, &ta);
    }
}

// Поверх карточки: у переключателя и триггера - плашка-кнопка на месте дуги, у списка - черта внизу справа.
static void card_draw(lv_event_t *e)
{
    const knob_t *k = lv_event_get_user_data(e);
    if (!k->lab[0] || k->kind == PL_PICK_NONE) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(k->card, &c);
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    if (k->kind == PL_PICK_GRID) {
        f.radius = 2;
        f.color = lv_color_hex(C_MUTED);
        lv_area_t a = {c.x1 + 118, c.y1 + 54, c.x1 + 137, c.y1 + 56};
        lv_draw_fill(layer, &f, &a);
        return;
    }
    int lit = k->lit > 0;
    f.radius = 10;
    f.color = lv_color_hex(lit ? k->color : C_TRACK);
    lv_area_t a = {c.x1 + 8, c.y1 + 16, c.x1 + 59, c.y1 + 49};
    lv_draw_fill(layer, &f, &a);
    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.font = &bc_17;
    l.align = LV_TEXT_ALIGN_CENTER;
    l.color = lv_color_hex(lit ? C_BG : C_TEXT);
    l.text = k->pill;
    l.text_local = 1;
    lv_area_t ta = {a.x1, a.y1 + 8, a.x2, a.y2};
    lv_draw_label(layer, &l, &ta);
}

static void build(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_scrollable(scr, false);

    u.bar = ui_box(scr, 0, 0, W, TOP_H, C_BAR);
    lv_obj_add_event_cb(u.bar, bar_draw, LV_EVENT_DRAW_MAIN, NULL);

    u.tabs = ui_box(scr, 0, TOP_H, W, PL_IA_TABS_H, C_BG);
    lv_obj_add_event_cb(u.tabs, tabs_draw, LV_EVENT_DRAW_MAIN, NULL);

    u.dock = ui_box(scr, 0, DOCK_Y, W, PL_IA_DOCK_H, C_BAR);
    lv_obj_add_event_cb(u.dock, dock_draw, LV_EVENT_DRAW_MAIN, NULL);

    // Сетка 3x5 из 15 карточек 150x66 между полосой вкладок и доком.
    u.grid = ui_box(scr, 0, WORK_Y, W, WORK_H, C_BG);
    for (int i = 0; i < KNOBS; i++) {
        knob_t *k = &u.knob[i];
        int x, y;
        pl_grid_rect(i, &x, &y);
        k->card = ui_box(u.grid, x, y - WORK_Y, PL_CARD_W, PL_CARD_H, C_TILE);
        lv_obj_set_style_radius(k->card, UI_R, 0);
        lv_obj_set_style_border_width(k->card, 1, 0);
        lv_obj_set_style_border_color(k->card, lv_color_hex(C_TILE_BRD), 0);

        k->arc = arc(k->card, KNOB_D, 6);
        lv_obj_set_pos(k->arc, 8, 9);
        k->val = ui_label(k->arc, &bc_17, C_TEXT);
        lv_obj_align(k->val, LV_ALIGN_CENTER, 0, 0);

        k->pdot = ui_box(k->card, 132, 9, 9, 9, C_MUTED);
        lv_obj_set_style_radius(k->pdot, 5, 0);
        lv_obj_set_style_border_width(k->pdot, 2, 0);

        k->name = ui_label(k->card, &bc_15, C_TEXT);
        lv_obj_set_width(k->name, 66);
        lv_obj_set_pos(k->name, 64, 9);

        k->sub = ui_label(k->card, &bc_15, C_MUTED);
        lv_obj_set_size(k->sub, 78, 16);
        lv_label_set_long_mode(k->sub, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(k->sub, 64, 42);

        k->empty_dot = ui_box(k->card, 72, 30, 6, 6, C_TRACK);
        lv_obj_set_style_radius(k->empty_dot, 3, 0);

        lv_obj_add_event_cb(k->card, card_draw, LV_EVENT_DRAW_POST, k);
        k->pill[0] = k->srcs[0] = 0;
        k->v = k->held = k->grabbed = k->kind = k->lit = -1;
        k->color = k->src_color = 0;
        k->lo = k->hi = -1;
        strcpy(k->lab, "?");
    }

    // Захват: круглый медальон с большой дугой в центре рабочей области поверх сетки.
    u.big_box = ui_box(u.grid, PL_CENTER - BIG_BOX / 2, (WORK_H - BIG_BOX) / 2, BIG_BOX, BIG_BOX, C_FIELD);
    lv_obj_set_style_radius(u.big_box, BIG_BOX / 2, 0);
    lv_obj_set_style_border_width(u.big_box, 2, 0);
    lv_obj_set_style_border_color(u.big_box, lv_color_hex(C_RING), 0);

    u.big = arc(u.big_box, BIG_D, 16);
    lv_obj_set_style_arc_color(u.big, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_center(u.big);
    u.big_name = ui_label(u.big, &bc_17, C_DIM);
    lv_obj_align(u.big_name, LV_ALIGN_CENTER, 0, -40);
    u.big_val = ui_label(u.big, &bc_58, C_ACCENT);
    lv_obj_align(u.big_val, LV_ALIGN_CENTER, 0, 4);
    u.big_sub = ui_label(u.big, &bc_15, C_BSP);
    lv_obj_align(u.big_sub, LV_ALIGN_CENTER, 0, 44);

    u.bsp_mark = ui_box(u.big_box, 0, 0, 16, 16, C_BG);
    lv_obj_set_style_bg_opa(u.bsp_mark, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(u.bsp_mark, 8, 0);
    lv_obj_set_style_border_color(u.bsp_mark, lv_color_hex(C_BSP), 0);
    lv_obj_set_style_border_width(u.bsp_mark, 3, 0);

    uc_build(scr);
    ug_build(scr);
    us_build(scr);
    up_build(scr);
    upf_build(scr);

    u.pick = ui_box(scr, 0, TOP_H, W, W - TOP_H, C_BG);
    lv_obj_add_event_cb(u.pick, pick_draw, LV_EVENT_DRAW_MAIN, NULL);
    ui_show(u.pick, 0);

    // Всплывашка от BSP поверх всего: тёмная плашка с цветной рамкой страницы.
    u.toast = ui_box(scr, 20, W - 82, W - 40, 68, C_TOAST);
    lv_obj_set_style_radius(u.toast, 16, 0);
    lv_obj_set_style_border_width(u.toast, 2, 0);
    lv_obj_set_style_border_color(u.toast, lv_color_hex(C_ACCENT), 0);
    u.toast_name = ui_label(u.toast, &bc_22, C_TEXT);
    lv_obj_align(u.toast_name, LV_ALIGN_LEFT_MID, 16, 0);
    u.toast_val = ui_label(u.toast, &bc_32, C_ACCENT);
    lv_obj_align(u.toast_val, LV_ALIGN_RIGHT_MID, -16, 0);
    for (int i = 0; i < PL_TARGETS; i++) {
        int cw = (W - 40 - 32) / PL_TARGETS;
        u.chip_page[i] = ui_label(u.toast, &bc_15, C_DIM);
        lv_obj_set_pos(u.chip_page[i], 16 + i * cw, 46);
        u.chip_val[i] = ui_label(u.toast, &bc_17, C_TEXT);
        lv_obj_set_size(u.chip_val[i], cw - 6, 18);
        lv_label_set_long_mode(u.chip_val[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(u.chip_val[i], 16 + i * cw, 64);
    }
    u.toast_tall = 1;

    u.group = PL_GRP_PERF;
    u.page = 1;
    u.grab = u.pick_page = u.drawn_grab = u.flash_cell = -1;
    u.drawn_tabs = u.drawn_dock = u.drawn_pick = -1;
    u.big_sym = -1;
}

static void style_knob_grab(knob_t *k, int grabbed)
{
    if (k->grabbed == grabbed) {
        return;
    }
    k->grabbed = grabbed;
    if (!k->lab[0]) {
        return;
    }
    lv_obj_set_style_border_width(k->card, grabbed ? 2 : 1, 0);
    lv_obj_set_style_border_color(k->card, lv_color_hex(grabbed ? k->color : C_TILE_BRD), 0);
    lv_obj_set_style_text_color(k->name, lv_color_hex(grabbed ? k->color : C_TEXT), 0);
}

static void card_setup(knob_t *k, int idx, const pl_cell_t *c, int kind)
{
    int on = c->label != NULL, button = kind == PL_PICK_TOGGLE || kind == PL_PICK_TRIGGER;
    snprintf(k->lab, sizeof k->lab, "%s", on ? c->label : "");
    lv_obj_set_style_bg_color(k->card, lv_color_hex(on ? C_TILE : C_TILE_EMPTY), 0);
    ui_show(k->arc, on && !button);
    k->kind = on ? kind : PL_PICK_NONE;
    k->lit = -1;
    lv_obj_invalidate(k->card);
    ui_show(k->name, on);
    ui_show(k->sub, on);
    ui_show(k->pdot, on);
    ui_show(k->empty_dot, !on);
    if (on) {
        lv_arc_set_range(k->arc, c->lo, c->hi);
        lv_arc_set_mode(k->arc, c->bipolar ? LV_ARC_MODE_SYMMETRICAL : LV_ARC_MODE_NORMAL);
        if (k->color != c->color) {
            lv_obj_set_style_arc_color(k->arc, lv_color_hex(c->color), LV_PART_INDICATOR);
        }
        ui_text(k->name, k->lab);
    }
    snprintf(k->srcs, sizeof k->srcs, "%s", on && c->src ? c->src : "");
    if (on) {
        char sub[16];
        if (k->srcs[0]) {
            snprintf(sub, sizeof sub, "%s", k->srcs);
        } else {
            snprintf(sub, sizeof sub, "E%d", idx + 2);
        }
        ui_text(k->sub, sub);
    }
    k->src_color = c->src_color;
    k->color = c->color;
    k->lo = c->lo, k->hi = c->hi, k->bipolar = c->bipolar;
    k->v = k->held = -1;
    k->grabbed = -1;
    style_knob_grab(k, 0);
    u.drawn_grab = -1;
}

static void place_mark(int pos, int lo, int hi)
{
    float f = hi > lo ? (float)(pos - lo) / (float)(hi - lo) : 0.0f;
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    float a = (135.0f + 270.0f * f) * (float)M_PI / 180.0f;
    float r = (BIG_D - 14) / 2.0f;
    int c = BIG_BOX / 2;
    lv_obj_set_pos(u.bsp_mark, c + (int)lroundf(r * cosf(a)) - 8, c + (int)lroundf(r * sinf(a)) - 8);
}

// Позиция энкодера BSP для слота: на вкладке MIX событие от синта приходит с индексом целевого энкодера.
static int slot_bsp_pos(const pl_model_t *m, int page, int enc)
{
    int tp, te;
    if (pl_view_mix(m, page, enc, &tp, &te) && m->bsp_pos[te] != 0xFF) {
        return m->bsp_pos[te];
    }
    return m->bsp_pos[enc];
}

static void draw_page(const pl_model_t *m)
{
    int page = view_page();
    pl_cell_t cell;
    for (int i = 0; i < KNOBS; i++) {
        knob_t *k = &u.knob[i];
        int enc = i + 1;
        pl_view_cell(m, page, enc, &cell);
        int tp, te, kind = pl_pick_view(m, page, enc, &tp, &te);
        if (k->kind != kind || strcmp(k->lab, cell.label ? cell.label : "") != 0 || k->color != cell.color ||
            k->lo != cell.lo || k->hi != cell.hi || k->bipolar != cell.bipolar ||
            strcmp(k->srcs, cell.src ? cell.src : "") != 0 || k->src_color != cell.src_color) {
            card_setup(k, i, &cell, kind);
        }
        if (!k->lab[0]) {
            continue;
        }
        if (cell.v != k->v) {
            lv_arc_set_value(k->arc, cell.v);
            ui_text(k->val, cell.text);
            if (kind == PL_PICK_TOGGLE || kind == PL_PICK_TRIGGER) {
                snprintf(k->pill, sizeof k->pill, "%s", kind == PL_PICK_TRIGGER ? "GO" : cell.text);
                lv_obj_invalidate(k->card);
            }
            k->v = cell.v;
        }
        int lit = kind == PL_PICK_TRIGGER  ? (u.flash_cell == i && (int32_t)(u.flash_until - u.now) > 0)
                  : kind == PL_PICK_TOGGLE ? pl_pick_next(tp, te, m->raw[tp], cell.v) < cell.v
                                           : 0;
        if (lit != k->lit) {
            k->lit = lit;
            lv_obj_invalidate(k->card);
        }
        int bpos = slot_bsp_pos(m, page, enc);
        int held = cell.held && bpos != 0xFF;
        if (held != k->held) {
            uint32_t lc = held ? C_BSP : k->color;
            lv_obj_set_style_bg_color(k->pdot, lv_color_hex(lc), 0);
            lv_obj_set_style_border_color(k->pdot, lv_color_hex(ui_mix(lc, C_TILE, 141)), 0);
            uint32_t sc = held ? C_BSP : k->srcs[0] ? ui_mix(k->src_color, C_TILE, 77) : C_MUTED;
            lv_obj_set_style_text_color(k->sub, lv_color_hex(sc), 0);
            k->held = held;
        }
    }

    int g = u.grab;
    if (g != u.drawn_grab) {
        if (u.drawn_grab >= 0 && u.drawn_grab < KNOBS) {
            style_knob_grab(&u.knob[u.drawn_grab], 0);
        }
        if (g >= 0 && g < KNOBS) {
            style_knob_grab(&u.knob[g], 1);
        }
        u.drawn_grab = g;
    }

    ui_show(u.big_box, g >= 0);
    if (g < 0) {
        return;
    }
    int enc = g + 1;
    pl_view_cell(m, page, enc, &cell);
    if (u.big_sym != cell.bipolar) {
        lv_arc_set_mode(u.big, cell.bipolar ? LV_ARC_MODE_SYMMETRICAL : LV_ARC_MODE_NORMAL);
        u.big_sym = cell.bipolar;
    }
    if (lv_arc_get_min_value(u.big) != cell.lo || lv_arc_get_max_value(u.big) != cell.hi) {
        lv_arc_set_range(u.big, cell.lo, cell.hi);
    }
    if (lv_arc_get_value(u.big) != cell.v) {
        lv_arc_set_value(u.big, cell.v);
    }
    uint32_t c = u.knob[g].color;
    if (u.big_color != c) {
        lv_obj_set_style_arc_color(u.big, lv_color_hex(c), LV_PART_INDICATOR);
        lv_obj_set_style_text_color(u.big_val, lv_color_hex(c), 0);
        lv_obj_set_style_border_color(u.big_box, lv_color_hex(c), 0);
        u.big_color = c;
    }
    ui_text(u.big_val, cell.text);
    char head[40];
    if (cell.src && (pl_mix_is(page) || (is_user(page) && pl_view_is_alias(m, page - PL_USER_FIRST, enc)))) {
        snprintf(head, sizeof head, "E%d  %s / %s", enc + 1, cell.src, u.knob[g].lab);
    } else {
        snprintf(head, sizeof head, "E%d  %s", enc + 1, u.knob[g].lab);
    }
    ui_text(u.big_name, head);
    int bpos = slot_bsp_pos(m, page, enc);
    int held = cell.held && bpos != 0xFF;
    ui_show(u.bsp_mark, held);
    if (held) {
        place_mark(bpos, cell.lo, cell.hi);
        char bsp_txt[20];
        snprintf(bsp_txt, sizeof bsp_txt, "BSP %d", bpos);
        ui_text(u.big_sub, bsp_txt);
    } else {
        ui_text(u.big_sub, "");
    }
}

static void toast_shape(int tall)
{
    if (u.toast_tall == tall) {
        return;
    }
    u.toast_tall = tall;
    int h = tall ? 96 : 68;
    lv_obj_set_pos(u.toast, 20, W - 14 - h);
    lv_obj_set_height(u.toast, h);
    lv_obj_align(u.toast_name, tall ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 16, tall ? 8 : 0);
    lv_obj_align(u.toast_val, tall ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_RIGHT_MID, -16, tall ? 2 : 0);
    for (int i = 0; i < PL_TARGETS; i++) {
        ui_show(u.chip_page[i], tall);
        ui_show(u.chip_val[i], tall);
    }
}

static void draw_chips(const pl_model_t *m, int page, int enc)
{
    for (int i = 0; i < PL_TARGETS; i++) {
        int te, tp = pl_view_target(m, page, enc, i, &te);
        pl_cell_t t;
        if (tp < 0) {
            ui_text(u.chip_page[i], "");
            ui_text(u.chip_val[i], "");
            continue;
        }
        pl_view_cell(m, tp, te, &t);
        char line[28];
        snprintf(line, sizeof line, "%s %s", t.label ? t.label : "?", t.text);
        ui_text(u.chip_page[i], pl_view_page_name(tp));
        ui_text(u.chip_val[i], line);
        lv_obj_set_style_text_color(u.chip_val[i], lv_color_hex(pl_page_color(tp)), 0);
    }
}

static void draw_toast(const pl_model_t *m, uint32_t ms)
{
    int page = m->toast_page, enc = m->toast_enc;
    int on = m->toast_until_ms && (int32_t)(m->toast_until_ms - ms) > 0;
    int vp = view_page();
    if (!overlay_on() && u.grab >= 0 && is_user(vp) && !pl_view_is_alias(m, vp - PL_USER_FIRST, u.grab + 1)) {
        page = vp, enc = u.grab + 1, on = 1;
    }
    pl_cell_t cell;
    if (on) {
        pl_view_cell(m, page, enc, &cell);
        on = cell.label != NULL;
    }
    ui_show(u.toast, on);
    if (!on) {
        return;
    }
    int user = is_user(page), alias = user && pl_view_is_alias(m, page - PL_USER_FIRST, enc);
    int te, tp = alias ? pl_view_target(m, page, enc, 0, &te) : -1;
    uint32_t pc = pl_view_page_color(tp >= 0 ? tp : page);
    if (u.toast_color != pc) {
        lv_obj_set_style_border_color(u.toast, lv_color_hex(pc), 0);
        lv_obj_set_style_text_color(u.toast_val, lv_color_hex(pc), 0);
        u.toast_color = pc;
    }
    toast_shape(user && !alias);
    char line[48];
    snprintf(line, sizeof line, "%s / %s", tp >= 0 ? pl_view_page_name(tp) : pl_view_page_name(page), cell.label);
    ui_text(u.toast_name, line);
    ui_text(u.toast_val, cell.text);
    if (user && !alias) {
        draw_chips(m, page, enc);
    }
}

static void release_all(pl_model_t *m)
{
    uc_xy_release(m);
    ug_release(m);
    us_release();
    upf_release(m);
    u.grab = -1;
}

void ui_open_morph(void)
{
    u.overlay = OVL_MORPH;
}

// Открыть группу и вкладку: на вкладке с ручками сообщить страницу синту, чтобы энкодеры BSP крутили её.
static void select_tab(pl_model_t *m, int group, int tab, uint32_t ms, int from_bsp)
{
    release_all(m);
    u.overlay = OVL_NONE;
    u.ed.state = PL_EDIT_OFF;
    u.dish = 0;
    u.flash_cell = -1;
    u.group = group;
    if (tab >= 0 && tab < pl_ia_tabs(group)) {
        u.tab[group] = tab;
    }
    const pl_tab_t *x = cur_tab();
    if (!x) {
        return;
    }
    if (x->kind == PL_TAB_SEQ) {
        us_set_synth(x->page);
        return;
    }
    u.page = x->kind == PL_TAB_USER ? PL_USER_FIRST + u.user : x->page;
    if (!from_bsp) {
        link_page(u.page);
        pl_model_page_sent(m, ms);
    }
}

// Страница для повтора PL_PAGE синту (-1 - экран без своей страницы ручек).
static int active_bsp_page(void)
{
    if (overlay_on() || u.group == PL_GRP_PERF) {
        return -1;
    }
    if (u.dish) {
        return PL_PAGE_PETRI;
    }
    const pl_tab_t *t = cur_tab();
    return t && t->kind != PL_TAB_SEQ ? u.page : -1;
}

// Правка ручки: на вкладке MIX шлём прямо в цель на штатной странице.
static void set_knob(pl_model_t *m, int page, int enc, int v)
{
    int tp, te;
    if (pl_mix_is(page)) {
        if (!pl_view_mix(m, page, enc, &tp, &te)) {
            return;
        }
        page = tp, enc = te;
    }
    if (pl_model_local_set(m, page, enc, v)) {
        link_set(page, enc, v);
    }
}

static void card_tap(pl_model_t *m, int cell, uint32_t ms)
{
    int enc = cell + 1, tp, te;
    switch (pl_pick_view(m, u.page, enc, &tp, &te)) {
    case PL_PICK_GRID:
        u.pick_page = u.page, u.pick_enc = enc, u.pick_tp = tp, u.pick_te = te;
        u.overlay = OVL_VAL;
        break;
    case PL_PICK_TOGGLE:
        set_knob(m, u.page, enc, pl_pick_next(tp, te, m->raw[tp], m->raw[tp][te]));
        break;
    case PL_PICK_TRIGGER: {
        int lo, hi;
        pl_knob_range(tp, te, m->raw[tp], &lo, &hi);
        set_knob(m, u.page, enc, hi);
        set_knob(m, u.page, enc, lo);
        u.flash_cell = cell;
        u.flash_until = ms + FLASH_MS;
        break;
    }
    default:
        break;
    }
}

static void knob_input(pl_model_t *m, int ev, int down, int x, int y, int x0, int y0)
{
    int moved = abs(x - x0) > PL_SLOP_PX || abs(y - y0) > PL_SLOP_PX;
    int drag = u.press.state == 2 || (u.press.state == 3 && moved);
    if (down && u.grab < 0 && (ev == PL_PRESS_HOLD || drag)) {
        int i = pl_grid_cell(x0, y0), tp, te;
        pl_cell_t cell;
        pl_view_cell(m, u.page, i + 1, &cell);
        int kind = pl_pick_view(m, u.page, i + 1, &tp, &te);
        int can = kind == PL_PICK_NONE || pl_pick_grab(tp, te, m->raw[tp], !drag);
        if (i >= 0 && i < KNOBS && cell.label && m->have && can) {
            pl_turn_begin(&u.turn, x, y - RING_DY, cell.v, cell.lo, cell.hi);
            pl_turn_skip(&u.turn, cell.skip);
            u.grab = i;
            u.sent = cell.v;
        }
    }
    if (!down || ev == PL_PRESS_RELEASE || ev == PL_PRESS_END || ev == PL_PRESS_TAP) {
        u.grab = -1;
    }
    if (u.grab >= 0 && down) {
        int v = pl_turn_move(&u.turn, x, y - RING_DY);
        if (v != u.sent) {
            set_knob(m, u.page, u.grab + 1, v);
            u.sent = v;
        }
    }
}

static void edit_input(pl_model_t *m, int ev, int down, int x, int y, int x0, int y0, int x1, int y1, int tap,
                       int nav)
{
    pl_custom_edit_t out;
    if (y0 < TOP_H) {
        if (nav == PL_NAV_CLOSE) {
            u.ed.state = PL_EDIT_OFF;
        } else if (nav == PL_NAV_ACTION) {
            pl_edit_back(&u.ed);
        }
        return;
    }
    int cell = pl_grid_cell(x0, y0);
    switch (u.ed.state) {
    case PL_EDIT_SLOTS:
        if (tap && cell >= 0 && cell == pl_grid_cell(x1, y1)) {
            pl_edit_slot(&u.ed, cell + 1);
        }
        break;
    case PL_EDIT_PICK_PAGE: {
        int p = pl_pick_at(PL_PAGES, x0, y0);
        if (tap && p >= 0 && p == pl_pick_at(PL_PAGES, x1, y1)) {
            pl_edit_page(&u.ed, p);
        }
        break;
    }
    case PL_EDIT_PICK_KNOB:
        if (tap && cell >= 0 && cell == pl_grid_cell(x1, y1) && pl_edit_knob(&u.ed, m, cell + 1, &out)) {
            link_custom_edit(&out);
        }
        break;
    default:
        uc_edit_input(m, &u.ed, ev, down, x, y, x0, y0);
        break;
    }
}

static void overlay_input(pl_model_t *m, int tap, int x0, int y0, int x1, int y1, int nav, uint32_t ms)
{
    if (!tap) {
        return;
    }
    if (y0 < TOP_H) {
        if (nav == PL_NAV_CLOSE) {
            u.overlay = OVL_NONE;
        }
        return;
    }
    if (u.overlay == OVL_VAL) {
        const uint8_t *raw = m->raw[u.pick_tp];
        int n = pl_pick_count(u.pick_tp, u.pick_te, raw);
        int s = pl_pick_at(n, x0, y0);
        int t = s == pl_pick_at(n, x1, y1) ? pl_pick_value(u.pick_tp, u.pick_te, raw, s) : -1;
        if (t >= 0) {
            set_knob(m, u.pick_page, u.pick_enc, t);
        }
        u.overlay = OVL_NONE;
    } else if (u.overlay == OVL_USER) {
        int up = pl_pick_at(PL_USER_PAGES, x0, y0);
        if (up >= 0 && up == pl_pick_at(PL_USER_PAGES, x1, y1)) {
            u.user = up;
            select_tab(m, PL_GRP_FX, u.tab[PL_GRP_FX], ms, 0);
        } else {
            u.overlay = OVL_NONE;
        }
    } else if (u.overlay == OVL_MODE) {
        int modes[4], n = pl_nav_modes(m->mode, modes);
        int i = pl_nav_mode_at(n, x0, y0);
        if (i >= 0 && i == pl_nav_mode_at(n, x1, y1) && pl_model_linked(m, ms) && pl_mode_ok(m->mode, modes[i])) {
            link_mode(modes[i]);
            m->demo = modes[i];
        }
        u.overlay = OVL_NONE;
    } else if (u.overlay == OVL_MORPH) {
        int n = 9;
        int s = pl_pick_at(n, x0, y0);
        if (s >= 0 && s == pl_pick_at(n, x1, y1)) {
            upf_set_morph(s);
        }
        u.overlay = OVL_NONE;
    }
}

static void toggle_seq_play(pl_model_t *m)
{
    int run = seq_running(m);
    for (int s = 0; s < PL_SEQ_SYNTHS; s++) {
        if ((m->seq_have >> s & 1) && pl_model_seq_ctrl(m, s, !run, m->seq[s].edit)) {
            link_seq_ctrl(s, m->seq[s].run, m->seq[s].edit);
        }
    }
}

static void input(pl_model_t *m, int down, int x, int y, uint32_t ms)
{
    int ev = pl_press_step(&u.press, down, x, y, ms);
    int x0, y0, x1, y1;
    pl_press_where(&u.press, &x0, &y0);
    pl_press_last(&u.press, &x1, &y1);
    int tap = pl_press_tap(ev) && u.grab < 0;
    int bar = bar_kind();
    int nav = tap ? pl_nav_tap(bar, x0, y0, x1, y1) : PL_NAV_NONE;

    if (editing()) {
        edit_input(m, ev, down, x, y, x0, y0, x1, y1, tap, nav);
    } else if (u.overlay != OVL_NONE) {
        overlay_input(m, tap, x0, y0, x1, y1, nav, ms);
    } else if (us_picking()) {
        if (y0 < TOP_H) {
            if (nav == PL_NAV_CLOSE) {
                us_close_pick();
            }
        } else {
            us_input(m, down, x, y, x0, y0, ms);
        }
    } else {
        int has_tabs = pl_ia_tabs(u.group) > 0;
        int zone = pl_nav_zone(y0, has_tabs);
        if (zone == PL_ZONE_TOP) {
            if (down) {
                if (!u.play_touch && pl_nav_at(PL_BAR_MAIN, x0) == PL_NAV_PLAY) {
                    u.play_touch = 1;
                    u.bpm_drag = 0;
                    u.bpm0 = (m->seq_have & 1) ? m->seq[0].bpm : (m->seq_have & 2) ? m->seq[1].bpm : 120;
                }
                if (u.play_touch && pl_nav_drag(PL_BAR_MAIN, x0, y0, x)) {
                    int ext = 0;
                    current_tempo(m, &ext);
                    if (!ext && m->seq_have) {
                        u.bpm_drag = 1;
                        int bpm = pl_seq_bpm_drag(u.bpm0, x0, x);
                        if (bpm != m->seq[0].bpm && pl_model_seq_bpm(m, bpm)) {
                            link_seq_bpm(bpm);
                        }
                    }
                }
            } else {
                int dragged = u.bpm_drag;
                u.play_touch = u.bpm_drag = 0;
                if (nav == PL_NAV_PLAY && !dragged) {
                    toggle_seq_play(m);
                } else if (nav == PL_NAV_MODE) {
                    release_all(m);
                    u.overlay = OVL_MODE;
                }
            }
        } else if (zone == PL_ZONE_DOCK) {
            int g = tap ? pl_nav_dock_tap(x0, y0, x1, y1) : -1;
            if (g >= 0) {
                select_tab(m, g, u.tab[g], ms, 0);
            }
        } else if (zone == PL_ZONE_TABS) {
            if (tap) {
                int t = pl_nav_tab_tap(u.group, x0, y0, x1, y1);
                const char *acts[2];
                int act_on[2];
                int n_acts = tab_actions(m, acts, act_on);
                int a = pl_nav_act_tap(u.group, n_acts, x0, y0, x1, y1);
                if (t >= 0) {
                    const pl_tab_t *tab = pl_ia_tab(u.group, t);
                    if (t == u.tab[u.group] && tab && tab->kind == PL_TAB_USER) {
                        release_all(m);
                        u.overlay = OVL_USER;
                    } else {
                        select_tab(m, u.group, t, ms, 0);
                    }
                } else if (a >= 0) {
                    if (!strcmp(acts[a], "XY")) {
                        uc_xy_release(m);
                        u.grab = -1;
                        u.xy_view ^= (uint8_t)(1 << u.user);
                    } else if (!strcmp(acts[a], "EDIT")) {
                        release_all(m);
                        pl_edit_start(&u.ed, u.user);
                    } else if (!strcmp(acts[a], "DISH")) {
                        release_all(m);
                        u.dish = 1;
                        link_page(PL_PAGE_PETRI);
                        pl_model_page_sent(m, ms);
                    } else if (!strcmp(acts[a], "KNOBS")) {
                        u.dish = 0;
                        link_page(u.page);
                        pl_model_page_sent(m, ms);
                    }
                }
            }
        } else if (u.group == PL_GRP_PERF) {
            upf_input(m, ev, down, x, y, x0, y0, x1, y1, ms - u.press.t0, ms);
        } else {
            const pl_tab_t *t = cur_tab();
            if (u.dish) {
                up_input(down, x, y, x0, y0);
            } else if (t && t->kind == PL_TAB_GRIDS) {
                ug_input(m, down, x, y, x0, y0, ms);
            } else if (t && t->kind == PL_TAB_SEQ) {
                us_input(m, down, x, y, x0, y0, ms);
            } else if (xy_on()) {
                uc_xy_input(m, u.page, ev, down, x, y, x0, y0, ms);
            } else {
                int cell = pl_grid_cell(x0, y0);
                if (tap && m->have && cell >= 0 && cell == pl_grid_cell(x1, y1)) {
                    card_tap(m, cell, ms);
                }
                knob_input(m, ev, down, x, y, x0, y0);
            }
        }
    }

    if (m->goto_page >= 0 && !down && u.grab < 0 && !editing()) {
        int g, t;
        if (pl_ia_find(m->goto_page, &g, &t)) {
            if (is_user(m->goto_page)) {
                u.user = m->goto_page - PL_USER_FIRST;
            }
            select_tab(m, g, t, ms, 1);
        }
        m->goto_page = -1;
    }
}

static void check_bar_redraw(const pl_model_t *m, uint32_t ms)
{
    char key[96];
    if (bar_kind() == PL_BAR_OVERLAY) {
        char nav[40];
        uint32_t nc;
        const char *act;
        overlay_title(m, nav, &nc, &act);
        snprintf(key, sizeof key, "O:%s:%s:%u", nav, act, (unsigned)nc);
    } else {
        const char *title;
        char sub[24];
        main_title(m, &title, sub);
        int ext = 0, bpm = current_tempo(m, &ext);
        int linked = pl_model_linked(m, ms);
        const char *st = pl_view_status(m, linked);
        int ld_ok = m->have_load && ms - m->load_ms <= LOAD_STALE_MS;
        snprintf(key, sizeof key, "M:%d:%s:%s:%d:%d:%d:%d:%s:%d:%d:%d:%d:%d", u.group, title, sub, seq_running(m), bpm,
                 ext, u.bpm_drag, st, linked, ld_ok, ld_ok ? m->load.guard : 0, ld_ok ? m->load.c1_worst : 0,
                 ld_ok ? m->load.c0_worst : 0);
    }
    if (strcmp(key, u.drawn_bar) != 0) {
        snprintf(u.drawn_bar, sizeof u.drawn_bar, "%s", key);
        lv_obj_invalidate(u.bar);
    }
}

static void check_bars_redraw(const pl_model_t *m, uint32_t ms)
{
    check_bar_redraw(m, ms);
    const char *acts[2];
    int act_on[2];
    int n_acts = tab_actions(m, acts, act_on);
    int tkey = u.group | u.tab[u.group] << 4 | u.user << 8 | u.dish << 12 | n_acts << 14 |
               (n_acts > 0 ? act_on[0] << 16 : 0);
    if (tkey != u.drawn_tabs) {
        u.drawn_tabs = tkey;
        lv_obj_invalidate(u.tabs);
    }
    int bsp_grp = -1, bsp_tab = -1;
    if (m->have && m->mode == 1) {
        pl_ia_find(m->bsp_page, &bsp_grp, &bsp_tab);
    }
    int dkey = u.group | (bsp_grp + 1) << 4;
    if (dkey != u.drawn_dock) {
        u.drawn_dock = dkey;
        lv_obj_invalidate(u.dock);
    }
}

static int pick_state_key(const pl_model_t *m)
{
    if (editing() && u.ed.state == PL_EDIT_PICK_PAGE) {
        return 0x100000;
    }
    if (u.overlay == OVL_USER) {
        int k = 0x200000 | u.user | m->user_have << 4;
        for (int up = 0; up < PL_USER_PAGES; up++) {
            for (int e = 1; e < PL_KNOBS; e++) {
                k ^= (m->user_t[up][e][0].pe != PL_NONE) << (8 + up);
            }
        }
        return k;
    }
    if (u.overlay == OVL_MODE) {
        return 0x300000 | m->mode << 4 | m->demo;
    }
    if (u.overlay == OVL_MORPH) {
        return 0x500000 | upf_morph();
    }
    if (u.overlay == OVL_VAL) {
        return 0x400000 | ((u.pick_tp * 16 + u.pick_te) * 128 + m->raw[u.pick_tp][u.pick_te]);
    }
    return -1;
}

void ui_start(void)
{
    lvgl_port_lock(0);
    build();
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI("panel", "lvgl heap: used %d%%, free %u of %u, biggest %u; internal RAM free %u", mon.used_pct,
             (unsigned)mon.free_size, (unsigned)mon.total_size, (unsigned)mon.free_biggest_size,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    lvgl_port_unlock();
}

void ui_step(pl_model_t *m, int down, int x, int y, uint32_t ms)
{
    input(m, down, x, y, ms);
    int bp = active_bsp_page();
    if (bp >= 0 && pl_model_page_resend(m, bp, ms)) {
        link_page(bp);
        pl_model_page_sent(m, ms);
    }
    if (!lvgl_port_lock(1)) {
        return;
    }
    u.now = ms;
    pick_m = m;
    int ovl = overlay_on();
    int pick_vis = (editing() && u.ed.state == PL_EDIT_PICK_PAGE) || u.overlay != OVL_NONE;
    int edit_grid = editing() && (u.ed.state == PL_EDIT_SLOTS || u.ed.state == PL_EDIT_PICK_KNOB);
    const pl_tab_t *t = cur_tab();
    int show_tabs = !ovl && pl_ia_tabs(u.group) > 0;
    int show_perf = !ovl && u.group == PL_GRP_PERF;
    int show_dish = !ovl && u.group != PL_GRP_PERF && u.dish;
    int show_grids = !ovl && !u.dish && t && t->kind == PL_TAB_GRIDS;
    int show_seq = !ovl && !u.dish && t && t->kind == PL_TAB_SEQ;
    int show_xy = !ovl && !u.dish && xy_on();
    int show_grid = edit_grid || (!ovl && !u.dish && t && (t->kind == PL_TAB_PAGE || (t->kind == PL_TAB_USER && !show_xy)));

    ui_show(u.tabs, show_tabs);
    ui_show(u.dock, !ovl);
    ui_show(u.grid, show_grid);
    ui_show(u.pick, pick_vis);
    upf_show(show_perf);
    uc_xy_show(show_xy);
    ug_show(show_grids);
    us_show(show_seq || us_picking());
    up_show(show_dish);
    uc_edit_draw(m, &u.ed);

    if (pick_vis) {
        int pk = pick_state_key(m);
        if (pk != u.drawn_pick) {
            u.drawn_pick = pk;
            lv_obj_invalidate(u.pick);
        }
    } else if (show_perf) {
        upf_draw(m, ms);
    } else if (show_xy) {
        uc_xy_draw(m, u.page);
    } else if (show_grid) {
        draw_page(m);
    } else if (show_grids) {
        ug_draw(m);
    } else if (show_seq || us_picking()) {
        us_draw(m);
    } else if (show_dish) {
        up_draw(m, ms);
    }

    check_bars_redraw(m, ms);
    draw_toast(m, ms);
    lvgl_port_unlock();
}
