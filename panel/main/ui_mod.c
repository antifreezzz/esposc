// Нижний ряд страницы модулятора: форма и текущий выход одним объектом LVGL, рядом кнопка TARGETS.
#include <math.h>
#include <stdio.h>

#include "pl_gesture.h"
#include "pl_mod.h"
#include "ui_priv.h"

#define WAVE_X 9
#define WAVE_Y (UI_WORK_Y + 4 + 4 * PL_IA_ROW_H)
#define WAVE_W 306
#define WAVE_H PL_CARD_H
#define TARGET_X 321
#define TARGET_W PL_CARD_W
#define SAMPLES 76

static struct {
    lv_obj_t *wave, *targets, *label;
    int shape, out, attack, decay, dot_x, count;
    int drawn_shape, drawn_out, drawn_attack, drawn_decay, drawn_dot_x, drawn_count;
    float phase;
    uint32_t last_ms;
} md;

static float rnd01(int n)
{
    uint32_t x = (uint32_t)n * 1664525u + 1013904223u;
    x ^= x >> 16;
    return (float)(x & 0xffff) * (1.0f / 65535.0f);
}

static float shape_at(int shape, float p, int attack, int decay)
{
    switch (shape) {
    case 0: return sinf(2.0f * (float)M_PI * p);
    case 1: return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
    case 2: return 1.0f - 2.0f * p;
    case 3: return 2.0f * p - 1.0f;
    case 4: return p < 0.5f ? 1.0f : -1.0f;
    case 5: return rnd01((int)(p * 8.0f)) * 2.0f - 1.0f;
    case 6: {
        float x = p * 8.0f;
        int n = (int)x;
        float f = x - (float)n;
        float a = rnd01(n) * 2.0f - 1.0f, b = rnd01(n + 1) * 2.0f - 1.0f;
        f = f * f * (3.0f - 2.0f * f);
        return a + (b - a) * f;
    }
    default: {
        float aw = 0.08f + 0.32f * (float)(attack + 1) / (float)(attack + decay + 2);
        if (p < aw) {
            return p / aw;
        }
        return exp2f(-6.644f * (p - aw) / (1.0f - aw));
    }
    }
}

static void wave_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t base;
    lv_obj_get_coords(md.wave, &base);
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.color = lv_color_hex(C_TRACK);
    int zero = md.shape == 7 ? WAVE_H - 10 : WAVE_H / 2;
    lv_area_t z = {base.x1 + 7, base.y1 + zero, base.x2 - 7, base.y1 + zero};
    lv_draw_fill(layer, &f, &z);

    f.color = lv_color_hex(C_MOD);
    f.radius = 1;
    for (int i = 0; i < SAMPLES; i++) {
        float p = (float)i / (float)(SAMPLES - 1);
        float v = shape_at(md.drawn_shape, p, md.drawn_attack, md.drawn_decay);
        int y = md.drawn_shape == 7 ? zero - (int)(v * (WAVE_H - 20)) : zero - (int)(v * (WAVE_H / 2 - 8));
        int x = 8 + i * (WAVE_W - 16) / (SAMPLES - 1);
        lv_area_t a = {base.x1 + x, base.y1 + (y < zero ? y : zero), base.x1 + x + 1,
                       base.y1 + (y < zero ? zero : y)};
        lv_draw_fill(layer, &f, &a);
    }

    float v = md.drawn_shape == 7 ? (float)md.drawn_out / 127.0f : (float)md.drawn_out / 127.0f;
    int y = md.drawn_shape == 7 ? zero - (int)(v * (WAVE_H - 20)) : zero - (int)(v * (WAVE_H / 2 - 8));
    f.color = lv_color_hex(C_TEXT);
    f.radius = 5;
    lv_area_t dot = {base.x1 + md.drawn_dot_x - 5, base.y1 + y - 5, base.x1 + md.drawn_dot_x + 4,
                     base.y1 + y + 4};
    lv_draw_fill(layer, &f, &dot);
}

void um_build(lv_obj_t *grid)
{
    md.wave = ui_box(grid, WAVE_X, WAVE_Y - UI_WORK_Y, WAVE_W, WAVE_H, C_FIELD);
    lv_obj_set_style_radius(md.wave, 10, 0);
    lv_obj_set_style_border_width(md.wave, 1, 0);
    lv_obj_set_style_border_color(md.wave, lv_color_hex(C_TILE_BRD), 0);
    lv_obj_add_event_cb(md.wave, wave_draw, LV_EVENT_DRAW_MAIN, NULL);

    md.targets = ui_box(grid, TARGET_X, WAVE_Y - UI_WORK_Y, TARGET_W, WAVE_H, C_TILE);
    lv_obj_set_style_radius(md.targets, 10, 0);
    lv_obj_set_style_border_width(md.targets, 1, 0);
    lv_obj_set_style_border_color(md.targets, lv_color_hex(C_MOD), 0);
    md.label = ui_label(md.targets, &bc_17, C_MOD);
    lv_obj_center(md.label);
    md.drawn_shape = md.drawn_out = md.drawn_attack = md.drawn_decay = md.drawn_dot_x = md.drawn_count = -1000;
    md.count = -1;
    um_show(0);
}

void um_show(int on)
{
    ui_show(md.wave, on);
    ui_show(md.targets, on);
    if (!on) {
        md.last_ms = 0;
    }
}

int um_targets_at(int x, int y)
{
    return x >= TARGET_X && x < TARGET_X + TARGET_W && y >= WAVE_Y && y < WAVE_Y + WAVE_H;
}

void um_draw(const pl_model_t *m, int page, uint32_t ms)
{
    int k = page - PL_PAGE_MOD1;
    const uint8_t *raw = m->mod_knob[k];
    md.shape = raw[1];
    md.out = m->mod_out[k];
    md.attack = raw[6];
    md.decay = raw[7];
    md.count = pl_mod_count(m, k);

    if (!md.last_ms) {
        md.last_ms = ms;
    }
    float dt = (float)(ms - md.last_ms) * 0.001f;
    md.last_ms = ms;
    static const uint16_t ticks[9] = {0, 6, 12, 24, 48, 96, 192, 384, 768};
    int sync = raw[3] < 9 ? raw[3] : 0;
    float hz = 0.05f * exp2f((float)raw[2] * (8.644f / 127.0f));
    int bpm = (m->seq_have & 1) ? m->seq[0].bpm : (m->seq_have & 2) ? m->seq[1].bpm : 120;
    if (bpm <= 0) {
        bpm = m->seq_pos[0].bpm ? m->seq_pos[0].bpm : m->seq_pos[1].bpm;
    }
    if (bpm <= 0) {
        bpm = 120;
    }
    float inc = sync ? dt * (float)bpm / ((float)ticks[sync] * 2.5f) : dt * hz;
    md.phase += inc;
    md.phase -= floorf(md.phase);
    md.dot_x = 8 + (int)(md.phase * (WAVE_W - 16));

    if (md.drawn_shape != md.shape || md.drawn_out != md.out || md.drawn_attack != md.attack ||
        md.drawn_decay != md.decay || md.drawn_dot_x != md.dot_x) {
        md.drawn_shape = md.shape;
        md.drawn_out = md.out;
        md.drawn_attack = md.attack;
        md.drawn_decay = md.decay;
        md.drawn_dot_x = md.dot_x;
        lv_obj_invalidate(md.wave);
    }
    if (md.drawn_count != md.count) {
        char text[20];
        snprintf(text, sizeof text, "TARGETS %d", md.count);
        ui_text(md.label, text);
        md.drawn_count = md.count;
    }
}
