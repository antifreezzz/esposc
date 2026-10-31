#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_lvgl_port.h"
#include "link.h"
#include "pl_gesture.h"
#include "pl_labels.h"

// Экраны по одобренным макетам: меню страниц, круг ручек («барабан»), захват ручки, всплывашка от BSP.

#define W 480
#define TOP_H 40   // верхняя полоса: на странице - «назад»
#define RING_R 168 // радиус круга ручек вокруг центра (PL_CENTER)
#define KNOB_D 54  // мини-дуга ручки
#define BIG_D 210  // большая дуга захваченной ручки
#define KNOBS 15   // энкодеры 2..16

#define C_BG 0x0c0c0c
#define C_BAR 0x161616
#define C_TILE 0x1c1c1c
#define C_DIM 0x777777
#define C_TEXT 0xeeeeee
#define C_ACCENT 0xff8800
#define C_TRACK 0x2a2a2a
#define C_BSP 0x66ccff

enum { MENU, PAGE };

typedef struct {
    lv_obj_t *arc, *val, *name, *dot;
    int v, held; // нарисовано
    const char *label;
} knob_t;

static struct {
    int screen, page, grab; // grab - индекс энкодера с 0 или -1
    pl_press_t press;
    pl_turn_t turn;
    int sent;  // последнее отправленное значение захваченной ручки
    int dirty; // сменился экран: перестроить под локом
    lv_obj_t *bar_left, *bar_right, *menu, *ring, *center_title, *center_sub;
    lv_obj_t *tile[PL_PAGES];
    knob_t knob[KNOBS];
    lv_obj_t *big, *big_val, *big_name, *bsp_mark;
    lv_obj_t *toast, *toast_name, *toast_val;
    int drawn_mode, drawn_have, drawn_type, drawn_grab;
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

// Центр ручки i на круге: по часовой от 12 часов, как pl_sector.
static void knob_xy(int i, int *x, int *y)
{
    float a = (float)i * (2.0f * (float)M_PI / KNOBS);
    *x = PL_CENTER + (int)lroundf(RING_R * sinf(a));
    *y = PL_CENTER - (int)lroundf(RING_R * cosf(a));
}

static void build(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *bar = box(scr, 0, 0, W, TOP_H, C_BAR);
    u.bar_left = label(bar, &lv_font_montserrat_20, 0xaaaaaa);
    lv_obj_align(u.bar_left, LV_ALIGN_LEFT_MID, 14, 0);
    u.bar_right = label(bar, &lv_font_montserrat_14, C_ACCENT);
    lv_obj_align(u.bar_right, LV_ALIGN_RIGHT_MID, -14, 0);

    // Меню: 2 столбца по 5 плиток.
    u.menu = box(scr, 0, TOP_H, W, W - TOP_H, C_BG);
    for (int p = 0; p < PL_PAGES; p++) {
        lv_obj_t *t = box(u.menu, 12 + (p % 2) * 234, 10 + (p / 2) * 86, 222, 74, C_TILE);
        lv_obj_set_style_radius(t, 8, 0);
        lv_obj_t *num = label(t, &lv_font_montserrat_28, C_ACCENT);
        lv_label_set_text_fmt(num, "%d", p);
        lv_obj_align(num, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_t *name = label(t, &lv_font_montserrat_20, C_TEXT);
        lv_label_set_text(name, pl_page_name(p));
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 56, 0);
        u.tile[p] = t;
    }

    // Страница: круг ручек.
    u.ring = box(scr, 0, TOP_H, W, W - TOP_H, C_BG);
    lv_obj_set_style_bg_opa(u.ring, LV_OPA_TRANSP, 0);
    for (int i = 0; i < KNOBS; i++) {
        knob_t *k = &u.knob[i];
        int x, y;
        knob_xy(i, &x, &y);
        y -= TOP_H; // координаты внутри контейнера
        k->arc = arc(u.ring, KNOB_D, 5);
        lv_obj_set_pos(k->arc, x - KNOB_D / 2, y - KNOB_D / 2 - 8);
        k->val = label(k->arc, &lv_font_montserrat_14, C_TEXT);
        lv_obj_center(k->val);
        k->name = label(u.ring, &lv_font_montserrat_12, C_DIM);
        lv_obj_set_style_text_letter_space(k->name, 1, 0);
        lv_obj_set_width(k->name, 90);
        lv_obj_set_style_text_align(k->name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(k->name, x - 45, y + KNOB_D / 2 - 6);
        k->dot = box(u.ring, x - 3, y - 3, 6, 6, C_TRACK);
        lv_obj_set_style_radius(k->dot, 3, 0);
        k->v = k->held = -1;
    }
    u.center_title = label(u.ring, &lv_font_montserrat_28, C_TEXT);
    lv_obj_align(u.center_title, LV_ALIGN_CENTER, 0, -TOP_H / 2 - 12);
    u.center_sub = label(u.ring, &lv_font_montserrat_14, C_DIM);
    lv_obj_align(u.center_sub, LV_ALIGN_CENTER, 0, -TOP_H / 2 + 18);

    // Захват: большая дуга в центре.
    u.big = arc(u.ring, BIG_D, 14);
    lv_obj_set_style_arc_color(u.big, lv_color_hex(0x1e1e1e), LV_PART_MAIN);
    lv_obj_set_pos(u.big, PL_CENTER - BIG_D / 2, PL_CENTER - TOP_H - BIG_D / 2);
    u.big_val = label(u.big, &lv_font_montserrat_48, C_ACCENT);
    lv_obj_align(u.big_val, LV_ALIGN_CENTER, 0, 10);
    u.big_name = label(u.big, &lv_font_montserrat_14, C_DIM);
    lv_obj_align(u.big_name, LV_ALIGN_CENTER, 0, -36);
    u.bsp_mark = box(u.ring, 0, 0, 16, 16, C_BG);
    lv_obj_set_style_bg_opa(u.bsp_mark, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(u.bsp_mark, 8, 0);
    lv_obj_set_style_border_color(u.bsp_mark, lv_color_hex(C_BSP), 0);
    lv_obj_set_style_border_width(u.bsp_mark, 3, 0);

    // Всплывашка от BSP поверх всего.
    u.toast = box(scr, 20, W - 84, W - 40, 70, C_ACCENT);
    lv_obj_set_style_radius(u.toast, 10, 0);
    u.toast_name = label(u.toast, &lv_font_montserrat_20, 0x000000);
    lv_obj_align(u.toast_name, LV_ALIGN_LEFT_MID, 16, 0);
    u.toast_val = label(u.toast, &lv_font_montserrat_28, 0x000000);
    lv_obj_align(u.toast_val, LV_ALIGN_RIGHT_MID, -16, 0);

    u.screen = MENU;
    u.grab = -1;
    u.drawn_mode = u.drawn_have = u.drawn_type = u.drawn_grab = -1;
}

static void show(lv_obj_t *o, int on)
{
    if (lv_obj_is_hidden(o) == !!on) {
        lv_obj_set_hidden(o, !on);
    }
}

// Текст только если поменялся: каждая установка перерисовывает область.
static void text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s) != 0) {
        lv_label_set_text(l, s);
    }
}


