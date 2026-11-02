// Барабанщик: конечности, режимы хэта и времени, сбивки, крэш.
#include "acid_drummer.h"
#include "pl_grids.h"
#include "test.h"

#include <string.h>

static acid_drummer_t d;
static acid_hit_t h[ACID_DRUMMER_MAX];

static int has(const acid_hit_t *x, int n, int note)
{
    for (int i = 0; i < n; i++) {
        if (x[i].note == note) {
            return 1;
        }
    }
    return 0;
}

// Руки - всё, кроме ног: бочка 36 и педаль 44.
static int hands(const acid_hit_t *x, int n)
{
    int c = 0;
    for (int i = 0; i < n; i++) {
        c += x[i].note != 36 && x[i].note != 44;
    }
    return c;
}

// Ручки в умолчания плюс вкл, точка карты, плотности; сбивки выключены.
static void setup(int kx, int ky, int dk, int ds, int dh)
{
    acid_drummer_init(&d);
    acid_drummer_set(&d, DRM_ON, 1);
    acid_drummer_set(&d, DRM_X, kx);
    acid_drummer_set(&d, DRM_Y, ky);
    acid_drummer_set(&d, DRM_KICK, dk);
    acid_drummer_set(&d, DRM_SNARE, ds);
    acid_drummer_set(&d, DRM_HAT, dh);
    acid_drummer_set(&d, DRM_FILL_EVERY, 0);
}

// Точка карты, где дорожка хэта бьёт (уровень > 0) на всех шагах g0..g1 паттерна из 32.
static int find_hat_point(int g0, int g1, int *kx, int *ky)
{
    for (int x = 0; x < 128; x++) {
        for (int y = 0; y < 128; y++) {
            int ok = 1;
            for (int s = g0; s <= g1 && ok; s++) {
                ok = pl_grids_level(pl_grids_u8(x), pl_grids_u8(y), 2, s) > 0;
            }
            if (ok) {
                *kx = x;
                *ky = y;
                return 1;
            }
        }
    }
    return 0;
}

TEST(test_off_silent)
{
    acid_drummer_init(&d);
    acid_drummer_set(&d, DRM_KICK, 127);
    acid_drummer_set(&d, DRM_SNARE, 127);
    acid_drummer_set(&d, DRM_HAT, 127);
    acid_drummer_set(&d, DRM_FILL_EVERY, 1);
    CHECK(d.knob[DRM_ON] == 0);
    for (int i = 0; i < 64; i++) {
        if (i == 5) {
            acid_drummer_set(&d, DRM_FILL_NOW, 1);
        }
        CHECK(acid_drummer_step(&d, i % 16, (uint32_t)(i / 16), h) == 0);
    }
    CHECK(d.fill_pending == 0 && d.crash_next == 0 && d.fill_from == d.fill_to);
}

TEST(test_limbs)
{
    uint32_t r = 12345;
    for (int c = 0; c < 50; c++) {
        r = r * 1664525u + 1013904223u;
        int x = (int)(r >> 24) & 127;
        r = r * 1664525u + 1013904223u;
        int y = (int)(r >> 24) & 127;
        r = r * 1664525u + 1013904223u;
        setup(x, y, (int)(r >> 24) & 127, (int)(r >> 16) & 127, (int)(r >> 8) & 127);
        r = r * 1664525u + 1013904223u;
        acid_drummer_set(&d, DRM_CHAOS, (int)(r >> 24) & 127);
        acid_drummer_set(&d, DRM_HAT_MODE, (int)(r >> 20) % 3);
        acid_drummer_set(&d, DRM_TIME, (int)(r >> 18) & 1);
        acid_drummer_set(&d, DRM_FILL_EVERY, 1 + (int)(r >> 16) % 3);
        acid_drummer_set(&d, DRM_FILL_LEN, (int)(r >> 14) % 3);
        int press = (int)(r >> 8) % 32;
        for (int i = 0; i < 32; i++) {
            if (i == press) {
                acid_drummer_set(&d, DRM_FILL_NOW, 1);
            }
            int n = acid_drummer_step(&d, i % 16, (uint32_t)(i / 16), h);
            CHECK(n >= 0 && n <= ACID_DRUMMER_MAX);
            CHECK(hands(h, n) <= 2);
            CHECK(!((has(h, n, 42) || has(h, n, 46)) && (has(h, n, 51) || has(h, n, 53))));
        }
    }
    // Сбивка каждые 4 такта на 4 долях, крэш после неё: пройти 5 тактов с максимальными плотностями.
    setup(64, 64, 127, 127, 127);
    acid_drummer_set(&d, DRM_FILL_EVERY, 1);
    acid_drummer_set(&d, DRM_FILL_LEN, 2);
    for (int i = 0; i < 80; i++) {
        int n = acid_drummer_step(&d, i % 16, (uint32_t)(i / 16), h);
        CHECK(n <= ACID_DRUMMER_MAX && hands(h, n) <= 2);
    }
}

