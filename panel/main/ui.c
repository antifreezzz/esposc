#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_lvgl_port.h"
#include "link.h"
#include "pl_gesture.h"
#include "pl_labels.h"

// Вариант Б: модульная сетка 3x5 (все 15 карточек - ручки E2..E16) + круговой захват в центре.

#define W 480
#define TOP_H 40    // верхняя полоса: меню и быстрое листание страниц
#define RING_DY 18  // центр вращения (240, 258) - посередине рабочей области 40..480
#define RING_CY (PL_CENTER + RING_DY)
#define KNOB_D 56   // дуга внутри карточки сетки
#define BIG_BOX 244 // круглый диск подложки под большую дугу в центре
#define BIG_D 210   // большая дуга захваченной ручки
#define KNOBS 15    // энкодеры 2..16 (ячейки 0..14)

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

enum { MENU, PAGE };

typedef struct {
    lv_obj_t *card, *num, *arc, *val, *name, *pdot, *empty_dot;
    int v, held, grabbed; // нарисовано
    uint32_t color;
    const char *label;
} knob_t;

static struct {
    int screen, page, grab; // grab - индекс ручки 0..14 или -1
    pl_press_t press;
    pl_turn_t turn;
    int sent;  // последнее отправленное значение захваченной ручки
    int dirty; // сменился экран или страница: перестроить под локом
    lv_obj_t *bar_left, *bar_nav, *bar_right;
    lv_obj_t *menu, *grid;
    lv_obj_t *tile[PL_PAGES], *tile_sub[PL_PAGES];
    int drawn_bsp_page;
    knob_t knob[KNOBS];
    lv_obj_t *big_box, *big, *big_val, *big_name, *big_sub, *bsp_mark;
    uint32_t big_color;
    int big_sym;
    lv_obj_t *toast, *toast_name, *toast_val;
    uint32_t toast_color;
    int drawn_type, drawn_grab;
} u;

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg)
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

static void show(lv_obj_t *o, int on)
{
    if (lv_obj_is_hidden(o) == !!on) {
        lv_obj_set_hidden(o, !on);
    }
}

// Текст только если поменялся: каждая установка инвалидирует область LVGL.
static void text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s) != 0) {
        lv_label_set_text(l, s);
    }
}

// Координаты карточки i (0..14) в сетке 3x5 внутри контейнера u.grid (480 x 440).
static void cell_xy(int i, int *x, int *y)
{
    *x = 9 + (i % 3) * 156;
    *y = 6 + (i / 3) * 86;
}

