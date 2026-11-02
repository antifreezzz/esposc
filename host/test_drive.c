// Перегруз синта: чистый саб, обертоны по ручке, свист на гармонике ноты, тишина после ноты, мало алиасинга.
#include "acid_drive.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f
#define N 24000
#define PI_D 3.14159265358979

static acid_drive_t d;
static float out[N];

// Амплитуда частоты f в куске сигнала (Гёрцель).
static double amp_at(const float *x, int n, double f)
{
    double w = 2.0 * PI_D * f / SR, c = 2.0 * cos(w), s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; i++) {
        double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

static double rms(const float *x, int n)
{
    double e = 0.0;
    for (int i = 0; i < n; i++) {
        e += (double)x[i] * x[i];
    }
    return sqrt(e / n);
}

// Синус f0 амплитуды a через перегруз; ручки заданы до вызова.
static void run_sine(double f0, float a, int n)
{
    acid_drive_pitch(&d, (float)f0);
    for (int i = 0; i < n; i++) {
        out[i] = acid_drive_run(&d, a * (float)sin(2.0 * PI_D * f0 * i / SR));
    }
}

// Самый сильный пик в +-5% от частоты f: свист захватывается ближайшей гармоникой ноты.
static double peak_near(double f)
{
    double best = 0.0;
    for (double x = 0.95 * f; x <= 1.05 * f; x += 2.0) {
        double a = amp_at(out + N / 2, N / 2, x);
        best = a > best ? a : best;
    }
    return best;
}

static void setup(int drive, int tone, int howl)
{
    acid_drive_init(&d, SR);
    acid_drive_set(&d, drive, tone, howl);
}

TEST(test_drive_zero_is_off)
{
    setup(0, 64, 0);
    CHECK(!acid_drive_on(&d));
    acid_drive_set(&d, 1, 64, 0);
    CHECK(acid_drive_on(&d));
}

// Саб 50 Гц проходит мимо перегруза: основной тон на месте и не гасится искажённым остатком
// (обертоны выше раздела - это и есть хруст поверх чистого низа).
TEST(test_drive_keeps_sub)
{
    setup(127, 64, 0);
    run_sine(50.0, 0.5f, N);
    double a1 = amp_at(out + N / 2, N / 2, 50.0);
    CHECK(a1 > 0.4 && a1 < 0.75);
}

// Больше ручки - больше обертонов на 220 Гц.
TEST(test_drive_more_knob_more_harmonics)
{
    double thd[3];
    int knob[3] = {10, 50, 127};
    for (int k = 0; k < 3; k++) {
        setup(knob[k], 30, 0);
        run_sine(220.0, 0.5f, N);
        const float *t = out + N / 2;
        double h = 0.0;
        for (int m = 2; m <= 7; m++) {
            h += amp_at(t, N / 2, 220.0 * m);
        }
        thd[k] = h / amp_at(t, N / 2, 220.0);
    }
    CHECK(thd[0] < thd[1]);
    CHECK(thd[1] < thd[2]);
    CHECK(thd[2] > 0.5);
}

// Громкость по ручке не прыгает: от слабого до максимума в пределах +-6 дБ от входа.
TEST(test_drive_level_stays_close)
{
    int knob[3] = {1, 64, 127};
    for (int k = 0; k < 3; k++) {
        setup(knob[k], 64, 0);
        run_sine(220.0, 0.5f, N);
        double r = rms(out + N / 2, N / 2) / (0.5 / sqrt(2.0));
        CHECK(r > 0.5 && r < 2.0);
    }
}

// Выход ограничен и без NaN даже на всём максимуме и громком входе.
TEST(test_drive_bounded)
{
    setup(127, 127, 127);
    run_sine(110.0, 2.0f, N);
    float pk = 0.0f;
    int finite = 1;
    for (int i = 0; i < N; i++) {
        finite &= isfinite(out[i]);
        pk = fabsf(out[i]) > pk ? fabsf(out[i]) : pk;
    }
    CHECK(finite);
    CHECK(pk < 3.5f);
}

// Вой: тихий синус заводит свист на гармонике ноты (тон), и свист едет за нотой.
TEST(test_drive_howl_follows_note)
{
    setup(80, 64, 127);
    float h = d.harm;
    CHECK(h > 2.0f && h < 6.0f);
    run_sine(200.0, 0.05f, N);
    double at1 = peak_near(200.0 * h);
    run_sine(300.0, 0.05f, N);
    double at1_after = peak_near(200.0 * h);
    double at2 = peak_near(300.0 * h);
    CHECK(at1 > 0.1);
    CHECK(at2 > 0.1);
    CHECK(at1_after < 0.3 * at2);

    setup(80, 64, 0); // без воя на той же гармонике почти пусто
    run_sine(200.0, 0.05f, N);
    CHECK(peak_near(200.0 * h) < 0.3 * at1);
}

// Слой на половинной частоте: коэффициенты как у перегруза, заведённого на 24 кГц, состояние (история фильтров,
// ручки) не обнуляется - переход без щелчка. Вой на той же гармонике ноты.
TEST(test_drive_set_rate_keeps_state)
{
    setup(100, 80, 60);
    run_sine(200.0, 0.3f, 4800);
    acid_drive_t fresh;
    acid_drive_init(&fresh, SR / 2);
    acid_drive_set(&fresh, 100, 80, 60);
    acid_drive_pitch(&fresh, 200.0f);
    float ic1 = d.xover.ic1, fb = d.fb, env = d.env;
    acid_drive_set_rate(&d, SR / 2);
    acid_drive_pitch(&d, 200.0f);
    CHECK(d.sr == SR / 2);
    CHECK(fabsf(d.xover.g - fresh.xover.g) < 1e-7f);
    CHECK(fabsf(d.post_hp.a3 - fresh.post_hp.a3) < 1e-7f);
    CHECK(fabsf(d.env_rel - fresh.env_rel) < 1e-7f);
    CHECK(fabsf(d.bp.g - fresh.bp.g) < 1e-7f);
    CHECK(d.drive == 100 && d.tone == 80 && d.howl == 60);
    CHECK(d.xover.ic1 == ic1 && d.fb == fb && d.env == env);
    CHECK(ic1 != 0.0f);
}

// Нота кончилась - вой затихает за ~0.3 с, петля не свистит сама по себе.
TEST(test_drive_howl_dies_after_note)
{
    setup(127, 64, 127);
    run_sine(200.0, 0.3f, N / 2);
    for (int i = 0; i < N; i++) {
        out[i] = acid_drive_run(&d, 0.0f);
    }
    CHECK(rms(out + 3 * N / 4, N / 4) < 1e-4);
}

// 2x оверсемплинг: гармоники 7 кГц выше Найквиста почти не заворачиваются (35 кГц в 13 кГц, 49 кГц в 1 кГц).
// Без него на 13 кГц ~0.2 от основного, с ним ~0.03.
TEST(test_drive_low_alias)
{
    setup(127, 0, 0);
    run_sine(7000.0, 0.5f, N);
    const float *t = out + N / 2;
    double a1 = amp_at(t, N / 2, 7000.0);
    CHECK(a1 > 0.1);
    CHECK(amp_at(t, N / 2, 13000.0) < 0.05 * a1);
    CHECK(amp_at(t, N / 2, 1000.0) < 0.05 * a1);
}

int main(void)
{
    RUN(test_drive_zero_is_off);
    RUN(test_drive_keeps_sub);
    RUN(test_drive_more_knob_more_harmonics);
    RUN(test_drive_level_stays_close);
    RUN(test_drive_bounded);
    RUN(test_drive_howl_follows_note);
    RUN(test_drive_howl_dies_after_note);
    RUN(test_drive_set_rate_keeps_state);
    RUN(test_drive_low_alias);
    TEST_MAIN_END();
}