TEST(test_hat_modes)
{
    setup(64, 64, 0, 0, 127);
    uint8_t xs = pl_grids_u8(64);
    const uint8_t dens[3] = {0, 0, 255}, none[3] = {0, 0, 0};
    int accents = 0, plain = 0;
    for (int mode = 0; mode < 3; mode++) {
        setup(64, 64, 0, 0, 127);
        acid_drummer_set(&d, DRM_HAT_MODE, mode);
        for (int i = 0; i < 32; i++) {
            uint8_t m = pl_grids_step(xs, xs, dens, none, i);
            int hit = (m >> 2) & 1, acc = (m >> 5) & 1;
            int n = acid_drummer_step(&d, i % 16, (uint32_t)(i / 16), h);
            if (mode == DRM_HAT_CLOSED) {
                CHECK(!has(h, n, 46) && has(h, n, 42) == hit);
                accents += acc;
                plain += hit && !acc;
            } else if (mode == DRM_HAT_OPEN) {
                CHECK(!has(h, n, 42) && has(h, n, 46) == hit);
            } else {
                CHECK(has(h, n, 46) == acc);
                CHECK(has(h, n, 42) == (hit && !acc));
            }
            for (int k = 0; k < n; k++) {
                if (h[k].note == 42 || h[k].note == 46) {
                    CHECK(h[k].vel >= (acc ? 112 : 82) && h[k].vel <= (acc ? 128 : 98));
                }
            }
        }
    }
    CHECK(accents > 0 && plain > 0); // точка карты годится: есть и акценты, и обычные удары
}

TEST(test_ride)
{
    setup(64, 64, 0, 0, 127);
    acid_drummer_set(&d, DRM_TIME, DRM_TIME_RIDE);
    uint8_t xs = pl_grids_u8(64);
    const uint8_t dens[3] = {0, 0, 255}, none[3] = {0, 0, 0};
    int rides = 0;
    for (int i = 0; i < 32; i++) {
        uint8_t m = pl_grids_step(xs, xs, dens, none, i);
        int hit = (m >> 2) & 1, acc = (m >> 5) & 1;
        int n = acid_drummer_step(&d, i % 16, (uint32_t)(i / 16), h);
        CHECK(!has(h, n, 42) && !has(h, n, 46));
        CHECK(has(h, n, 51) == (hit && !acc));
        CHECK(has(h, n, 53) == acc);
        CHECK(has(h, n, 44) == ((i % 16) == 4 || (i % 16) == 12));
        rides += has(h, n, 51) + has(h, n, 53);
    }
    CHECK(rides > 0);
}

