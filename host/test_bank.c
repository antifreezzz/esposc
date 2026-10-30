#include "acid_bank.h"
#include "bank_fixture.h"
#include "test.h"

#include <math.h>
#include <string.h>

static uint32_t img_[64 * 1024]; // 256 КБ, выровнено на 4
static uint8_t *const img = (uint8_t *)img_;

static const fixture_note_t NOTES[] = {
    {49, 3, {40, 90, 127}, {2, 2, 2}, 1000},
    {51, 2, {64, 127}, {1, 1}, 700},
};

static size_t build(void)
{
    return fixture_bank(img, sizeof img_, NOTES, 2);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static void recrc(size_t size)
{
    put32(img + 12, acid_crc32(img + ACID_BANK_HDR_SIZE, size - ACID_BANK_HDR_SIZE));
}

TEST(test_bank_opens_fixture)
{
    size_t n = build();
    acid_bank_t b;
    CHECK(n > 0);
    CHECK(acid_bank_open(&b, img, n) == 1);
    CHECK(b.hdr->nnotes == 2 && b.hdr->nlayers == 5 && b.hdr->nrr == 8);
    CHECK(acid_bank_open(&b, img, n + 4096) == 1); // раздел больше образа - норма
}

TEST(test_bank_rejects_magic)
{
    size_t n = build();
    acid_bank_t b;
    img[0] ^= 1;
    CHECK(acid_bank_open(&b, img, n) == 0);
}

TEST(test_bank_rejects_version)
{
    size_t n = build();
    acid_bank_t b;
    put32(img + 4, ACID_BANK_VERSION + 1);
    CHECK(acid_bank_open(&b, img, n) == 0);
}

TEST(test_bank_rejects_crc)
{
    size_t n = build();
    acid_bank_t b;
    img[n - 10] ^= 0x40;
    CHECK(acid_bank_open(&b, img, n) == 0);
}

TEST(test_bank_rejects_truncated)
{
    size_t n = build();
    acid_bank_t b;
    CHECK(acid_bank_open(&b, img, n - 1) == 0);
    CHECK(acid_bank_open(&b, img, 10) == 0); // даже заголовок не целый
}

TEST(test_bank_rejects_rate)
{
    size_t n = build();
    acid_bank_t b;
    put32(img + 16, 44100);
    CHECK(acid_bank_open(&b, img, n) == 0);
}

TEST(test_bank_rejects_offset_out_of_range)
{
    size_t n = build();
    acid_bank_t b;
    // Первый повтор таблицы: после заголовка, 2 нот и 5 слоёв.
    uint8_t *rr = img + ACID_BANK_HDR_SIZE + 2 * 4 + 5 * 4;
    put32(rr, (uint32_t)n - 100); // блоки вылезают за size
    recrc(n);
    CHECK(acid_bank_open(&b, img, n) == 0);
    build();
    put32(rr + 4, 0xFFFFFFF0u); // кадров столько, что размер переполняется
    recrc(n);
    CHECK(acid_bank_open(&b, img, n) == 0);
}

TEST(test_bank_rejects_table_index)
{
    size_t n = build();
    acid_bank_t b;
    uint8_t *note1 = img + ACID_BANK_HDR_SIZE + 4;
    note1[2] = 4; // first_layer 4 + 2 слоя > 5
    recrc(n);
    CHECK(acid_bank_open(&b, img, n) == 0);
    build();
    uint8_t *layer0 = img + ACID_BANK_HDR_SIZE + 2 * 4;
    layer0[1] = 0; // слой без повторов
    recrc(n);
    CHECK(acid_bank_open(&b, img, n) == 0);
}

TEST(test_bank_note_lookup)
{
    size_t n = build();
    acid_bank_t b;
    CHECK(acid_bank_open(&b, img, n) == 1);
    const acid_bank_note_t *c = acid_bank_note(&b, 49);
    CHECK(c != NULL && c->nlayers == 3);
    CHECK(acid_bank_note(&b, 51) != NULL);
    CHECK(acid_bank_note(&b, 50) == NULL);
}

TEST(test_adpcm_roundtrip)
{
    acid_adpcm_t enc = {0, 0}, dec = {0, 0};
    double err = 0.0;
    int n = 4800;
    for (int i = 0; i < n; i++) {
        int16_t x = (int16_t)lrintf(20000.0f * sinf(2.0f * 3.14159265f * 1000.0f * (float)i / 48000.0f));
        int16_t y = acid_adpcm_step(&dec, fixture_adpcm_encode(&enc, x));
        if (i >= 100) { // кодеру нужно разогнаться с нулевого шага
            err += ((double)x - y) * ((double)x - y);
        }
    }
    double rms = sqrt(err / (n - 100)) / 32768.0;
    CHECK(rms < 0.01);
}

TEST(test_adpcm_index_clamped)
{
    acid_adpcm_t st = {0, 0};
    for (int i = 0; i < 200; i++) {
        acid_adpcm_step(&st, 0x7); // всё вверх: pred упирается в 32767
        CHECK(st.index <= 88);
    }
    CHECK(st.pred == 32767);
    for (int i = 0; i < 400; i++) {
        acid_adpcm_step(&st, 0xF);
        CHECK(st.index <= 88);
    }
    CHECK(st.pred == -32768);
    for (int i = 0; i < 100; i++) {
        acid_adpcm_step(&st, 0x0);
    }
    CHECK(st.index == 0);
}

TEST(test_bank_rr_bytes)
{
    CHECK(acid_bank_rr_bytes(1) == 8 + 1);
    CHECK(acid_bank_rr_bytes(256) == 8 + 256);
    CHECK(acid_bank_rr_bytes(257) == 2 * 8 + 257);
}

TEST(test_crc32_matches_zlib)
{
    CHECK(acid_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
}

int main(void)
{
    RUN(test_bank_opens_fixture);
    RUN(test_bank_rejects_magic);
    RUN(test_bank_rejects_version);
    RUN(test_bank_rejects_crc);
    RUN(test_bank_rejects_truncated);
    RUN(test_bank_rejects_rate);
    RUN(test_bank_rejects_offset_out_of_range);
    RUN(test_bank_rejects_table_index);
    RUN(test_bank_note_lookup);
    RUN(test_adpcm_roundtrip);
    RUN(test_adpcm_index_clamped);
    RUN(test_bank_rr_bytes);
    RUN(test_crc32_matches_zlib);
    TEST_MAIN_END();
}
