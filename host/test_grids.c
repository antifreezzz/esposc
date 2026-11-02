// Генератор Grids: карта, смешивание узлов, плотность, акцент, возмущение, превью.
#include "pl_grids.h"
#include "test.h"

#include <string.h>

// U8Mix из avrlib (avril/op.h): (a * (255 - w) + b * w) >> 8.
static uint8_t u8mix(uint8_t a, uint8_t b, uint8_t w)
{
    return (uint8_t)((a * (255 - w) + b * w) >> 8);
}

static const uint8_t PTS[5][2] = {{0, 0}, {64, 0}, {0, 64}, {128, 192}, {255, 255}};
static const int STEPS[3] = {0, 5, 31};

// Углы: при нулевых весах ReadDrumMap отдаёт двойное смешивание узла с собой, то есть байт узла
// с потерей до 2 единиц (>> 8 в оригинале), а не сам байт.
TEST(test_corner_nodes)
{
    static const struct {
        uint8_t x, y;
        int i, j;
    } C[3] = {{0, 0, 0, 0}, {64, 0, 1, 0}, {0, 64, 0, 1}};
    for (int c = 0; c < 3; c++) {
        for (int p = 0; p < 3; p++) {
            for (int k = 0; k < 3; k++) {
                int s = STEPS[k];
                const uint8_t *a = pl_grids_drum_map[C[c].i][C[c].j];
                uint8_t node = a[p * 32 + s];
                uint8_t lv = pl_grids_level(C[c].x, C[c].y, p, s);
                uint8_t ref = u8mix(u8mix(node, node, 0), u8mix(node, node, 0), 0);
                CHECK(lv == ref);
                CHECK(lv <= node && node - lv <= 2);
            }
        }
    }
}

// Сверка с байтами оригинала (grids/resources.cc, node_10 - угол [0][0]).
TEST(test_original_bytes)
{
    const uint8_t *n10 = pl_grids_drum_map[0][0];
    CHECK(n10[0] == 145);
    CHECK(n10[64] == 255);
    CHECK(n10[32] == 0);
    CHECK(pl_grids_drum_map[0][1][0] == 255);  // node_8
    CHECK(pl_grids_drum_map[0][1][32] == 159);
    CHECK(pl_grids_drum_map[1][0][32] == 36);  // node_15
    CHECK(pl_grids_drum_map[1][0][95] == 85);
    CHECK(pl_grids_drum_map[0][2][64] == 170); // node_0
    uint8_t one = u8mix(145, 145, 0);
    CHECK(pl_grids_level(0, 0, 0, 0) == u8mix(one, one, 0));
    CHECK(pl_grids_level(0, 0, 2, 0) == 253); // 255 -> 254 -> 253
}

TEST(test_mix_midpoint)
{
    // x = 32: вес x << 2 = 128 между узлами [0][0] и [1][0]; y = 0 тоже проходит через U8Mix с весом 0.
    for (int p = 0; p < 3; p++) {
        for (int k = 0; k < 3; k++) {
            int s = STEPS[k];
            uint8_t a = pl_grids_drum_map[0][0][p * 32 + s];
            uint8_t b = pl_grids_drum_map[1][0][p * 32 + s];
            uint8_t ab = u8mix(a, b, 128);
            // Узлы [0][1] и [1][1] в верхней половине по y весят 0: сверка полной формулы.
            uint8_t c = pl_grids_drum_map[0][1][p * 32 + s];
            uint8_t d = pl_grids_drum_map[1][1][p * 32 + s];
            uint8_t ref = u8mix(ab, u8mix(c, d, 128), 0);
            CHECK(pl_grids_level(32, 0, p, s) == ref);
        }
    }
}

TEST(test_density_zero_silent)
{
    const uint8_t d[3] = {0, 0, 0}, z[3] = {0, 0, 0};
    for (int k = 0; k < 5; k++) {
        for (int s = 0; s < PL_GRIDS_STEPS; s++) {
            CHECK(pl_grids_step(PTS[k][0], PTS[k][1], d, z, s) == 0);
        }
    }
}

TEST(test_density_full)
{
    const uint8_t d[3] = {255, 255, 255}, z[3] = {0, 0, 0};
    for (int k = 0; k < 5; k++) {
        for (int s = 0; s < PL_GRIDS_STEPS; s++) {
            uint8_t m = pl_grids_step(PTS[k][0], PTS[k][1], d, z, s);
            for (int p = 0; p < 3; p++) {
                CHECK(((m >> p) & 1) == (pl_grids_level(PTS[k][0], PTS[k][1], p, s) > 0));
            }
        }
    }
}