TEST(test_auto_fill)
{
    int kx = 64, ky = 64;
    CHECK(find_hat_point(28, 31, &kx, &ky)); // такт 3 - вторая половина паттерна, шаги 28..31
    setup(kx, ky, 127, 127, 127);
    acid_drummer_set(&d, DRM_FILL_EVERY, 1); // каждые 4 такта
    acid_drummer_set(&d, DRM_FILL_LEN, 0);   // 1 доля
    static const int TOMS[4] = {50, 48, 45, 43}; // индекс 6 * k / 4: 0, 1, 3, 4
    int hat_before = 0;
    for (int bar = 0; bar < 5; bar++) {
        for (int s = 0; s < 16; s++) {
            int n = acid_drummer_step(&d, s, (uint32_t)bar, h);
            if (bar == 3 && s < 12) {
                hat_before += has(h, n, 42);
            }
            if (bar == 3 && s >= 12) {
                CHECK(d.fill_from == 12 && d.fill_to == 16);
                CHECK(!has(h, n, 42) && !has(h, n, 46));
                CHECK(has(h, n, TOMS[s - 12]));
                CHECK(hands(h, n) <= 2);
            }
            if (bar == 4 && s == 0) {
                CHECK(has(h, n, 49) && has(h, n, 36));
                CHECK(!has(h, n, 42) && !has(h, n, 46));
            }
            if (bar < 3) {
                CHECK(d.fill_from == d.fill_to);
            }
            if (bar == 4 && s == 1) {
                CHECK(d.crash_next == 0);
            }
        }
    }
    CHECK(hat_before > 0); // вне сбивки время играет
}

TEST(test_fill_now)
{
    int kx = 64, ky = 64, toms = 0;
    // Нажали на шаге 5: сбивка с ближайшей доли, 8..15.
    setup(kx, ky, 127, 127, 127);
    for (int s = 0; s < 16; s++) {
        int n = acid_drummer_step(&d, s, 0, h);
        if (s == 5) {
            CHECK(d.fill_from == d.fill_to);
            acid_drummer_set(&d, DRM_FILL_NOW, 1);
        }
        if (s >= 8) {
            CHECK(d.fill_from == 8 && d.fill_to == 16);
            CHECK(!has(h, n, 42) && !has(h, n, 46));
            int tom = has(h, n, 50) || has(h, n, 48) || has(h, n, 47) || has(h, n, 45) || has(h, n, 43)
                      || has(h, n, 41);
            // Том там, где дорожка хэта бьёт при плотности +128 (уровень > 0).
            CHECK(tom == (pl_grids_level(pl_grids_u8(kx), pl_grids_u8(ky), 2, s) > 0));
            toms += tom;
        }
        if (s == 9) { // повторное нажатие внутри сбивки ничего не меняет
            acid_drummer_set(&d, DRM_FILL_NOW, 0);
            acid_drummer_set(&d, DRM_FILL_NOW, 1);
            CHECK(d.fill_pending == 0);
            CHECK(d.fill_from == 8 && d.fill_to == 16);
        }
    }
    CHECK(toms > 0);
    for (int s = 0; s < 16; s++) {
        int n = acid_drummer_step(&d, s, 1, h);
        CHECK(d.fill_from == d.fill_to);
        if (s == 0) {
            CHECK(has(h, n, 49) && has(h, n, 36));
        }
    }

    // Нажали на последней доле (шаг 13): сбивка начинается в следующем такте с шага 0, на весь такт.
    setup(kx, ky, 127, 127, 127);
    for (int s = 0; s < 16; s++) {
        acid_drummer_step(&d, s, 0, h);
        CHECK(d.fill_from == d.fill_to);
        if (s == 13) {
            acid_drummer_set(&d, DRM_FILL_NOW, 1);
            CHECK(d.fill_pending == 1);
        }
    }
    CHECK(d.fill_pending == 1);
    for (int s = 0; s < 16; s++) {
        int n = acid_drummer_step(&d, s, 1, h);
        CHECK(d.fill_from == 0 && d.fill_to == 16);
        CHECK(d.fill_pending == 0);
        CHECK(!has(h, n, 42) && !has(h, n, 46));
        CHECK(hands(h, n) <= 2);
    }
    for (int s = 0; s < 16; s++) { // не дублируется: в такте 2 крэш и никакой новой сбивки
        int n = acid_drummer_step(&d, s, 2, h);
        CHECK(d.fill_from == d.fill_to);
        if (s == 0) {
            CHECK(has(h, n, 49) && has(h, n, 36));
        }
    }
}

