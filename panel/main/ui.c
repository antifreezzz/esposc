#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "link.h"
#include "pl_gesture.h"
#include "pl_labels.h"
#include "pl_typepick.h"
#include "pl_view.h"
#include "ui_priv.h"

// Вариант Б: модульная сетка 3x5 (все 15 карточек - ручки E2..E16) + круговой захват в центре.
// Меню в два экрана: штатные страницы и свои (USER 1-6, сцены). На своей странице - редактор (EDIT).
// TYPE синта и слоя - тап по карточке, выбор сеткой типов поверх страницы.

#define W UI_W
#define TOP_H UI_TOP_H
#define RING_DY UI_RING_DY
#define RING_CY (PL_CENTER + RING_DY)
#define KNOB_D 56   // дуга внутри карточки сетки
#define BIG_BOX 244 // круглый диск подложки под большую дугу в центре
#define BIG_D 210   // большая дуга захваченной ручки
#define KNOBS 15    // энкодеры 2..16 (ячейки 0..14)
#define MENU2_TILES 9 // USER 1-6, SCENES, GRIDS, PETRI
#define TILE_GRIDS (PL_USER_PAGES + 1)
#define TILE_PETRI (PL_USER_PAGES + 2)
#define EDIT_X0 362 // кнопка EDIT в шапке своей страницы
#define EDIT_X1 412
#define XY_X0 88 // кнопка XY в шапке своей страницы: между «MENU» и стрелками
#define XY_X1 140

enum { MENU, MENU2, PAGE, SCENES, GRIDS, PETRIS };

typedef struct {
    lv_obj_t *card, *num, *arc, *val, *name, *src, *pdot, *empty_dot;
    int v, held, grabbed; // нарисовано
    int lo, hi, bipolar;
    uint32_t color, src_color;
    char srcs[12]; // строка-источник своей страницы, "" - нет
    char lab[20]; // подпись карточки, "" - пустой слот
} knob_t;

static struct {
    int screen, page, grab; // grab - индекс ручки 0..14 или -1
    pl_press_t press;
    pl_turn_t turn;
    pl_edit_t ed;
    int sent; // последнее отправленное значение захваченной ручки
    lv_obj_t *bar_left, *bar_nav, *bar_right, *bar_edit, *bar_xy, *bar_load;
    uint32_t load_color;
    uint8_t xy_view; // бит своей страницы: вместо сетки XY-пэд
    lv_obj_t *menu, *menu2, *grid;
    lv_obj_t *tile[PL_PAGES], *tile_sub[PL_PAGES], *tile2_sub[MENU2_TILES];
    int drawn_bsp_page;
    knob_t knob[KNOBS];
    lv_obj_t *big_box, *big, *big_val, *big_name, *big_sub, *bsp_mark;
    uint32_t big_color, nav_color;
    int big_sym;
    lv_obj_t *toast, *toast_name, *toast_val;
    lv_obj_t *chip_page[PL_TARGETS], *chip_val[PL_TARGETS]; // цели макро во всплывашке
    uint32_t toast_color;
    int toast_tall;
    int drawn_grab;
    lv_obj_t *pick; // сетка типов: один объект, клетки рисует сам
    int pick_page;  // страница, чей TYPE выбираем, -1 - сетка закрыта
    int drawn_pick; // страница * 32 + тип, что нарисованы
} u;

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

// Текст только если поменялся: каждая установка инвалидирует область LVGL.
void ui_text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s) != 0) {
        lv_label_set_text(l, s);
    }
}

void ui_tile_xy(int p, int *x, int *y)
{
    *x = 12 + (p % 2) * 234;
    *y = 10 + (p / 2) * 84;
}

int ui_tile_at(int x, int y)
{
    y -= TOP_H;
    for (int p = 0; p < 10; p++) { // сетка 2x5
        int tx, ty;
        ui_tile_xy(p, &tx, &ty);
        if (x >= tx && x < tx + UI_TILE_W && y >= ty && y < ty + UI_TILE_H) {
            return p;
        }
    }
    return -1;
}

// Меню страниц: 2x6 плиток пониже, слой - справа от своего синта.
#define MENU_TILE_H 64
static const uint8_t MENU_SLOT[PL_PAGE_GRIDS] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 11, 7, 9}; // страница -> место

static void menu_xy(int p, int *x, int *y)
{
    int k = MENU_SLOT[p];
    *x = 12 + (k % 2) * 234;
    *y = 6 + (k / 2) * 72;
}

static int menu_page_at(int x, int y)
{
    y -= TOP_H;
    for (int p = 0; p < PL_PAGE_GRIDS; p++) {
        int tx, ty;
        menu_xy(p, &tx, &ty);
        if (x >= tx && x < tx + UI_TILE_W && y >= ty && y < ty + MENU_TILE_H) {
            return p;
        }
    }
    return -1;
}

