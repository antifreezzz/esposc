// Экран PERF (дом панели): сцены 1-8, морф, XY-пэд USER 1, FILL, GRIDS ON, статтер, DJ-фильтр,
// пампинг и 7 ползунков громкости. Один объект LVGL со своей отрисовкой (LV_EVENT_DRAW_MAIN).
#include <stdio.h>

#include "link.h"
#include "pl_gesture.h"
#include "pl_labels.h"
#include "pl_perf_ui.h"
#include "pl_view.h"
#include "pl_xy.h"
#include "ui_priv.h"

#define SEND_EVERY_MS 10
#define STORE_MS 600 // удержание плитки сцены дольше - запись
#define SAVED_MS 800 // вспышка «OK» после записи сцены
#define XY_DOT 18

static const char *const STUTTER_TXT[PL_PUI_STUTTERS] = {"OFF", "1/4", "1/8", "1/16", "1/32"};
static const char *const FADER_NAME[PL_PUI_FADERS] = {"MASTER", "KICK", "HATS", "CLAP", "SNARE", "SYN 1", "SYN 2"};
static const uint8_t FADER_CPAGE[PL_PUI_FADERS] = {0, 1, 1, 1, 1, 6, 7};

static struct {
    lv_obj_t *root;
    const pl_model_t *m;
    pl_xy_t xy;
    int xy_active;
    int filling;
    int drag_id, drag_idx;
    int want, sent;
    uint32_t sent_at;
    int bars;
    int saved_slot;
    uint32_t saved_until, now;
    int state_key;
} pf;

int upf_morph(void)
{
    return pf.bars;
}

void upf_set_morph(int bars)
{
    if (bars >= 0 && bars <= 8) {
        pf.bars = bars;
    }
}

static void xy_axes(const pl_model_t *m, int cur[2], pl_cell_t cell[2])
{
    for (int a = 0; a < 2; a++) {
        pl_view_cell(m, PL_USER_FIRST, 1 + a, &cell[a]);
        cur[a] = cell[a].label && m->have ? cell[a].v : -1;
        if (cur[a] >= 0) {
            pl_xy_range(&pf.xy, a, cell[a].lo, cell[a].hi);
        }
    }
}

static void xy_send(pl_model_t *m, int mask, const int out[2])
{
    for (int a = 0; a < 2; a++) {
        if ((mask >> a & 1) && pl_model_local_set(m, PL_USER_FIRST, 1 + a, out[a])) {
            link_set(PL_USER_FIRST, 1 + a, out[a]);
        }
    }
}

static void send_knob(pl_model_t *m, int page, int enc, int v, uint32_t ms, int force)
{
    pf.want = v;
    if (v == pf.sent || (!force && pf.sent >= 0 && ms - pf.sent_at < SEND_EVERY_MS)) {
        return;
    }
    if (pl_model_local_set(m, page, enc, v)) {
        link_set(page, enc, v);
    }
    pf.sent = v;
    pf.sent_at = ms;
}

static void draw_tile(lv_layer_t *layer, int ox, int oy, int x, int y, int w, int h, uint32_t bg, int on, uint32_t brd)
{
    lv_area_t a = {ox + x, oy + y, ox + x + w - 1, oy + y + h - 1};
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.radius = 10;
    f.color = lv_color_hex(bg);
    lv_draw_fill(layer, &f, &a);
    lv_draw_border_dsc_t b;
    lv_draw_border_dsc_init(&b);
    b.radius = 10;
    b.width = on ? 2 : 1;
    b.color = lv_color_hex(on ? brd : C_TILE_BRD);
    lv_draw_border(layer, &b, &a);
}

static void draw_text(lv_layer_t *layer, int x1, int y1, int x2, int y2, const lv_font_t *font, uint32_t col,
                      lv_text_align_t align, const char *txt)
{
    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.font = font;
    l.color = lv_color_hex(col);
    l.align = align;
    l.text = txt;
    l.text_local = 1;
    lv_area_t a = {x1, y1, x2, y2};
    lv_draw_label(layer, &l, &a);
}