static void build(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *bar = box(scr, 0, 0, W, TOP_H, C_BAR);
    u.bar_left = label(bar, &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(u.bar_left, LV_ALIGN_LEFT_MID, 14, 0);
    u.bar_nav = label(bar, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_align(u.bar_nav, LV_ALIGN_CENTER, 0, 0);
    u.bar_right = label(bar, &lv_font_montserrat_14, C_ACCENT);
    lv_obj_align(u.bar_right, LV_ALIGN_RIGHT_MID, -14, 0);

    // Меню: 2 столбца по 5 плиток с цветной полоской и сводкой состояния.
    u.menu = box(scr, 0, TOP_H, W, W - TOP_H, C_BG);
    for (int p = 0; p < PL_PAGES; p++) {
        uint32_t pc = pl_page_color(p);
        lv_obj_t *t = box(u.menu, 12 + (p % 2) * 234, 10 + (p / 2) * 84, 222, 74, C_TILE);
        lv_obj_set_style_radius(t, 10, 0);
        lv_obj_set_style_border_width(t, 1, 0);
        lv_obj_set_style_border_color(t, lv_color_hex(C_TILE_BRD), 0);

        lv_obj_t *stripe = box(t, 0, 12, 4, 50, pc);
        lv_obj_set_style_radius(stripe, 2, 0);

        lv_obj_t *num = label(t, &lv_font_montserrat_28, pc);
        lv_label_set_text_fmt(num, "%d", p);
        lv_obj_align(num, LV_ALIGN_LEFT_MID, 16, 0);

        lv_obj_t *name = label(t, &lv_font_montserrat_20, C_TEXT);
        lv_label_set_text(name, pl_page_name(p));
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 54, 12);

        u.tile_sub[p] = label(t, &lv_font_montserrat_12, C_DIM);
        lv_obj_align(u.tile_sub[p], LV_ALIGN_BOTTOM_LEFT, 55, -11);
        u.tile[p] = t;
    }

    // Страница: сетка 3x5 из 15 карточек ручек (энкодеры 2..16).
    u.grid = box(scr, 0, TOP_H, W, W - TOP_H, C_BG);
    for (int i = 0; i < KNOBS; i++) {
        knob_t *k = &u.knob[i];
        int x, y;
        cell_xy(i, &x, &y);
        k->card = box(u.grid, x, y, 150, 80, C_TILE);
        lv_obj_set_style_radius(k->card, 10, 0);
        lv_obj_set_style_border_width(k->card, 1, 0);
        lv_obj_set_style_border_color(k->card, lv_color_hex(C_TILE_BRD), 0);

        k->arc = arc(k->card, KNOB_D, 6);
        lv_obj_set_pos(k->arc, 8, 12);
        k->val = label(k->arc, &lv_font_montserrat_14, C_TEXT);
        lv_obj_align(k->val, LV_ALIGN_CENTER, 0, 0);

        k->num = label(k->card, &lv_font_montserrat_12, C_MUTED);
        lv_label_set_text_fmt(k->num, "E%d", i + 2);
        lv_obj_set_pos(k->num, 72, 14);

        k->pdot = box(k->card, 134, 10, 7, 7, C_MUTED);
        lv_obj_set_style_radius(k->pdot, 4, 0);

        k->name = label(k->card, &lv_font_montserrat_12, C_DIM);
        lv_obj_set_width(k->name, 74);
        lv_obj_set_pos(k->name, 72, 36);

        k->empty_dot = box(k->card, 72, 37, 6, 6, C_TRACK);
        lv_obj_set_style_radius(k->empty_dot, 3, 0);
        k->v = k->held = k->grabbed = -1;
        k->color = 0;
    }

    // Захват: круглый медальон с большой дугой в центре экрана поверх сетки.
    int cy_in = RING_CY - TOP_H;
    u.big_box = box(u.grid, PL_CENTER - BIG_BOX / 2, cy_in - BIG_BOX / 2, BIG_BOX, BIG_BOX, 0x0e1016);
    lv_obj_set_style_radius(u.big_box, BIG_BOX / 2, 0);
    lv_obj_set_style_border_width(u.big_box, 2, 0);
    lv_obj_set_style_border_color(u.big_box, lv_color_hex(0x2c3240), 0);

    u.big = arc(u.big_box, BIG_D, 14);
    lv_obj_set_style_arc_color(u.big, lv_color_hex(0x1c1f28), LV_PART_MAIN);
    lv_obj_center(u.big);
    u.big_name = label(u.big, &lv_font_montserrat_14, C_DIM);
    lv_obj_align(u.big_name, LV_ALIGN_CENTER, 0, -40);
    u.big_val = label(u.big, &lv_font_montserrat_48, C_ACCENT);
    lv_obj_align(u.big_val, LV_ALIGN_CENTER, 0, 4);
    u.big_sub = label(u.big, &lv_font_montserrat_12, C_BSP);
    lv_obj_align(u.big_sub, LV_ALIGN_CENTER, 0, 44);

    u.bsp_mark = box(u.big_box, 0, 0, 16, 16, C_BG);
    lv_obj_set_style_bg_opa(u.bsp_mark, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(u.bsp_mark, 8, 0);
    lv_obj_set_style_border_color(u.bsp_mark, lv_color_hex(C_BSP), 0);
    lv_obj_set_style_border_width(u.bsp_mark, 3, 0);

    // Всплывашка от BSP поверх всего: тёмная плашка с цветной рамкой страницы.
    u.toast = box(scr, 20, W - 82, W - 40, 68, 0x161820);
    lv_obj_set_style_radius(u.toast, 12, 0);
    lv_obj_set_style_border_width(u.toast, 2, 0);
    lv_obj_set_style_border_color(u.toast, lv_color_hex(C_ACCENT), 0);
    u.toast_name = label(u.toast, &lv_font_montserrat_20, C_TEXT);
    lv_obj_align(u.toast_name, LV_ALIGN_LEFT_MID, 16, 0);
    u.toast_val = label(u.toast, &lv_font_montserrat_28, C_ACCENT);
    lv_obj_align(u.toast_val, LV_ALIGN_RIGHT_MID, -16, 0);

    u.screen = MENU;
    u.grab = -1;
    u.drawn_type = u.drawn_grab = u.drawn_bsp_page = -1;
    u.big_sym = -1;
}

static void style_knob_grab(knob_t *k, int grabbed)
{
    if (k->grabbed == grabbed) {
        return;
    }
    k->grabbed = grabbed;
    if (!k->label) {
        return;
    }
    lv_obj_set_style_border_width(k->card, grabbed ? 2 : 1, 0);
    lv_obj_set_style_border_color(k->card, lv_color_hex(grabbed ? k->color : C_TILE_BRD), 0);
    lv_obj_set_style_text_color(k->name, lv_color_hex(grabbed ? k->color : C_DIM), 0);
}

// Подписи и цвета страницы зависят от типа синта и модели перкуссии: перестроить при смене.
static void page_labels(const pl_model_t *m)
{
    const uint8_t *raw = m->raw[u.page];
    uint32_t pc = pl_page_color(u.page);
    for (int i = 0; i < KNOBS; i++) {
        knob_t *k = &u.knob[i];
        int enc = i + 1;
        k->label = pl_knob_label(u.page, enc, raw);
        int on = k->label != NULL;
        lv_obj_set_style_bg_color(k->card, lv_color_hex(on ? C_TILE : C_TILE_EMPTY), 0);
        show(k->arc, on);
        show(k->name, on);
        show(k->num, on);
        show(k->pdot, on);
        show(k->empty_dot, !on);
        if (on) {
            int lo, hi;
            pl_knob_range(u.page, enc, raw, &lo, &hi);
            lv_arc_set_range(k->arc, lo, hi);
            int sym = pl_knob_bipolar(u.page, enc, raw);
            lv_arc_set_mode(k->arc, sym ? LV_ARC_MODE_SYMMETRICAL : LV_ARC_MODE_NORMAL);
            uint32_t c = pl_knob_color(u.page, enc, raw);
            if (k->color != c) {
                lv_obj_set_style_arc_color(k->arc, lv_color_hex(c), LV_PART_INDICATOR);
                k->color = c;
            }
            text(k->name, k->label);
        }
        k->v = k->held = -1;
        style_knob_grab(k, 0);
    }
    u.drawn_grab = -1;

    char nav[32];
    snprintf(nav, sizeof nav, LV_SYMBOL_LEFT "  %d %s  " LV_SYMBOL_RIGHT, u.page, pl_page_name(u.page));
    text(u.bar_nav, nav);
    lv_obj_set_style_text_color(u.bar_nav, lv_color_hex(pc), 0);
    text(u.bar_left, LV_SYMBOL_LEFT " MENU");
    show(u.bar_nav, 1);
    u.drawn_type = raw[1];
}

static void open_page(int page)
{
    u.screen = PAGE;
    u.page = (page + PL_PAGES) % PL_PAGES;
    u.grab = -1;
}

// Под локом LVGL.
static void open_menu(void)
{
    u.screen = MENU;
    u.grab = -1;
    text(u.bar_left, "ESPOSC");
    show(u.bar_nav, 0);
}

// Голубое кольцо на большой дуге внутри u.big_box: где стоит физический энкодер BSP.
static void place_mark(int pos, int lo, int hi)
{
    float f = hi > lo ? (float)(pos - lo) / (float)(hi - lo) : 0.0f;
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    float a = (135.0f + 270.0f * f) * (float)M_PI / 180.0f;
    float r = (BIG_D - 14) / 2.0f;
    int c = BIG_BOX / 2;
    lv_obj_set_pos(u.bsp_mark, c + (int)lroundf(r * cosf(a)) - 8,
                   c + (int)lroundf(r * sinf(a)) - 8);
}

static void draw_menu(const pl_model_t *m)
{
    char buf[24];
    for (int p = 0; p < PL_PAGES; p++) {
        text(u.tile_sub[p], pl_page_summary(p, m->raw[p], buf));
    }
    int active = (m->have && m->mode == 1 && m->bsp_page < PL_PAGES) ? m->bsp_page : -1;
    if (active != u.drawn_bsp_page) {
        if (u.drawn_bsp_page >= 0 && u.drawn_bsp_page < PL_PAGES) {
            lv_obj_set_style_border_width(u.tile[u.drawn_bsp_page], 1, 0);
            lv_obj_set_style_border_color(u.tile[u.drawn_bsp_page], lv_color_hex(C_TILE_BRD), 0);
        }
        if (active >= 0) {
            lv_obj_set_style_border_width(u.tile[active], 2, 0);
            lv_obj_set_style_border_color(u.tile[active], lv_color_hex(pl_page_color(active)), 0);
        }
        u.drawn_bsp_page = active;
    }
}

static void draw_page(const pl_model_t *m, uint32_t ms)
{
    const uint8_t *raw = m->raw[u.page];
    if (pl_labels_depend(u.page) && raw[1] != u.drawn_type) {
        page_labels(m);
    }
    char buf[24];
    for (int i = 0; i < KNOBS; i++) {
        knob_t *k = &u.knob[i];
        if (!k->label) {
            continue;
        }
        int enc = i + 1;
        int v = raw[enc];
        if (v != k->v) {
            lv_arc_set_value(k->arc, v);
            text(k->val, pl_value_text(u.page, enc, v, raw, buf));
            k->v = v;
        }
        int held = m->held[u.page][enc] && m->bsp_pos[enc] != 0xFF;
        if (held != k->held) {
            lv_obj_set_style_bg_color(k->pdot, lv_color_hex(held ? C_BSP : C_MUTED), 0);
            lv_obj_set_style_text_color(k->num, lv_color_hex(held ? C_BSP : C_MUTED), 0);
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

    show(u.big_box, g >= 0);
    if (g >= 0) {
        int enc = g + 1;
        int lo, hi, v = raw[enc];
        pl_knob_range(u.page, enc, raw, &lo, &hi);
        int sym = pl_knob_bipolar(u.page, enc, raw);
        if (u.big_sym != sym) {
            lv_arc_set_mode(u.big, sym ? LV_ARC_MODE_SYMMETRICAL : LV_ARC_MODE_NORMAL);
            u.big_sym = sym;
        }
        if (lv_arc_get_min_value(u.big) != lo || lv_arc_get_max_value(u.big) != hi) {
            lv_arc_set_range(u.big, lo, hi);
        }
        if (lv_arc_get_value(u.big) != v) {
            lv_arc_set_value(u.big, v);
        }
        uint32_t c = u.knob[g].color;
        if (u.big_color != c) {
            lv_obj_set_style_arc_color(u.big, lv_color_hex(c), LV_PART_INDICATOR);
            lv_obj_set_style_text_color(u.big_val, lv_color_hex(c), 0);
            lv_obj_set_style_border_color(u.big_box, lv_color_hex(c), 0);
            u.big_color = c;
        }
        text(u.big_val, pl_value_text(u.page, enc, v, raw, buf));
        char head[28];
        snprintf(head, sizeof head, "E%d  %s", enc + 1, u.knob[g].label);
        text(u.big_name, head);
        int held = m->held[u.page][enc] && m->bsp_pos[enc] != 0xFF;
        show(u.bsp_mark, held);
        if (held) {
            place_mark(m->bsp_pos[enc], lo, hi);
            char bsp_txt[20];
            snprintf(bsp_txt, sizeof bsp_txt, "BSP %s",
                     pl_value_text(u.page, enc, m->bsp_pos[enc], raw, buf));
            text(u.big_sub, bsp_txt);
        } else {
            text(u.big_sub, "");
        }
    }
    (void)ms;
}

static void draw_status(const pl_model_t *m, uint32_t ms)
{
    int linked = pl_model_linked(m, ms);
    const char *s = !linked ? "NO LINK" : m->mode ? "LIVE" : "DEMO";
    uint32_t c = !linked ? 0xff5e6c : m->mode ? 0x7be382 : 0xffb547;
    text(u.bar_right, s);
    lv_obj_set_style_text_color(u.bar_right, lv_color_hex(c), 0);
}

static void draw_toast(const pl_model_t *m, uint32_t ms)
{
    int on = m->toast_until_ms && (int32_t)(m->toast_until_ms - ms) > 0;
    show(u.toast, on);
    if (on) {
        const uint8_t *raw = m->raw[m->toast_page];
        uint32_t pc = pl_page_color(m->toast_page);
        if (u.toast_color != pc) {
            lv_obj_set_style_border_color(u.toast, lv_color_hex(pc), 0);
            lv_obj_set_style_text_color(u.toast_val, lv_color_hex(pc), 0);
            u.toast_color = pc;
        }
        char buf[12];
        const char *name = pl_knob_label(m->toast_page, m->toast_enc, raw);
        char line[36];
        snprintf(line, sizeof line, "%s / %s", pl_page_name(m->toast_page), name ? name : "?");
        text(u.toast_name, line);
        text(u.toast_val, pl_value_text(m->toast_page, m->toast_enc, raw[m->toast_enc], raw, buf));
    }
}

static int tile_at(int x, int y)
{
    y -= TOP_H;
    for (int p = 0; p < PL_PAGES; p++) {
        int tx = 12 + (p % 2) * 234, ty = 10 + (p / 2) * 84;
        if (x >= tx && x < tx + 222 && y >= ty && y < ty + 74) {
            return p;
        }
    }
    return -1;
}

static void knob_input(pl_model_t *m, int ev, int down, int x, int y, int x0, int y0);

static void input(pl_model_t *m, int down, int x, int y, uint32_t ms)
{
    int ev = pl_press_step(&u.press, down, x, y, ms);
    int x0, y0, x1, y1;
    pl_press_where(&u.press, &x0, &y0);
    pl_press_last(&u.press, &x1, &y1);
    int tap = ev == PL_PRESS_TAP || ev == PL_PRESS_END || (ev == PL_PRESS_RELEASE && u.grab < 0);
    if (u.screen == MENU) {
        int t = tile_at(x0, y0);
        if (tap && t >= 0 && t == tile_at(x1, y1)) {
            open_page(t);
            link_page(u.page);
            pl_model_page_sent(m, ms);
        }
    } else if (tap && y0 < TOP_H && y1 < TOP_H + 16) {
        if (x0 >= 140 && x0 < 215) {
            open_page(u.page - 1);
            link_page(u.page);
            pl_model_page_sent(m, ms);
        } else if (x0 >= 265 && x0 < 360) {
            open_page(u.page + 1);
            link_page(u.page);
            pl_model_page_sent(m, ms);
        } else {
            u.screen = MENU;
            u.grab = -1;
        }
    } else {
        knob_input(m, ev, down, x, y, x0, y0);
    }
    if (m->goto_page >= 0 && !down && u.grab < 0) {
        open_page(m->goto_page);
        m->goto_page = -1;
    }
}

static void knob_input(pl_model_t *m, int ev, int down, int x, int y, int x0, int y0)
{
    // Захват по удержанию 50 мс или сразу при начале сдвига пальца внутри рабочей области страницы.
    if (down && u.grab < 0 && y0 >= TOP_H && (ev == PL_PRESS_HOLD || u.press.state == 2)) {
        int i = pl_grid_cell(x0, y0);
        if (i >= 0 && i < KNOBS && u.knob[i].label) {
            int lo, hi;
            pl_knob_range(u.page, i + 1, m->raw[u.page], &lo, &hi);
            pl_turn_begin(&u.turn, x, y - RING_DY, m->raw[u.page][i + 1], lo, hi);
            u.grab = i;
            u.sent = m->raw[u.page][i + 1];
        }
    }
    if (!down || ev == PL_PRESS_RELEASE || ev == PL_PRESS_END || ev == PL_PRESS_TAP) {
        u.grab = -1;
    }
    if (u.grab >= 0 && down) {
        int v = pl_turn_move(&u.turn, x, y - RING_DY);
        if (v != u.sent) {
            pl_model_local_set(m, u.page, u.grab + 1, v);
            link_set(u.page, u.grab + 1, v);
            u.sent = v;
        }
    }
}

void ui_start(void)
{
    lvgl_port_lock(0);
    build();
    open_menu();
    lvgl_port_unlock();
}

void ui_step(pl_model_t *m, int down, int x, int y, uint32_t ms)
{
    int was = u.screen, was_page = u.page;
    input(m, down, x, y, ms);
    if (u.screen == PAGE && pl_model_page_resend(m, u.page, ms)) {
        link_page(u.page);
        pl_model_page_sent(m, ms);
    }
    u.dirty |= u.screen != was || (u.screen == PAGE && u.page != was_page);
    if (!lvgl_port_lock(1)) {
        return;
    }
    if (u.dirty) {
        if (u.screen == PAGE) {
            page_labels(m);
        } else {
            open_menu();
        }
        u.dirty = 0;
    }
    show(u.menu, u.screen == MENU);
    show(u.grid, u.screen == PAGE);
    if (u.screen == PAGE) {
        draw_page(m, ms);
    } else {
        draw_menu(m);
    }
    draw_status(m, ms);
    draw_toast(m, ms);
    lvgl_port_unlock();
}
