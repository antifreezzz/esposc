// Экран чашки Петри: сетка 96x96 крупно x3 одной картинкой в PSRAM (объект LVGL на клетку дорог), поверх орбита
#include <stdlib.h>
// и жители. Тап и ведение пальцем - капля. Картинка перерисовывается не чаще кадра чашки и не чаще 10 раз в секунду.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "petri_color.h"
#include "petri_task.h"
#include "pl_labels.h"
#include "ui_priv.h"

#define SCALE 3
#define SIDE (PETRI_N * SCALE) // 288
#define IMG_X ((UI_W - SIDE) / 2)
#define IMG_Y 12               // внутри рабочей области между вкладками и доком
#define REDRAW_MS 100
#define DROP_PX 6 // ведение пальцем: капля не чаще, чем через столько пикселей

static struct {
    lv_obj_t *box, *img, *info;
    lv_image_dsc_t dsc;
    uint16_t *px; // SIDE x SIDE RGB565, PSRAM
    uint16_t lut[256];
    uint32_t drawn_frame, drawn_ms;
    int lx, ly; // последняя капля
} p;

void up_build(lv_obj_t *scr)
{
    p.box = ui_box(scr, 0, UI_WORK_Y, UI_W, UI_WORK_H, C_BG);
    p.px = heap_caps_calloc(SIDE * SIDE, 2, MALLOC_CAP_SPIRAM);
    petri_palette(p.lut);
    if (p.px) {
        p.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        p.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        p.dsc.header.w = SIDE;
        p.dsc.header.h = SIDE;
        p.dsc.header.stride = SIDE * 2;
        p.dsc.data = (const uint8_t *)p.px;
        p.dsc.data_size = SIDE * SIDE * 2;
        p.img = lv_image_create(p.box);
        lv_image_set_src(p.img, &p.dsc);
        lv_obj_set_pos(p.img, IMG_X, IMG_Y);
    }
    p.info = ui_label(p.box, &bc_17, C_DIM);
    lv_obj_align(p.info, LV_ALIGN_TOP_MID, 0, IMG_Y + SIDE + 12);
    p.drawn_frame = UINT32_MAX;
}

void up_show(int on)
{
    ui_show(p.box, on);
}

static void dot(int x, int y, int r, uint16_t c)
{
    x = ((x % SIDE) + SIDE) % SIDE;
    y = ((y % SIDE) + SIDE) % SIDE;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int xx = x + dx, yy = y + dy;
            if (xx >= 0 && xx < SIDE && yy >= 0 && yy < SIDE) {
                p.px[yy * SIDE + xx] = c;
            }
        }
    }
}

static void paint(const petri_t *d)
{
    for (int y = 0; y < PETRI_N; y++) {
        uint16_t *row = p.px + y * SCALE * SIDE;
        for (int x = 0; x < PETRI_N; x++) {
            uint16_t c = petri_cell_color(p.lut, d->v[y * PETRI_N + x], d->dk[y * PETRI_N + x]);
            for (int k = 0; k < SCALE; k++) {
                row[x * SCALE + k] = c;
            }
        }
        for (int k = 1; k < SCALE; k++) {
            memcpy(row + k * SIDE, row, SIDE * 2);
        }
    }
    const uint16_t ORBIT = PETRI_RGB565(255, 255, 255);
    for (int j = 0; j < 128; j++) {
        float x, y;
        petri_orbit_point(d, 6.2831853f * j / 128, &x, &y);
        dot((int)(petri_wrapf(x) * SCALE), (int)(petri_wrapf(y) * SCALE), 1, ORBIT);
    }
    static const uint16_t AGENT[PETRI_KINDS] = {PETRI_RGB565(238, 242, 240), PETRI_RGB565(242, 199, 110),
                                                PETRI_RGB565(255, 107, 107), PETRI_RGB565(170, 190, 200)};
    for (int i = 0; i < d->n; i++) {
        const petri_agent_t *a = &d->a[i];
        uint16_t c = a->kind == PETRI_MUTAGEN && a->sign < 0 ? PETRI_RGB565(107, 184, 255) : AGENT[a->kind];
        int r = a->kind == PETRI_GRAZER ? 3 : a->kind == PETRI_VORTEX ? 4 : 2;
        dot((int)(a->x * SCALE), (int)(a->y * SCALE), r, c);
        if (a->kind == PETRI_GRAZER || a->kind == PETRI_VORTEX) {
            dot((int)(a->x * SCALE), (int)(a->y * SCALE), r - 2, PETRI_RGB565(14, 19, 21)); // кольцо
        }
    }
}

void up_draw(const pl_model_t *m, uint32_t ms)
{
    const petri_t *d = petri_task_dish();
    int on = m->have && pl_petri_synth(m->raw) >= 0;
    char buf[64], pre[12];
    if (!on) {
        ui_text(p.info, "SET SYNTH 1 OR 2 TYPE TO PETRI");
        return;
    }
    if (!d) {
        ui_text(p.info, petri_task_no_ram() ? "NO RAM FOR THE DISH, RETRYING" : "GROWING THE DISH...");
        return;
    }
    uint32_t f = petri_task_frames();
    if (!p.px || f == p.drawn_frame || ms - p.drawn_ms < REDRAW_MS) {
        return;
    }
    paint(d);
    lv_obj_invalidate(p.img);
    p.drawn_frame = f;
    p.drawn_ms = ms;
    snprintf(buf, sizeof buf, "%s   G%d S%d M%d V%d", pl_value_text(PL_PAGE_PETRI, 1, m->raw[PL_PAGE_PETRI][1],
                                                                   m->raw[PL_PAGE_PETRI], pre),
             petri_count(d, PETRI_GRAZER), petri_count(d, PETRI_SEEDER), petri_count(d, PETRI_MUTAGEN),
             petri_count(d, PETRI_VORTEX));
    ui_text(p.info, buf);
}

void up_input(int down, int x, int y, int x0, int y0)
{
    (void)x0;
    if (!down || y0 < UI_WORK_Y || y0 >= UI_WORK_Y + UI_WORK_H) {
        p.lx = p.ly = -1000;
        return;
    }
    int ix = x - IMG_X, iy = y - UI_WORK_Y - IMG_Y;
    if (ix < 0 || iy < 0 || ix >= SIDE || iy >= SIDE) {
        return;
    }
    if (abs(x - p.lx) + abs(y - p.ly) < DROP_PX) {
        return;
    }
    p.lx = x;
    p.ly = y;
    petri_task_drop((float)ix / SCALE, (float)iy / SCALE);
}