TEST(test_fill_now_needs_release)
{
    setup(64, 64, 127, 127, 127);
    for (int s = 0; s < 4; s++) {
        acid_drummer_step(&d, s, 0, h);
    }
    acid_drummer_set(&d, DRM_FILL_NOW, 1);
    CHECK(d.fill_pending == 1);
    for (int s = 4; s < 16; s++) {
        acid_drummer_step(&d, s, 0, h);
    }
    CHECK(d.fill_from == 4 && d.fill_to == 16);
    for (int s = 0; s < 16; s++) {
        acid_drummer_step(&d, s, 1, h);
        if (s == 0) {
            acid_drummer_set(&d, DRM_FILL_NOW, 1); // 1 -> 1: не отпускали, второй сбивки нет
            CHECK(d.fill_pending == 0);
        }
        CHECK(d.fill_from == d.fill_to);
    }
    for (int s = 0; s < 16; s++) {
        acid_drummer_step(&d, s, 2, h);
        if (s == 1) {
            acid_drummer_set(&d, DRM_FILL_NOW, 0);
            acid_drummer_set(&d, DRM_FILL_NOW, 1); // 1 -> 0 -> 1: запускается
            CHECK(d.fill_pending == 1);
        }
        if (s >= 4) {
            CHECK(d.fill_from == 4 && d.fill_to == 16);
        }
    }
}

TEST(test_list_ignore)
{
    acid_drummer_init(&d);
    CHECK(acid_drummer_set(&d, DRM_HAT_MODE, 1) == 1);
    CHECK(acid_drummer_set(&d, DRM_HAT_MODE, 3) == 0 && d.knob[DRM_HAT_MODE] == 1);
    CHECK(acid_drummer_set(&d, DRM_TIME, 1) == 1);
    CHECK(acid_drummer_set(&d, DRM_TIME, 2) == 0 && d.knob[DRM_TIME] == 1);
    CHECK(acid_drummer_set(&d, DRM_FILL_EVERY, 3) == 1);
    CHECK(acid_drummer_set(&d, DRM_FILL_EVERY, 4) == 0 && d.knob[DRM_FILL_EVERY] == 3);
    CHECK(acid_drummer_set(&d, DRM_FILL_LEN, 2) == 1);
    CHECK(acid_drummer_set(&d, DRM_FILL_LEN, 3) == 0 && d.knob[DRM_FILL_LEN] == 2);
    CHECK(acid_drummer_set(&d, 0, 5) == 0);
    CHECK(acid_drummer_set(&d, 13, 5) == 0);
}

TEST(test_defaults)
{
    acid_drummer_init(&d);
    CHECK(d.knob[DRM_ON] == 0 && d.knob[DRM_X] == 64 && d.knob[DRM_Y] == 64);
    CHECK(d.knob[DRM_KICK] == 80 && d.knob[DRM_SNARE] == 70 && d.knob[DRM_HAT] == 90);
    CHECK(d.knob[DRM_CHAOS] == 0 && d.knob[DRM_HAT_MODE] == 0 && d.knob[DRM_TIME] == 0);
    CHECK(d.knob[DRM_FILL_EVERY] == 2 && d.knob[DRM_FILL_LEN] == 0 && d.knob[DRM_FILL_NOW] == 0);
}

TEST(test_deterministic)
{
    acid_hit_t a[64][ACID_DRUMMER_MAX], b[64][ACID_DRUMMER_MAX];
    int na[64], nb[64];
    for (int run = 0; run < 2; run++) {
        setup(40, 90, 100, 90, 110);
        acid_drummer_set(&d, DRM_CHAOS, 127);
        for (int i = 0; i < 64; i++) {
            if (run == 0) {
                na[i] = acid_drummer_step(&d, i % 16, (uint32_t)(i / 16), a[i]);
            } else {
                nb[i] = acid_drummer_step(&d, i % 16, (uint32_t)(i / 16), b[i]);
            }
        }
    }
    CHECK(memcmp(na, nb, sizeof na) == 0);
    for (int i = 0; i < 64; i++) {
        CHECK(memcmp(a[i], b[i], sizeof(acid_hit_t) * (size_t)na[i]) == 0);
    }
}

int main(void)
{
    RUN(test_off_silent);
    RUN(test_limbs);
    RUN(test_hat_modes);
    RUN(test_ride);
    RUN(test_auto_fill);
    RUN(test_fill_now);
    RUN(test_fill_now_needs_release);
    RUN(test_list_ignore);
    RUN(test_defaults);
    RUN(test_deterministic);
    TEST_MAIN_END();
}