static void draw_hbar(lv_layer_t *layer, int ox, int oy, int id, const char *name, int center)
{
    const pl_model_t *m = pf.m;
    int x, y, w, h, page, enc;
    pl_pui_rect(id, 0, &x, &y, &w, &h);
    pl_pui_target(id, 0, &page, &enc);
    int v = m->raw[page][enc];
    uint32_t pc = pl_page_color(page);

    lv_area_t a = {ox + x, oy + y, ox + x + w - 1, oy + y + h - 1};
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.radius = 10;
    f.color = lv_color_hex(C_TILE);
    lv_draw_fill(layer, &f, &a);

    int px = (w - 4) * v / 127;
    f.radius = 4;
    f.color = lv_color_hex(ui_mix(pc, C_BG, 128));
    if (center) {
        int mid = (w - 4) / 2;
        if (v != 64) {
            int x0 = px < mid ? px : mid, x1 = px > mid ? px : mid;
            lv_area_t fa = {a.x1 + 2 + x0, a.y1 + 2, a.x1 + 2 + x1, a.y2 - 2};
            lv_draw_fill(layer, &f, &fa);
        }
        f.radius = 0;
        f.color = lv_color_hex(C_MUTED);
        lv_area_t ma = {a.x1 + w / 2 - 1, a.y1 + 2, a.x1 + w / 2, a.y2 - 2};
        lv_draw_fill(layer, &f, &ma);
    } else if (px > 0) {
        lv_area_t fa = {a.x1 + 2, a.y1 + 2, a.x1 + 1 + px, a.y2 - 2};
        lv_draw_fill(layer, &f, &fa);
    }

    if (m->held[page][enc] && m->bsp_pos[enc] != 0xFF) {
        int bx = a.x1 + 2 + (w - 4) * m->bsp_pos[enc] / 127;
        f.radius = 1;
        f.color = lv_color_hex(C_BSP);
        lv_area_t ba = {bx - 1, a.y1 + 2, bx + 1, a.y2 - 2};
        lv_draw_fill(layer, &f, &ba);
    }

    lv_draw_border_dsc_t b;
    lv_draw_border_dsc_init(&b);
    b.radius = 10;
    b.width = 1;
    b.color = lv_color_hex(C_TILE_BRD);
    lv_draw_border(layer, &b, &a);

    draw_text(layer, a.x1 + 10, a.y1 + 11, a.x2 - 80, a.y2, &bc_15, C_TEXT, LV_TEXT_ALIGN_LEFT, name);
    char vbuf[16];
    if (center && v == 64) {
        snprintf(vbuf, sizeof vbuf, "OFF");
    } else if (center) {
        snprintf(vbuf, sizeof vbuf, "%s %+d", v < 64 ? "LP" : "HP", v - 64);
    } else {
        snprintf(vbuf, sizeof vbuf, "%d", v);
    }
    draw_text(layer, a.x1 + 80, a.y1 + 10, a.x2 - 10, a.y2, &bc_17, C_TEXT, LV_TEXT_ALIGN_RIGHT, vbuf);
}