// Подписи страницы зависят от типа синта и модели перкуссии: перерисовать при смене.
static void page_labels(const pl_model_t *m)
{
    const uint8_t *raw = m->raw[u.page];
    for (int i = 0; i < KNOBS; i++) {
        knob_t *k = &u.knob[i];
        k->label = pl_knob_label(u.page, i + 1, raw);
        show(k->arc, k->label != NULL);
        show(k->name, k->label != NULL);
        show(k->dot, k->label == NULL);
        if (k->label) {
            int lo, hi;
            pl_knob_range(u.page, i + 1, raw, &lo, &hi);
            lv_arc_set_range(k->arc, lo, hi);
            lv_label_set_text(k->name, k->label);
        }
        k->v = k->held = -1;
    }
    u.drawn_grab = -1;
    lv_label_set_text(u.center_title, pl_page_name(u.page));
    char buf[12];
    int typed = u.page == 6 || u.page == 7 || (u.page >= 3 && u.page <= 5);
    lv_label_set_text(u.center_sub, typed ? pl_value_text(u.page, 1, raw[1], raw, buf) : "");
    lv_label_set_text_fmt(u.bar_left, LV_SYMBOL_LEFT "  %s", pl_page_name(u.page));
    u.drawn_type = raw[1];
}

static void open_page(int page)
{
    u.screen = PAGE;
    u.page = page;
    u.grab = -1;
}

// Под локом LVGL.
static void open_menu(void)
{
    u.screen = MENU;
    u.grab = -1;
    lv_label_set_text(u.bar_left, "ESPOSC");
    u.drawn_mode = -1;
}

// Голубое кольцо на большой дуге: где стоит энкодер BSP.
static void place_mark(int pos, int lo, int hi)
{
    float f = hi > lo ? (float)(pos - lo) / (float)(hi - lo) : 0.0f;
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    float a = (135.0f + 270.0f * f) * (float)M_PI / 180.0f;
    float r = (BIG_D - 14) / 2.0f;
    lv_obj_set_pos(u.bsp_mark, PL_CENTER + (int)lroundf(r * cosf(a)) - 8,
                   PL_CENTER - TOP_H + (int)lroundf(r * sinf(a)) - 8);
}