TEST(test_accent)
{
    const uint8_t z[3] = {0, 0, 0};
    const uint8_t dens[2][3] = {{255, 255, 255}, {100, 150, 200}};
    int accents = 0;
    for (int q = 0; q < 2; q++) {
        for (int k = 0; k < 5; k++) {
            for (int s = 0; s < PL_GRIDS_STEPS; s++) {
                uint8_t m = pl_grids_step(PTS[k][0], PTS[k][1], dens[q], z, s);
                for (int p = 0; p < 3; p++) {
                    int lv = pl_grids_level(PTS[k][0], PTS[k][1], p, s);
                    int hit = lv > 255 - dens[q][p];
                    CHECK(((m >> p) & 1) == hit);
                    CHECK(((m >> (3 + p)) & 1) == (hit && lv > PL_GRIDS_ACCENT));
                    accents += (m >> (3 + p)) & 1;
                }
            }
        }
    }
    CHECK(accents > 0);
}

TEST(test_perturb_clamps)
{
    // Ищем на карте шаг с уровнем 156..254: возмущение 100 поднимает его ровно до 255.
    int found = 0;
    for (int k = 0; k < 5 && !found; k++) {
        for (int s = 0; s < PL_GRIDS_STEPS && !found; s++) {
            for (int p = 0; p < 3 && !found; p++) {
                int lv = pl_grids_level(PTS[k][0], PTS[k][1], p, s);
                if (lv < 156 || lv > 254) {
                    continue;
                }
                found = 1;
                uint8_t d[3] = {1, 1, 1}, per[3] = {100, 100, 100};
                uint8_t m = pl_grids_step(PTS[k][0], PTS[k][1], d, per, s);
                CHECK((m >> p) & 1);              // уровень 255 > 254 при плотности 1
                CHECK((m >> (3 + p)) & 1);        // и это акцент
                uint8_t d0[3] = {0, 0, 0};
                m = pl_grids_step(PTS[k][0], PTS[k][1], d0, per, s);
                CHECK(((m >> p) & 1) == 0);       // плотность 0: порог 255, не бьёт даже 255
            }
        }
    }
    CHECK(found);
    // Нулевой уровень + возмущение 100 = 100: бьёт при плотности выше 155.
    int zero_found = 0;
    for (int s = 0; s < PL_GRIDS_STEPS && !zero_found; s++) {
        if (pl_grids_level(0, 0, 1, s) == 0) {
            zero_found = 1;
            uint8_t per[3] = {0, 100, 0}, hi[3] = {0, 156, 0}, lo[3] = {0, 155, 0};
            CHECK((pl_grids_step(0, 0, hi, per, s) >> 1) & 1);
            CHECK(((pl_grids_step(0, 0, lo, per, s) >> 1) & 1) == 0);
            CHECK(((pl_grids_step(0, 0, hi, per, s) >> 4) & 1) == 0); // 100 - не акцент
        }
    }
    CHECK(zero_found);
}

TEST(test_u8)
{
    CHECK(pl_grids_u8(0) == 0);
    CHECK(pl_grids_u8(64) == 129);
    CHECK(pl_grids_u8(127) == 255);
}

TEST(test_preview_matches_step)
{
    uint8_t knob[16];
    memset(knob, 0, sizeof knob);
    const uint8_t xs[3] = {0, 64, 127}, ys[3] = {0, 33, 127};
    for (int t = 0; t < 3; t++) {
        knob[2] = xs[t];
        knob[3] = ys[t];
        knob[4] = (uint8_t)(40 + 30 * t);
        knob[5] = (uint8_t)(70 + 20 * t);
        knob[6] = (uint8_t)(100 + 10 * t);
        uint8_t out[PL_GRIDS_STEPS];
        pl_grids_preview(knob, out);
        const uint8_t d[3] = {pl_grids_u8(knob[4]), pl_grids_u8(knob[5]), pl_grids_u8(knob[6])};
        const uint8_t z[3] = {0, 0, 0};
        int any = 0;
        for (int s = 0; s < PL_GRIDS_STEPS; s++) {
            CHECK(out[s] == pl_grids_step(pl_grids_u8(knob[2]), pl_grids_u8(knob[3]), d, z, s));
            any |= out[s];
        }
        CHECK(any != 0);
    }
}

int main(void)
{
    RUN(test_corner_nodes);
    RUN(test_original_bytes);
    RUN(test_mix_midpoint);
    RUN(test_density_zero_silent);
    RUN(test_density_full);
    RUN(test_accent);
    RUN(test_perturb_clamps);
    RUN(test_u8);
    RUN(test_preview_matches_step);
    TEST_MAIN_END();
}