static void draw_perf(lv_event_t *e)
{
    const pl_model_t *m = pf.m;
    if (!m) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t base;
    lv_obj_get_coords(pf.root, &base);
    int ox = base.x1, oy = base.y1 - UI_TOP_H;

    // 1. Сцены 1-8 и MORPH.
    for (int i = 0; i < PL_PUI_SCENES; i++) {
        int x, y, w, h;
        pl_pui_rect(PL_PUI_SCENE, i, &x, &y, &w, &h);
        int used = m->scene.used >> i & 1, cur = m->scene.cur == i;
        int saved = pf.saved_slot == i && pf.saved_until > pf.now;
        uint32_t bg = used ? C_TILE : ui_mix(C_TILE, C_BG, 128);
        uint32_t brd = saved ? C_OK : C_ACCENT;
        draw_tile(layer, ox, oy, x, y, w, h, bg, saved || cur, brd);
        char s[4] = {(char)('1' + i), 0};
        uint32_t tc = saved ? C_OK : cur ? C_ACCENT : used ? C_TEXT : C_MUTED;
        draw_text(layer, ox + x, oy + y + (saved ? 17 : 14), ox + x + w - 1, oy + y + h - 1,
                  saved ? &bc_17 : &bc_22, tc, LV_TEXT_ALIGN_CENTER, saved ? "OK" : s);
    }
    {
        int x, y, w, h;
        pl_pui_rect(PL_PUI_MORPH, 0, &x, &y, &w, &h);
        draw_tile(layer, ox, oy, x, y, w, h, C_TILE, 0, C_TILE_BRD);
        draw_text(layer, ox + x, oy + y + 6, ox + x + w - 1, oy + y + 22, &bc_15, C_DIM, LV_TEXT_ALIGN_CENTER, "MORPH");
        char mbuf[8];
        snprintf(mbuf, sizeof mbuf, "%d", pf.bars);
        draw_text(layer, ox + x, oy + y + 23, ox + x + w - 1, oy + y + h - 1, &bc_22, C_TEXT, LV_TEXT_ALIGN_CENTER,
                  mbuf);
    }

    // 2. XY-пэд USER 1 (200x200).
    {
        int x, y, w, h;
        pl_pui_rect(PL_PUI_XY, 0, &x, &y, &w, &h);
        draw_tile(layer, ox, oy, x, y, w, h, C_FIELD, 0, C_TILE_BRD);
        int cur[2];
        pl_cell_t cell[2];
        xy_axes(m, cur, cell);
        int pos0 = cur[0] < 0 ? 64 : pl_xy_pos(&pf.xy, 0, cur[0]);
        int pos1 = cur[1] < 0 ? 64 : pl_xy_pos(&pf.xy, 1, cur[1]);
        int px, py;
        pl_xy_point(&pf.xy, pos0, pos1, &px, &py);

        lv_draw_fill_dsc_t f;
        lv_draw_fill_dsc_init(&f);
        f.color = lv_color_hex(C_TRACK);
        lv_area_t vl = {ox + px - 1, oy + y + 1, ox + px, oy + y + h - 2};
        lv_area_t hl = {ox + x + 1, oy + py - 1, ox + x + w - 2, oy + py};
        lv_draw_fill(layer, &f, &vl);
        lv_draw_fill(layer, &f, &hl);
        if (cur[0] >= 0 || cur[1] >= 0) {
            uint32_t dc = cell[0].color ? cell[0].color : cell[1].color ? cell[1].color : C_ACCENT;
            f.color = lv_color_hex(dc);
            f.radius = XY_DOT / 2;
            lv_area_t da = {ox + px - XY_DOT / 2, oy + py - XY_DOT / 2, ox + px + XY_DOT / 2 - 1,
                            oy + py + XY_DOT / 2 - 1};
            lv_draw_fill(layer, &f, &da);
        }
        draw_text(layer, ox + x + 8, oy + y + 6, ox + x + w - 8, oy + y + 24, &bc_15, C_MUTED, LV_TEXT_ALIGN_LEFT,
                  "XY  USER 1");
        char sub[40];
        if (cur[0] < 0 && cur[1] < 0) {
            snprintf(sub, sizeof sub, "EMPTY");
        } else {
            snprintf(sub, sizeof sub, "%s %s / %s %s", cell[0].label ? cell[0].label : "-",
                     cur[0] >= 0 ? cell[0].text : "", cell[1].label ? cell[1].label : "-",
                     cur[1] >= 0 ? cell[1].text : "");
        }
        draw_text(layer, ox + x + 8, oy + y + h - 22, ox + x + w - 8, oy + y + h - 2, &bc_15, C_MUTED,
                  LV_TEXT_ALIGN_LEFT, sub);
    }

    // 3. FILL и GRIDS ON.
    {
        int x, y, w, h;
        uint32_t gc = pl_page_color(PL_PAGE_GRIDS);
        int fon = m->raw[PL_PAGE_GRIDS][12] != 0;
        pl_pui_rect(PL_PUI_FILL, 0, &x, &y, &w, &h);
        draw_tile(layer, ox, oy, x, y, w, h, C_TILE, fon, gc);
        draw_text(layer, ox + x, oy + y + 14, ox + x + w - 1, oy + y + h - 1, &bc_22, fon ? gc : C_DIM,
                  LV_TEXT_ALIGN_CENTER, "FILL");

        int gon = m->raw[PL_PAGE_GRIDS][1] != 0;
        pl_pui_rect(PL_PUI_GRIDS, 0, &x, &y, &w, &h);
        draw_tile(layer, ox, oy, x, y, w, h, C_TILE, gon, gc);
        draw_text(layer, ox + x, oy + y + 17, ox + x + w - 1, oy + y + h - 1, &bc_17, gon ? gc : C_DIM,
                  LV_TEXT_ALIGN_CENTER, gon ? "GRIDS ON" : "GRIDS OFF");
    }

    // 4. STUTTER (5 сегментов).
    draw_text(layer, ox + 216, oy + 163, ox + 472, oy + 180, &bc_15, C_MUTED, LV_TEXT_ALIGN_LEFT, "STUTTER");
    {
        uint32_t fc = pl_page_color(8);
        int cur = m->raw[8][9];
        for (int i = 0; i < PL_PUI_STUTTERS; i++) {
            int x, y, w, h, on = i == cur;
            pl_pui_rect(PL_PUI_STUTTER, i, &x, &y, &w, &h);
            draw_tile(layer, ox, oy, x, y, w, h, C_TILE, on, fc);
            draw_text(layer, ox + x, oy + y + 10, ox + x + w - 1, oy + y + h - 1, &bc_17, on ? fc : C_DIM,
                      LV_TEXT_ALIGN_CENTER, STUTTER_TXT[i]);
        }
    }

    // 5. Горизонтальные ползунки DJ FILTER и PUMP.
    draw_hbar(layer, ox, oy, PL_PUI_DJF, "DJ FILTER", 1);
    draw_hbar(layer, ox, oy, PL_PUI_PUMP, "PUMP", 0);

    // 6. 7 вертикальных ползунков громкости.
    for (int i = 0; i < PL_PUI_FADERS; i++) {
        int x, y, w, h, page, enc;
        pl_pui_rect(PL_PUI_FADER, i, &x, &y, &w, &h);
        pl_pui_target(PL_PUI_FADER, i, &page, &enc);
        int v = m->raw[page][enc];
        uint32_t pc = pl_page_color(FADER_CPAGE[i]);

        lv_area_t a = {ox + x, oy + y, ox + x + w - 1, oy + y + h - 1};
        lv_draw_fill_dsc_t f;
        lv_draw_fill_dsc_init(&f);
        f.radius = 10;
        f.color = lv_color_hex(C_TILE);
        lv_draw_fill(layer, &f, &a);

        int fh = (h - 4) * v / 127;
        if (fh > 0) {
            f.radius = fh >= h - 8 ? 8 : 3;
            f.color = lv_color_hex(ui_mix(pc, C_BG, 128));
            lv_area_t fa = {a.x1 + 2, a.y2 - 1 - fh, a.x2 - 2, a.y2 - 2};
            lv_draw_fill(layer, &f, &fa);
        }

        if (m->held[page][enc] && m->bsp_pos[enc] != 0xFF) {
            int by = a.y2 - 2 - (h - 4) * m->bsp_pos[enc] / 127;
            f.radius = 1;
            f.color = lv_color_hex(C_BSP);
            lv_area_t ba = {a.x1 + 4, by - 1, a.x2 - 4, by + 1};
            lv_draw_fill(layer, &f, &ba);
        }

        lv_draw_border_dsc_t b;
        lv_draw_border_dsc_init(&b);
        b.radius = 10;
        b.width = 1;
        b.color = lv_color_hex(C_TILE_BRD);
        lv_draw_border(layer, &b, &a);

        draw_text(layer, a.x1, a.y1 + 6, a.x2, a.y1 + 22, &bc_15, C_TEXT, LV_TEXT_ALIGN_CENTER, FADER_NAME[i]);
        char vbuf[8];
        snprintf(vbuf, sizeof vbuf, "%d", v);
        draw_text(layer, a.x1, a.y1 + 83, a.x2, a.y2, &bc_17, C_TEXT, LV_TEXT_ALIGN_CENTER, vbuf);
    }
}