// Координаты карточки i (0..14) в сетке 3x5 внутри контейнера u.grid (480 x 440).
static void cell_xy(int i, int *x, int *y)
{
    *x = 9 + (i % 3) * 156;
    *y = 6 + (i / 3) * 86;
}

static lv_obj_t *menu_tile(lv_obj_t *parent, int x, int y, int h, uint32_t pc, const char *num, const char *name,
                           lv_obj_t **sub)
{
    lv_obj_t *t = ui_box(parent, x, y, UI_TILE_W, h, C_TILE);
    lv_obj_set_style_radius(t, 10, 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_border_color(t, lv_color_hex(C_TILE_BRD), 0);

    lv_obj_t *stripe = ui_box(t, 0, 12, 4, h - 24, pc);
    lv_obj_set_style_radius(stripe, 2, 0);

    lv_obj_t *n = ui_label(t, &lv_font_montserrat_28, pc);
    lv_label_set_text(n, num);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *l = ui_label(t, &lv_font_montserrat_20, C_TEXT);
    lv_label_set_text(l, name);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 54, 12);

    *sub = ui_label(t, &lv_font_montserrat_12, C_DIM);
    lv_obj_align(*sub, LV_ALIGN_BOTTOM_LEFT, 55, -11);
    return t;
}

// Сетка типов: клетки с подписью, у текущего типа рамка и подпись цветом страницы.
static void pick_draw(lv_event_t *e)
{
    if (u.drawn_pick < 0) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t base;
    lv_obj_get_coords(u.pick, &base);
    int page = u.drawn_pick / 32, cur = u.drawn_pick % 32;
    uint32_t pc = pl_page_color(page);
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.radius = 8;
    f.color = lv_color_hex(C_TILE);
    lv_draw_border_dsc_t b;
    lv_draw_border_dsc_init(&b);
    b.radius = 8;
    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.font = &lv_font_montserrat_14;
    l.align = LV_TEXT_ALIGN_CENTER;
    uint8_t raw[PL_KNOBS] = {0};
    char buf[12];
    for (int s = 0; s < pl_tp_count(page); s++) {
        int t = pl_tp_type(page, s), x, y, w, h;
        pl_tp_rect(s, &x, &y, &w, &h);
        y -= TOP_H;
        lv_area_t a = {base.x1 + x, base.y1 + y, base.x1 + x + w - 1, base.y1 + y + h - 1};
        lv_draw_fill(layer, &f, &a);
        b.width = t == cur ? 2 : 1;
        b.color = lv_color_hex(t == cur ? pc : C_TILE_BRD);
        lv_draw_border(layer, &b, &a);
        l.text = pl_value_text(page, 1, t, raw, buf);
        l.text_local = 1; // LVGL копирует текст: buf живёт только здесь
        l.color = lv_color_hex(t == cur ? pc : C_TEXT);
        lv_area_t ta = {a.x1 + 2, a.y1 + (h - 16) / 2, a.x2 - 2, a.y2};
        lv_draw_label(layer, &l, &ta);
    }
}

