#include "petri_color.h"

// Стиль «Б. Тёплый аналог»: от фона экрана через бурый и янтарь к кремовому (docs/design/panel-mockups.html).
#define NSTOPS 4
static const struct {
    float t;
    uint8_t c[3];
} STOPS[NSTOPS] = {{0, {0x13, 0x11, 0x0e}}, {1 / 3.0f, {0x4a, 0x2a, 0x14}}, {2 / 3.0f, {0xe0, 0x8a, 0x3a}},
                   {1, {0xf6, 0xe7, 0xc6}}};

void petri_palette(uint16_t lut[256])
{
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f;
        int j = 0;
        while (j < NSTOPS - 2 && t > STOPS[j + 1].t) {
            j++;
        }
        float f = (t - STOPS[j].t) / (STOPS[j + 1].t - STOPS[j].t);
        int c[3];
        for (int k = 0; k < 3; k++) {
            c[k] = (int)(STOPS[j].c[k] + (STOPS[j + 1].c[k] - STOPS[j].c[k]) * f + 0.5f);
        }
        lut[i] = PETRI_RGB565(c[0], c[1], c[2]);
    }
}

static int clamp8(int x)
{
    return x < 0 ? 0 : x > 255 ? 255 : x;
}

uint16_t petri_cell_color(const uint16_t lut[256], int v, int dk)
{
    int k = (int)((int64_t)v * 3 * 255 / PETRI_Q + (int64_t)v * 255 / (10 * PETRI_Q)); // V x3.1
    uint16_t c = lut[k > 255 ? 255 : k < 0 ? 0 : k];
    if (!dk) {
        return c;
    }
    // След: Δk * 3000 в цвете прототипа, Δk = dk * 0.012/127 -> ~0.28 на шаг dk.
    int m = dk * 2835 / 10000;
    int r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
    r = clamp8(r + (m > 0 ? m : 0));
    b = clamp8(b + (m < 0 ? -m : 0));
    g = clamp8(g - (m < 0 ? -m : m) * 3 / 10);
    return PETRI_RGB565(r, g, b);
}