static void draw_page(const pl_model_t *m, uint32_t ms)
{
    const uint8_t *raw = m->raw[u.page];
    if (raw[1] != u.drawn_type) {
        page_labels(m);
    }
    char buf[12];
    for (int i = 0; i < KNOBS; i++) {
        knob_t *k = &u.knob[i];
        if (!k->label) {
            continue;
        }
        int v = raw[i + 1];
        if (v != k->v) {
            lv_arc_set_value(k->arc, v);
            text(k->val, pl_value_text(u.page, i + 1, v, raw, buf));
            k->v = v;
        }
    }
    int g = u.grab;
    // Остальные ручки не гасим: полная перерисовка экрана ~130 мс, захват ощущался долгим. Захват видно по
    // большой дуге в центре.
    u.drawn_grab = g;
    show(u.big, g >= 0);
    show(u.center_title, g < 0);
    show(u.center_sub, g < 0);
    if (g >= 0) {
        int lo, hi, v = raw[g + 1];
        pl_knob_range(u.page, g + 1, raw, &lo, &hi);
        if (lv_arc_get_min_value(u.big) != lo || lv_arc_get_max_value(u.big) != hi) {
            lv_arc_set_range(u.big, lo, hi);
        }
        if (lv_arc_get_value(u.big) != v) {
            lv_arc_set_value(u.big, v);
        }
        text(u.big_val, pl_value_text(u.page, g + 1, v, raw, buf));
        text(u.big_name, u.knob[g].label);
        int held = m->held[u.page][g + 1] && m->bsp_pos[g + 1] != 0xFF;
        show(u.bsp_mark, held);
        if (held) {
            place_mark(m->bsp_pos[g + 1], lo, hi); // lv_obj_set_pos на той же точке ничего не перерисовывает
        }
    } else {
        show(u.bsp_mark, 0);
    }
    (void)ms;
}

static void draw_status(const pl_model_t *m)
{
    int mode = m->have ? m->mode : -1;
    if (u.screen == PAGE) {
        text(u.bar_right, m->have ? "" : "NO LINK");
    } else if (mode != u.drawn_mode || m->have != u.drawn_have) {
        lv_label_set_text(u.bar_right, !m->have ? "NO LINK" : mode ? "LIVE" : "DEMO");
        u.drawn_mode = mode;
        u.drawn_have = m->have;
    }
}

static void draw_toast(const pl_model_t *m, uint32_t ms)
{
    int on = m->toast_until_ms && (int32_t)(m->toast_until_ms - ms) > 0;
    show(u.toast, on);
    if (on) {
        const uint8_t *raw = m->raw[m->toast_page];
        char buf[12];
        const char *name = pl_knob_label(m->toast_page, m->toast_enc, raw);
        char line[32];
        snprintf(line, sizeof line, "%s / %s", pl_page_name(m->toast_page), name ? name : "?");
        text(u.toast_name, line);
        text(u.toast_val, pl_value_text(m->toast_page, m->toast_enc, raw[m->toast_enc], raw, buf));
    }
}

static int tile_at(int x, int y)
{
    y -= TOP_H;
    for (int p = 0; p < PL_PAGES; p++) {
        int tx = 12 + (p % 2) * 234, ty = 10 + (p / 2) * 86;
        if (x >= tx && x < tx + 222 && y >= ty && y < ty + 74) {
            return p;
        }
    }
    return -1;
}

static void input(pl_model_t *m, int down, int x, int y, uint32_t ms)
{
    int ev = pl_press_step(&u.press, down, x, y, ms);
    int x0, y0;
    pl_press_where(&u.press, &x0, &y0);
    // Удержание всего 50 мс: обычный тап длиннее и приходит как захват и отпускание. Вне круга это тап.
    int tap = ev == PL_PRESS_TAP || (ev == PL_PRESS_RELEASE && u.grab < 0);
    if (u.screen == MENU) {
        if (tap && tile_at(x0, y0) >= 0) {
            open_page(tile_at(x0, y0));
        }
        return;
    }
    if (tap && y0 < TOP_H) {
        u.screen = MENU;
        u.grab = -1;
        return;
    }
    if (ev == PL_PRESS_HOLD && y0 >= TOP_H) {
        int i = pl_sector(x0, y0);
        if (i >= 0 && u.knob[i].label && m->have) {
            int lo, hi;
            pl_knob_range(u.page, i + 1, m->raw[u.page], &lo, &hi);
            pl_turn_begin(&u.turn, x, y, m->raw[u.page][i + 1], lo, hi);
            u.grab = i;
            u.sent = m->raw[u.page][i + 1];
        }
    }
    if (ev == PL_PRESS_RELEASE) {
        u.grab = -1;
    }
    if (u.grab >= 0 && down) {
        int v = pl_turn_move(&u.turn, x, y);
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
    // Жест и отправка в синт - каждый шаг, экран - когда LVGL свободен (палец не ждёт отрисовку).
    int was = u.screen;
    input(m, down, x, y, ms);
    u.dirty |= u.screen != was;
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
    show(u.ring, u.screen == PAGE);
    if (u.screen == PAGE) {
        draw_page(m, ms);
    }
    draw_status(m);
    draw_toast(m, ms);
    lvgl_port_unlock();
}