void upf_build(lv_obj_t *scr)
{
    pf.root = ui_box(scr, 0, UI_TOP_H, UI_W, UI_DOCK_Y - UI_TOP_H, C_BG);
    lv_obj_set_style_bg_opa(pf.root, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(pf.root, draw_perf, LV_EVENT_DRAW_MAIN, NULL);
    int x, y, w, h;
    pl_pui_rect(PL_PUI_XY, 0, &x, &y, &w, &h);
    pl_xy_init(&pf.xy, x, y, w, h);
    pf.saved_slot = -1;
    pf.state_key = -1;
    ui_show(pf.root, 0);
}

void upf_show(int on)
{
    ui_show(pf.root, on);
    if (on) {
        pf.state_key = -1;
    }
}

void upf_draw(const pl_model_t *m, uint32_t ms)
{
    pf.m = m;
    pf.now = ms;
    int cur[2];
    pl_cell_t cell[2];
    xy_axes(m, cur, cell);
    int k = (m->scene.cur & 15) | ((int)m->scene.used << 4) | (pf.bars << 12) | ((pf.saved_until > ms) << 16) |
            (m->raw[PL_PAGE_GRIDS][1] << 17) | (m->raw[PL_PAGE_GRIDS][12] << 18) | (m->raw[8][9] << 19) |
            ((m->raw[0][11] << 22) ^ (m->raw[0][9] * 131) ^ (cur[0] * 17) ^ (cur[1] * 53));
    for (int i = 0; i < PL_PUI_FADERS; i++) {
        int p, e;
        pl_pui_target(PL_PUI_FADER, i, &p, &e);
        k = (k * 33) ^ m->raw[p][e] ^ (m->held[p][e] << 7) ^ ((int)m->bsp_pos[e] << 8);
    }
    k ^= (m->held[0][11] << 9) ^ (m->held[0][9] << 10) ^ ((int)m->bsp_pos[11] << 11) ^ ((int)m->bsp_pos[9] << 12);
    if (k != pf.state_key) {
        pf.state_key = k;
        lv_obj_invalidate(pf.root);
    }
}

void upf_release(pl_model_t *m)
{
    int out[2];
    if (pf.xy_active) {
        xy_send(m, pl_xy_release(&pf.xy, out), out);
        pf.xy_active = 0;
    }
    if (pf.filling) {
        pf.filling = 0;
        if (pl_model_local_set(m, PL_PAGE_GRIDS, 12, 0)) {
            link_set(PL_PAGE_GRIDS, 12, 0);
        }
    }
    if (pf.drag_id) {
        int page, enc;
        if (pl_pui_target(pf.drag_id, pf.drag_idx, &page, &enc)) {
            send_knob(m, page, enc, pf.want, 0, 1);
        }
        pf.drag_id = PL_PUI_NONE;
    }
}

void upf_input(pl_model_t *m, int ev, int down, int x, int y, int x0, int y0, int x1, int y1, uint32_t held_ms,
               uint32_t ms)
{
    int arg0 = -1, id0 = pl_pui_at(x0, y0, &arg0);

    // Сцены 1-8 и MORPH: тап (вызвать) или удержание 0.6 с (записать), как в uc_scenes_input.
    if (id0 == PL_PUI_SCENE) {
        int arg1 = -1, id1 = pl_pui_at(x1, y1, &arg1);
        if (pl_press_tap(ev) && id1 == PL_PUI_SCENE && arg1 == arg0 && m->have) {
            if (ev == PL_PRESS_RELEASE && held_ms >= STORE_MS) {
                link_scene(PL_SCENE_STORE, arg0, 0);
                m->scene.used |= (uint8_t)(1 << arg0);
                m->scene.cur = (uint8_t)arg0;
                pf.saved_slot = arg0;
                pf.saved_until = ms + SAVED_MS;
            } else if (m->scene.used >> arg0 & 1) {
                link_scene(PL_SCENE_RECALL, arg0, pf.bars);
            }
        }
        return;
    }
    if (id0 == PL_PUI_MORPH) {
        int arg1 = -1, id1 = pl_pui_at(x1, y1, &arg1);
        if (pl_press_tap(ev) && id1 == PL_PUI_MORPH) {
            ui_open_morph();
        }
        return;
    }

    if (!down) {
        upf_release(m);
        return;
    }

    // XY-пэд USER 1.
    if (pf.xy_active || id0 == PL_PUI_XY) {
        int cur[2], out[2];
        pl_cell_t cell[2];
        xy_axes(m, cur, cell);
        if (!pf.xy_active && cur[0] < 0 && cur[1] < 0) {
            return;
        }
        pf.xy_active = 1;
        xy_send(m, pl_xy_touch(&pf.xy, x, y, cur, ms, out), out);
        return;
    }

    // Одиночные нажатия и переключатели: только в момент касания (ev != PL_PRESS_NONE нельзя - press_update шлёт 0).
    if (id0 == PL_PUI_FILL) {
        if (!pf.filling) {
            pf.filling = 1;
            if (pl_model_local_set(m, PL_PAGE_GRIDS, 12, 1)) {
                link_set(PL_PAGE_GRIDS, 12, 1);
            }
        }
        return;
    }
    if (id0 == PL_PUI_GRIDS) {
        if (pf.drag_id != PL_PUI_GRIDS) {
            pf.drag_id = PL_PUI_GRIDS;
            int v = !m->raw[PL_PAGE_GRIDS][1];
            if (pl_model_local_set(m, PL_PAGE_GRIDS, 1, v)) {
                link_set(PL_PAGE_GRIDS, 1, v);
            }
        }
        return;
    }
    if (id0 == PL_PUI_STUTTER) {
        int arg = -1, id = pl_pui_at(x, y, &arg);
        int seg = id == PL_PUI_STUTTER ? arg : arg0;
        if (pl_model_local_set(m, 8, 9, seg)) {
            link_set(8, 9, seg);
        }
        return;
    }

    // Ползунки: DJF, PUMP и 7 громкостей.
    if (!pf.drag_id && (id0 == PL_PUI_DJF || id0 == PL_PUI_PUMP || id0 == PL_PUI_FADER)) {
        pf.drag_id = id0;
        pf.drag_idx = arg0;
        pf.sent = -1;
    }
    if (pf.drag_id == PL_PUI_DJF || pf.drag_id == PL_PUI_PUMP || pf.drag_id == PL_PUI_FADER) {
        int page, enc;
        pl_pui_target(pf.drag_id, pf.drag_idx, &page, &enc);
        int v = pf.drag_id == PL_PUI_FADER ? pl_pui_fader_value(y) : pl_pui_hbar_value(x);
        if (pf.drag_id == PL_PUI_DJF && v >= 61 && v <= 67) {
            v = 64; // прилипание к середине (OFF)
        }
        send_knob(m, page, enc, v, ms, 0);
    }
}
