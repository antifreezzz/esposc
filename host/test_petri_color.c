// Палитра экрана чашки: 4 опорные точки стиля «Б. Тёплый аналог», след мутагена поверх, RGB565.
#include "petri_color.h"
#include "test.h"

static int r5(uint16_t c) { return c >> 11; }
static int g6(uint16_t c) { return (c >> 5) & 63; }
static int b5(uint16_t c) { return c & 31; }

TEST(palette_ends)
{
    static uint16_t lut[256];
    petri_palette(lut);
    CHECK(lut[0] == PETRI_RGB565(0x13, 0x11, 0x0e)); // фон экрана
    CHECK(lut[85] == PETRI_RGB565(0x4a, 0x2a, 0x14));
    CHECK(lut[170] == PETRI_RGB565(0xe0, 0x8a, 0x3a));
    CHECK(lut[255] == PETRI_RGB565(0xf6, 0xe7, 0xc6));
    CHECK(r5(lut[204]) > r5(lut[100])); // к янтарю краснее
}

TEST(cell_color)
{
    static uint16_t lut[256];
    petri_palette(lut);
    CHECK(petri_cell_color(lut, 0, 0) == lut[0]);
    CHECK(petri_cell_color(lut, PETRI_Q, 0) == lut[255]); // V x3.1 - насыщение
    uint16_t plain = petri_cell_color(lut, PETRI_Q / 10, 0);
    uint16_t red = petri_cell_color(lut, PETRI_Q / 10, 100), blue = petri_cell_color(lut, PETRI_Q / 10, -100);
    CHECK(r5(red) > r5(plain) && b5(red) <= b5(plain));
    CHECK(b5(blue) > b5(plain) && r5(blue) <= r5(plain));
    CHECK(g6(red) <= g6(plain));
}

int main(void)
{
    RUN(palette_ends);
    RUN(cell_color);
    TEST_MAIN_END();
}