static void build(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *bar = ui_box(scr, 0, 0, W, TOP_H, C_BAR);
    u.bar_left = ui_label(bar, &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(u.bar_left, LV_ALIGN_LEFT_MID, 14, 0);
    u.bar_nav = ui_label(bar, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_align(u.bar_nav, LV_ALIGN_CENTER, 0, 0);
    u.bar_right = ui_label(bar, &lv_font_montserrat_14, C_ACCENT);
    lv_obj_align(u.bar_right, LV_ALIGN_RIGHT_MID, -14, -8);
    // Под режимом - худшая загрузка ядер синта за полсекунды: ядро 1, ядро 0 (проценты бюджета).
    u.bar_load = ui_label(bar, &lv_font_montserrat_12, C_TEXT);
    lv_obj_align(u.bar_load, LV_ALIGN_RIGHT_MID, -14, 10);
    u.bar_edit = ui_label(bar, &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(u.bar_edit, LV_ALIGN_LEFT_MID, EDIT_X0 + 4, 0);
    u.bar_xy = ui_label(bar, &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(u.bar_xy, LV_ALIGN_LEFT_MID, XY_X0 + 4, 0);

    // Меню: 2 столбца по 5 плиток с цветной полоской и сводкой состояния.
    u.menu = ui_box(scr, 0, TOP_H, W, W - TOP_H, C_BG);
    for (int p = 0; p < PL_PAGE_GRIDS; p++) { // у Grids свой экран: плитка во втором меню
        char num[4];
        snprintf(num, sizeof num, "%d", pl_page_bsp(p));
        int x, y;
        menu_xy(p, &x, &y);
        u.tile[p] = menu_tile(u.menu, x, y, MENU_TILE_H, pl_page_color(p), num, pl_page_name(p), &u.tile_sub[p]);
    }
    // Второй экран меню: свои страницы 10..15, сцены и Grids.
    u.menu2 = ui_box(scr, 0, TOP_H, W, W - TOP_H, C_BG);
    for (int p = 0; p < MENU2_TILES; p++) {
        char num[4];
        snprintf(num, sizeof num, p < PL_USER_PAGES ? "%d" : p == TILE_GRIDS ? "G" : p == TILE_PETRI ? "18" : "S",
                 pl_page_bsp(PL_USER_FIRST + p));
        int x, y;
        ui_tile_xy(p, &x, &y);
        uint32_t pc = p < PL_USER_PAGES ? pl_view_page_color(PL_USER_FIRST + p)
                      : p == TILE_GRIDS ? pl_page_color(PL_PAGE_GRIDS)
                      : p == TILE_PETRI ? pl_page_color(PL_PAGE_PETRI)
                                        : C_ACCENT;
        menu_tile(u.menu2, x, y, UI_TILE_H, pc, num,
                  p < PL_USER_PAGES ? pl_user_name(p) : p == TILE_GRIDS ? "GRIDS" : p == TILE_PETRI ? "PETRI" : "SCENES",
                  &u.tile2_sub[p]);
    }

    // Страница: сетка 3x5 из 15 карточек ручек (энкодеры 2..16).
    u.grid = ui_box(scr, 0, TOP_H, W, W - TOP_H, C_BG);
    for (int i = 0; i < KNOBS; i++) {
        knob_t *k = &u.knob[i];
        int x, y;
        cell_xy(i, &x, &y);
        k->card = ui_box(u.grid, x, y, 150, 80, C_TILE);
        lv_obj_set_style_radius(k->card, 10, 0);
        lv_obj_set_style_border_width(k->card, 1, 0);
        lv_obj_set_style_border_color(k->card, lv_color_hex(C_TILE_BRD), 0);

        k->arc = arc(k->card, KNOB_D, 6);
        lv_obj_set_pos(k->arc, 8, 12);
        k->val = ui_label(k->arc, &lv_font_montserrat_14, C_TEXT);
        lv_obj_align(k->val, LV_ALIGN_CENTER, 0, 0);

        k->num = ui_label(k->card, &lv_font_montserrat_12, C_MUTED);
        lv_label_set_text_fmt(k->num, "E%d", i + 2);
        lv_obj_set_pos(k->num, 72, 14);

        k->pdot = ui_box(k->card, 134, 10, 7, 7, C_MUTED);
        lv_obj_set_style_radius(k->pdot, 4, 0);

        k->name = ui_label(k->card, &lv_font_montserrat_12, C_DIM);
        lv_obj_set_width(k->name, 74);
        lv_obj_set_pos(k->name, 72, 36);

        k->src = ui_label(k->card, &lv_font_montserrat_12, C_MUTED);
        lv_obj_set_size(k->src, 74, 16);
        lv_label_set_long_mode(k->src, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(k->src, 72, 56);
        ui_show(k->src, 0);
        k->srcs[0] = 0;

        k->empty_dot = ui_box(k->card, 72, 37, 6, 6, C_TRACK);
        lv_obj_set_style_radius(k->empty_dot, 3, 0);
        k->v = k->held = k->grabbed = -1;
        k->color = 0;
        k->lo = k->hi = -1;
        strcpy(k->lab, "?"); // первая отрисовка настроит карточку
    }

    // Захват: круглый медальон с большой дугой в центре экрана поверх сетки.
    int cy_in = RING_CY - TOP_H;
    u.big_box = ui_box(u.grid, PL_CENTER - BIG_BOX / 2, cy_in - BIG_BOX / 2, BIG_BOX, BIG_BOX, 0x0e1016);
    lv_obj_set_style_radius(u.big_box, BIG_BOX / 2, 0);
    lv_obj_set_style_border_width(u.big_box, 2, 0);
    lv_obj_set_style_border_color(u.big_box, lv_color_hex(0x2c3240), 0);

    u.big = arc(u.big_box, BIG_D, 14);
    lv_obj_set_style_arc_color(u.big, lv_color_hex(0x1c1f28), LV_PART_MAIN);
    lv_obj_center(u.big);
    u.big_name = ui_label(u.big, &lv_font_montserrat_14, C_DIM);
    lv_obj_align(u.big_name, LV_ALIGN_CENTER, 0, -40);
    u.big_val = ui_label(u.big, &lv_font_montserrat_48, C_ACCENT);
    lv_obj_align(u.big_val, LV_ALIGN_CENTER, 0, 4);
    u.big_sub = ui_label(u.big, &lv_font_montserrat_12, C_BSP);
    lv_obj_align(u.big_sub, LV_ALIGN_CENTER, 0, 44);

    u.bsp_mark = ui_box(u.big_box, 0, 0, 16, 16, C_BG);
    lv_obj_set_style_bg_opa(u.bsp_mark, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(u.bsp_mark, 8, 0);
    lv_obj_set_style_border_color(u.bsp_mark, lv_color_hex(C_BSP), 0);
    lv_obj_set_style_border_width(u.bsp_mark, 3, 0);

    u.pick = ui_box(scr, 0, TOP_H, W, W - TOP_H, C_BG);
    lv_obj_add_event_cb(u.pick, pick_draw, LV_EVENT_DRAW_MAIN, NULL);
    ui_show(u.pick, 0);
    u.pick_page = u.drawn_pick = -1;

    uc_build(scr);
    ug_build(scr);
    up_build(scr);

    // Всплывашка от BSP поверх всего: тёмная плашка с цветной рамкой страницы.
    u.toast = ui_box(scr, 20, W - 82, W - 40, 68, 0x161820);
    lv_obj_set_style_radius(u.toast, 12, 0);
    lv_obj_set_style_border_width(u.toast, 2, 0);
    lv_obj_set_style_border_color(u.toast, lv_color_hex(C_ACCENT), 0);
    u.toast_name = ui_label(u.toast, &lv_font_montserrat_20, C_TEXT);
    lv_obj_align(u.toast_name, LV_ALIGN_LEFT_MID, 16, 0);
    u.toast_val = ui_label(u.toast, &lv_font_montserrat_28, C_ACCENT);
    lv_obj_align(u.toast_val, LV_ALIGN_RIGHT_MID, -16, 0);
    for (int i = 0; i < PL_TARGETS; i++) {
        int cw = (W - 40 - 32) / PL_TARGETS;
        u.chip_page[i] = ui_label(u.toast, &lv_font_montserrat_12, C_DIM);
        lv_obj_set_pos(u.chip_page[i], 16 + i * cw, 46);
        u.chip_val[i] = ui_label(u.toast, &lv_font_montserrat_14, C_TEXT);
        lv_obj_set_size(u.chip_val[i], cw - 6, 18);
        lv_label_set_long_mode(u.chip_val[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(u.chip_val[i], 16 + i * cw, 64);
    }
    u.toast_tall = 1; // первая отрисовка уложит в низкую

    u.screen = MENU;
    u.grab = -1;
    u.drawn_grab = u.drawn_bsp_page = -1;
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
    lv_obj_set_style_text_color(k->name, lv_color_hex(grabbed ? k->color : C_DIM), 0);
}

// Карточка под новую подпись, ход или цвет (сменили тип синта, модель перкуссии, цели своей страницы).
static void card_setup(knob_t *k, const pl_cell_t *c)
{
    int on = c->label != NULL;
    snprintf(k->lab, sizeof k->lab, "%s", on ? c->label : "");
    lv_obj_set_style_bg_color(k->card, lv_color_hex(on ? C_TILE : C_TILE_EMPTY), 0);
    ui_show(k->arc, on);
    ui_show(k->name, on);
    ui_show(k->num, on);
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
    ui_show(k->src, k->srcs[0] != 0);
    if (k->srcs[0]) {
        ui_text(k->src, k->srcs);
        lv_obj_set_style_text_color(k->src, lv_color_hex(c->src_color), 0);
    }
    k->src_color = c->src_color;
    k->color = c->color;
    k->lo = c->lo, k->hi = c->hi, k->bipolar = c->bipolar;
    k->v = k->held = -1;
    k->grabbed = -1;
    style_knob_grab(k, 0);
    u.drawn_grab = -1;
}

static int editing(void)
{
    return u.ed.state != PL_EDIT_OFF;
}

// На экране XY-пэд вместо сетки: своя страница с видом XY, не в редакторе.
static int xy_on(void)
{
    return u.screen == PAGE && !editing() && u.page >= PL_USER_FIRST && (u.xy_view >> (u.page - PL_USER_FIRST) & 1);
}

// Какую страницу рисует сетка: при выборе ручки цели - выбранную в редакторе.
static int view_page(void)
{
    return u.ed.state == PL_EDIT_PICK_KNOB ? u.ed.page : u.page;
}

static void open_page(int page)
{
    u.screen = PAGE;
    u.page = page;
    u.grab = -1;
    u.pick_page = -1;
}

// Экран чашки: энкодеры BSP крутят её ручки, пока не тронут энкодер 1.
static void open_dish(pl_model_t *m, uint32_t ms)
{
    u.screen = PETRIS;
    u.page = PL_PAGE_PETRI;
    u.grab = -1;
    u.pick_page = -1;
    link_page(PL_PAGE_PETRI);
    pl_model_page_sent(m, ms);
}

static void send_page(pl_model_t *m, int page, uint32_t ms)
{
    open_page(page);
    link_page(u.page);
    pl_model_page_sent(m, ms);
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

static void draw_bar(const pl_model_t *m)
{
    static const char *const STEP[] = {"", "EDIT: TAP A KNOB", "TARGETS", "PICK PAGE", "PICK KNOB", "RANGE"};
    char nav[40];
    uint32_t nc = C_ACCENT;
    int user = u.page >= PL_USER_FIRST;
    if (editing() || u.screen != PAGE) {
        ui_text(u.bar_xy, "");
    }
    if (editing()) {
        snprintf(nav, sizeof nav, "%s", STEP[u.ed.state]);
        if (u.ed.state == PL_EDIT_TARGETS) {
            snprintf(nav, sizeof nav, "E%d TARGETS", u.ed.enc + 1);
        } else if (u.ed.state == PL_EDIT_PICK_KNOB) {
            snprintf(nav, sizeof nav, "PICK: %s", pl_page_name(u.ed.page));
        }
        ui_text(u.bar_left, LV_SYMBOL_LEFT " MENU");
        ui_text(u.bar_edit, u.ed.state == PL_EDIT_SLOTS ? "DONE" : "BACK");
    } else if (u.screen == PAGE && u.pick_page >= 0) {
        snprintf(nav, sizeof nav, "%s TYPE", pl_view_page_name(u.page));
        nc = pl_view_page_color(u.page);
        ui_text(u.bar_left, LV_SYMBOL_CLOSE " CLOSE");
        ui_text(u.bar_edit, "");
    } else if (u.screen == PAGE) {
        snprintf(nav, sizeof nav, LV_SYMBOL_LEFT "  %d %s  " LV_SYMBOL_RIGHT, pl_page_bsp(u.page), pl_view_page_name(u.page));
        nc = pl_view_page_color(u.page);
        ui_text(u.bar_left, LV_SYMBOL_LEFT " MENU");
        ui_text(u.bar_edit, user ? ((m->user_have >> (u.page - PL_USER_FIRST) & 1) ? "EDIT" : "")
                                 : pl_view_dish(m, u.page) ? "DISH" : "");
        ui_text(u.bar_xy, !user ? "" : xy_on() ? "GRID" : "XY");
    } else if (u.screen == GRIDS) {
        snprintf(nav, sizeof nav, "GRIDS");
        nc = pl_page_color(PL_PAGE_GRIDS);
        ui_text(u.bar_left, LV_SYMBOL_LEFT " MENU");
        ui_text(u.bar_edit, "");
    } else if (u.screen == PETRIS) {
        snprintf(nav, sizeof nav, "PETRI");
        nc = pl_page_color(PL_PAGE_PETRI);
        ui_text(u.bar_left, LV_SYMBOL_LEFT " MENU");
        ui_text(u.bar_edit, "KNOBS");
    } else if (u.screen == SCENES) {
        snprintf(nav, sizeof nav, "SCENES");
        ui_text(u.bar_left, LV_SYMBOL_LEFT " MENU");
        ui_text(u.bar_edit, "");
    } else {
        snprintf(nav, sizeof nav, u.screen == MENU ? "USER  " LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT "  PAGES");
        ui_text(u.bar_left, "ESPOSC");
        ui_text(u.bar_edit, "");
    }
    ui_text(u.bar_nav, nav);
    if (u.nav_color != nc) {
        lv_obj_set_style_text_color(u.bar_nav, lv_color_hex(nc), 0);
        u.nav_color = nc;
    }
}

static void draw_menu(const pl_model_t *m)
{
    char buf[24];
    for (int p = 0; p < PL_PAGE_GRIDS; p++) {
        ui_text(u.tile_sub[p], m->have ? pl_page_summary(p, m->raw[p], buf) : ""); // до снимка значения не настоящие
    }
    int active = (m->have && m->mode == 1 && m->bsp_page < PL_PAGE_GRIDS) ? m->bsp_page : -1;
    if (active != u.drawn_bsp_page) {
        if (u.drawn_bsp_page >= 0 && u.drawn_bsp_page < PL_PAGE_GRIDS) {
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

static void draw_menu2(const pl_model_t *m)
{
    char buf[24];
    for (int up = 0; up < PL_USER_PAGES; up++) {
        int n = 0;
        for (int e = 1; e < PL_KNOBS; e++) {
            n += m->user_t[up][e][0].pe != PL_NONE;
        }
        snprintf(buf, sizeof buf, n ? "%d KNOBS" : "EMPTY - TAP, THEN EDIT", n);
        ui_text(u.tile2_sub[up], (m->user_have >> up & 1) ? buf : "");
    }
    int n = 0;
    for (int s = 0; s < 8; s++) {
        n += m->scene.used >> s & 1;
    }
    snprintf(buf, sizeof buf, "%d OF 8 SAVED", n);
    ui_text(u.tile2_sub[PL_USER_PAGES], m->have ? buf : "");
    ui_text(u.tile2_sub[TILE_GRIDS], m->have ? pl_page_summary(PL_PAGE_GRIDS, m->raw[PL_PAGE_GRIDS], buf) : "");
    ui_text(u.tile2_sub[TILE_PETRI], m->have ? pl_page_summary(PL_PAGE_PETRI, m->raw[PL_PAGE_PETRI], buf) : "");
}

static void draw_page(const pl_model_t *m)
{
    int page = view_page();
    pl_cell_t cell;
    for (int i = 0; i < KNOBS; i++) {
        knob_t *k = &u.knob[i];
        int enc = i + 1;
        pl_view_cell(m, page, enc, &cell);
        if (strcmp(k->lab, cell.label ? cell.label : "") != 0 || k->color != cell.color || k->lo != cell.lo ||
            k->hi != cell.hi || k->bipolar != cell.bipolar || strcmp(k->srcs, cell.src ? cell.src : "") != 0 ||
            k->src_color != cell.src_color) {
            card_setup(k, &cell);
        }
        if (!k->lab[0]) {
            continue;
        }
        if (cell.v != k->v) {
            lv_arc_set_value(k->arc, cell.v);
            ui_text(k->val, cell.text);
            k->v = cell.v;
        }
        int held = cell.held && m->bsp_pos[enc] != 0xFF;
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
    if (cell.src && pl_view_is_alias(m, page - PL_USER_FIRST, enc)) {
        snprintf(head, sizeof head, "E%d  %s / %s", enc + 1, cell.src, u.knob[g].lab); // откуда псевдоним
    } else {
        snprintf(head, sizeof head, "E%d  %s", enc + 1, u.knob[g].lab);
    }
    ui_text(u.big_name, head);
    int held = cell.held && m->bsp_pos[enc] != 0xFF;
    ui_show(u.bsp_mark, held);
    if (held) {
        place_mark(m->bsp_pos[enc], cell.lo, cell.hi);
        char bsp_txt[20];
        snprintf(bsp_txt, sizeof bsp_txt, "BSP %d", m->bsp_pos[enc]);
        ui_text(u.big_sub, bsp_txt);
    } else {
        ui_text(u.big_sub, "");
    }
}

static void draw_status(const pl_model_t *m, uint32_t ms)
{
    int linked = pl_model_linked(m, ms);
    const char *s = pl_view_status(m, linked); // в демо - имя песни, тап по нему - следующая
    uint32_t c = !linked ? 0xff5e6c : m->mode ? 0x7be382 : 0xffb547;
    ui_text(u.bar_right, s);
    lv_obj_set_style_text_color(u.bar_right, lv_color_hex(c), 0);
}

#define LOAD_WARN 85
#define LOAD_STALE_MS 2000

static void draw_load(const pl_model_t *m, uint32_t ms)
{
    char t[24] = "";
    uint32_t c = 0x9aa3b2;
    if (m->have_load && ms - m->load_ms <= LOAD_STALE_MS) {
        const pl_load_t *l = &m->load;
        int worst = l->c1_worst > l->c0_worst ? l->c1_worst : l->c0_worst;
        if (l->guard) {
            snprintf(t, sizeof t, "G%d %d %d%%", l->guard, l->c1_worst, l->c0_worst);
        } else {
            snprintf(t, sizeof t, "%d %d%%", l->c1_worst, l->c0_worst);
        }
        c = (worst >= 100 || l->underruns) ? 0xff5e6c : worst >= LOAD_WARN ? 0xffb547 : 0x9aa3b2;
    }
    ui_text(u.bar_load, t);
    if (u.load_color != c) {
        lv_obj_set_style_text_color(u.bar_load, lv_color_hex(c), 0);
        u.load_color = c;
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

// Цели макро: страница и подпись с живым значением, как они стоят на родной странице.
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

// Всплывашка: поворот на BSP, а у макро ещё и захват на экране - видно, что именно он двигает.
static void draw_toast(const pl_model_t *m, uint32_t ms)
{
    int page = m->toast_page, enc = m->toast_enc;
    int on = m->toast_until_ms && (int32_t)(m->toast_until_ms - ms) > 0;
    int vp = view_page();
    if (u.screen == PAGE && u.grab >= 0 && vp >= PL_USER_FIRST && !pl_view_is_alias(m, vp - PL_USER_FIRST, u.grab + 1)) {
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
    int up = page - PL_USER_FIRST;
    int user = page >= PL_USER_FIRST, alias = user && pl_view_is_alias(m, up, enc);
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

static void knob_input(pl_model_t *m, int ev, int down, int x, int y, int x0, int y0);

// Шаги редактора на сетке и в меню; списки целей и ход - в ui_custom.c.
static void edit_input(pl_model_t *m, int ev, int down, int x, int y, int x0, int y0, int x1, int y1, int tap)
{
    pl_custom_edit_t out;
    if (tap && y0 < TOP_H && y1 < TOP_H + 16) {
        if (x0 < 140) {
            u.ed.state = PL_EDIT_OFF; // в меню, правки уже у синта
            u.screen = MENU2;
        } else if (x0 >= EDIT_X0 && x0 < EDIT_X1 + 20) {
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
        int t = menu_page_at(x0, y0);
        if (tap && t >= 0 && t == menu_page_at(x1, y1)) {
            pl_edit_page(&u.ed, t);
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

// Сетка типов: тап по клетке - тип синту, тап мимо клеток или по шапке - закрыть без выбора.
static void pick_input(pl_model_t *m, int tap, int x0, int y0, int x1, int y1)
{
    if (!tap) {
        return;
    }
    int s = pl_tp_at(x0, y0), t = s == pl_tp_at(x1, y1) ? pl_tp_type(u.pick_page, s) : -1;
    if (t >= 0 && pl_model_local_set(m, u.pick_page, 1, t)) {
        link_set(u.pick_page, 1, t);
    }
    u.pick_page = -1;
}

static void input(pl_model_t *m, int down, int x, int y, uint32_t ms)
{
    int ev = pl_press_step(&u.press, down, x, y, ms);
    int x0, y0, x1, y1;
    pl_press_where(&u.press, &x0, &y0);
    pl_press_last(&u.press, &x1, &y1);
    int tap = pl_press_tap(ev) && (ev != PL_PRESS_RELEASE || u.grab < 0); // отпустили крутилку - не тап
    int head = tap && y0 < TOP_H && y1 < TOP_H + 16;
    if (tap && pl_view_demo_tap(m, pl_model_linked(m, ms), x0, y0, x1, y1)) {
        link_demo(); // снимок с новой песней придёт от синта
    } else if (editing()) {
        edit_input(m, ev, down, x, y, x0, y0, x1, y1, tap);
        if (editing()) {
            u.screen = u.ed.state == PL_EDIT_PICK_PAGE ? MENU : PAGE; // выбор страницы цели - на меню
        }
    } else if (u.screen == MENU || u.screen == MENU2) {
        int (*at)(int, int) = u.screen == MENU ? menu_page_at : ui_tile_at;
        int t = at(x0, y0);
        if (head && x0 >= 140 && x0 < 360) {
            u.screen = u.screen == MENU ? MENU2 : MENU;
        } else if (tap && t >= 0 && t == at(x1, y1)) {
            if (u.screen == MENU) {
                send_page(m, t, ms);
            } else if (t < PL_USER_PAGES) {
                send_page(m, PL_USER_FIRST + t, ms);
            } else if (t == PL_USER_PAGES) {
                u.screen = SCENES;
            } else if (t == TILE_GRIDS) {
                u.screen = GRIDS; // энкодеры BSP 2-13 крутят Grids, пока не тронут энкодер 1
                u.page = PL_PAGE_GRIDS;
                link_page(PL_PAGE_GRIDS);
                pl_model_page_sent(m, ms);
            } else if (t == TILE_PETRI) {
                open_dish(m, ms);
            }
        }
    } else if (u.screen == GRIDS) {
        if (head) {
            ug_release(m);
            u.screen = MENU2;
            m->goto_page = -1;
        } else {
            ug_input(m, down, x, y, x0, y0, ms);
        }
    } else if (u.screen == PETRIS) {
        if (head && x0 >= EDIT_X0 && x0 < EDIT_X1 + 20) {
            send_page(m, PL_PAGE_PETRI, ms); // ручки чашки сеткой
        } else if (head) {
            u.screen = MENU2;
            m->goto_page = -1;
        } else {
            up_input(down, x, y, x0, y0);
        }
    } else if (u.screen == SCENES) {
        if (head) {
            u.screen = MENU2;
        } else {
            uc_scenes_input(m, ev, x0, y0, x1, y1, ms - u.press.t0);
        }
    } else if (u.pick_page >= 0) {
        pick_input(m, tap, x0, y0, x1, y1);
    } else if (head) {
        int user = u.page >= PL_USER_FIRST;
        uc_xy_release(m);
        if (user && x0 >= XY_X0 && x0 < XY_X1) {
            u.xy_view ^= (uint8_t)(1 << (u.page - PL_USER_FIRST));
            u.grab = -1;
        } else if (x0 >= 140 && x0 < 215) {
            send_page(m, pl_view_page_step(u.page, -1), ms);
        } else if (x0 >= 265 && x0 < 360) {
            send_page(m, pl_view_page_step(u.page, 1), ms);
        } else if (user && x0 >= EDIT_X0 && x0 < EDIT_X1 && (m->user_have >> (u.page - PL_USER_FIRST) & 1)) {
            pl_edit_start(&u.ed, u.page - PL_USER_FIRST);
            u.grab = -1;
        } else if (!user && x0 >= EDIT_X0 && x0 < EDIT_X1 + 20 && pl_view_dish(m, u.page)) {
            open_dish(m, ms);
        } else {
            u.screen = user ? MENU2 : MENU;
            u.grab = -1;
            m->goto_page = -1; // тап «в меню» новее листания на BSP
        }
    } else if (xy_on()) {
        uc_xy_input(m, u.page, ev, down, x, y, x0, y0, ms);
    } else {
        int cell = pl_grid_cell(x0, y0);
        if (tap && m->have && cell >= 0 && cell == pl_grid_cell(x1, y1) && pl_tp_on(u.page, cell + 1)) {
            u.pick_page = u.page;
        }
        knob_input(m, ev, down, x, y, x0, y0);
    }
    if (m->goto_page >= 0 && !down && u.grab < 0 && !editing()) {
        ug_release(m);
        open_page(m->goto_page);
        m->goto_page = -1;
    }
}

static void knob_input(pl_model_t *m, int ev, int down, int x, int y, int x0, int y0)
{
    // Захват по удержанию 50 мс или сразу при начале сдвига пальца внутри рабочей области страницы.
    if (down && u.grab < 0 && y0 >= TOP_H && (ev == PL_PRESS_HOLD || u.press.state == 2)) {
        int i = pl_grid_cell(x0, y0);
        pl_cell_t cell;
        pl_view_cell(m, u.page, i + 1, &cell);
        if (i >= 0 && i < KNOBS && cell.label && m->have && !pl_tp_on(u.page, i + 1)) { // TYPE - сеткой // без снимка значения нули: ручка прыгнула бы в 0
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
            if (pl_model_local_set(m, u.page, u.grab + 1, v)) {
                link_set(u.page, u.grab + 1, v);
            }
            u.sent = v;
        }
    }
}

void ui_start(void)
{
    lvgl_port_lock(0);
    build();
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon); // куча LVGL 96 КБ: при нехватке падает в первой отрисовке, а не здесь
    ESP_LOGI("panel", "lvgl heap: used %d%%, free %u of %u, biggest %u; internal RAM free %u", mon.used_pct,
             (unsigned)mon.free_size, (unsigned)mon.total_size, (unsigned)mon.free_biggest_size,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    lvgl_port_unlock();
}

void ui_step(pl_model_t *m, int down, int x, int y, uint32_t ms)
{
    input(m, down, x, y, ms);
    if ((u.screen == PAGE || u.screen == GRIDS || u.screen == PETRIS) && !editing() &&
        pl_model_page_resend(m, u.page, ms)) {
        link_page(u.page);
        pl_model_page_sent(m, ms);
    }
    if (!lvgl_port_lock(1)) {
        return;
    }
    ui_show(u.menu, u.screen == MENU);
    ui_show(u.menu2, u.screen == MENU2);
    int picking = u.screen == PAGE && u.pick_page == u.page;
    ui_show(u.grid, u.screen == PAGE && !xy_on() && !picking);
    ui_show(u.pick, picking);
    uc_xy_show(xy_on());
    uc_scenes_show(u.screen == SCENES);
    ug_show(u.screen == GRIDS);
    up_show(u.screen == PETRIS);
    uc_edit_draw(m, &u.ed);
    if (xy_on()) {
        uc_xy_draw(m, u.page);
    } else if (picking) {
        int d = u.page * 32 + m->raw[u.page][1];
        if (d != u.drawn_pick) {
            u.drawn_pick = d;
            lv_obj_invalidate(u.pick);
        }
    } else if (u.screen == PAGE) {
        draw_page(m);
    } else if (u.screen == MENU) {
        draw_menu(m);
    } else if (u.screen == MENU2) {
        draw_menu2(m);
    } else if (u.screen == GRIDS) {
        ug_draw(m);
    } else if (u.screen == PETRIS) {
        up_draw(m, ms);
    } else {
        uc_scenes_draw(m);
    }
    draw_bar(m);
    ui_show(u.bar_nav, 1);
    draw_status(m, ms);
    draw_load(m, ms);
    draw_toast(m, ms);
    lvgl_port_unlock();
}
