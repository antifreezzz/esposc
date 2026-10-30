#include "acid_fx.h"
#include "acid_noise.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f
#define MS(x) ((int)((x) * SR / 1000.0f))
#define PI_F 3.14159265f

// --- Пампинг ---

TEST(test_pump_neutral)
{
    acid_pump_t p;
    acid_pump_init(&p, SR);
    acid_pump_hit(&p, 1.0f);
    CHECK(!acid_pump_active(&p));
    int exact = 1;
    for (int i = 0; i < 48000; i++) {
        exact &= acid_pump_next(&p) == 1.0f;
    }
    CHECK(exact);
}

TEST(test_pump_dips_and_recovers)
{
    acid_pump_t p;
    acid_pump_init(&p, SR);
    acid_pump_set_depth(&p, 1.0f);
    acid_pump_set_release(&p, 0.1f);
    for (int i = 0; i < MS(50); i++) { // сглаживание глубины доехало
        acid_pump_next(&p);
    }
    acid_pump_hit(&p, 1.0f);
    float lo = 1.0f;
    for (int i = 0; i < MS(20); i++) {
        float g = acid_pump_next(&p);
        lo = g < lo ? g : lo;
    }
    CHECK(lo < 0.05f);
    float g = 0.0f;
    for (int i = MS(20); i < MS(600); i++) {
        g = acid_pump_next(&p);
    }
    CHECK(g > 0.99f);
}

static void pump_ready(acid_pump_t *p, float tau)
{
    acid_pump_init(p, SR);
    acid_pump_set_depth(p, 1.0f);
    acid_pump_set_release(p, tau);
    for (int i = 0; i < MS(50); i++) {
        acid_pump_next(p);
    }
}

TEST(test_pump_attack_not_instant)
{
    acid_pump_t p;
    pump_ready(&p, 0.1f);
    acid_pump_hit(&p, 1.0f);
    float prev = acid_pump_next(&p);
    CHECK(prev > 0.9f);
    float step = 1.0f - prev;
    for (int i = 0; i < MS(100); i++) {
        float g = acid_pump_next(&p);
        step = fabsf(g - prev) > step ? fabsf(g - prev) : step;
        prev = g;
    }
    CHECK(step < 0.02f);
}

TEST(test_pump_velocity)
{
    acid_pump_t p;
    pump_ready(&p, 0.1f);
    acid_pump_hit(&p, 0.5f);
    float lo = 1.0f;
    for (int i = 0; i < MS(20); i++) {
        float g = acid_pump_next(&p);
        lo = g < lo ? g : lo;
    }
    CHECK_NEAR(lo, 0.5f, 0.05f);
}

TEST(test_pump_soft_hit_keeps_dip)
{
    acid_pump_t a, b;
    pump_ready(&a, 0.2f);
    pump_ready(&b, 0.2f);
    acid_pump_hit(&a, 1.0f);
    acid_pump_hit(&b, 1.0f);
    float before = 0.0f;
    for (int i = 0; i < MS(20); i++) {
        acid_pump_next(&a);
        before = acid_pump_next(&b);
    }
    acid_pump_hit(&b, 0.3f); // тихий удар во время возврата
    float first = acid_pump_next(&b);
    acid_pump_next(&a);
    CHECK(first <= before + 0.001f);
    int ok = 1;
    for (int i = 0; i < MS(20); i++) {
        ok &= acid_pump_next(&b) <= acid_pump_next(&a) + 1e-6f;
    }
    CHECK(ok);
}

TEST(test_pump_depth_off_mid_dip_smooth)
{
    acid_pump_t p;
    pump_ready(&p, 0.2f);
    acid_pump_hit(&p, 1.0f);
    float prev = 0.0f;
    for (int i = 0; i < MS(10); i++) {
        prev = acid_pump_next(&p);
    }
    acid_pump_set_depth(&p, 0.0f);
    float step = 0.0f;
    for (int i = 0; i < MS(100); i++) {
        float g = acid_pump_next(&p);
        step = fabsf(g - prev) > step ? fabsf(g - prev) : step;
        prev = g;
    }
    CHECK(step < 0.02f);
    CHECK(!acid_pump_active(&p));
    CHECK(acid_pump_next(&p) == 1.0f);
}

// Удары при глубине 0 не копятся: включили пампинг - без удара присед не начинается.
TEST(test_pump_no_stale_dip_after_enable)
{
    acid_pump_t p;
    acid_pump_init(&p, SR);
    acid_pump_hit(&p, 1.0f);
    for (int i = 0; i < 48000; i++) {
        acid_pump_next(&p);
    }
    acid_pump_set_depth(&p, 1.0f);
    float lo = 1.0f;
    for (int i = 0; i < MS(200); i++) {
        float g = acid_pump_next(&p);
        lo = g < lo ? g : lo;
    }
    CHECK(lo > 0.999f);
}

// Сняли глубину посреди приседа, через время вернули: старый присед не всплывает.
TEST(test_pump_no_stale_dip_after_reenable)
{
    acid_pump_t p;
    pump_ready(&p, 0.2f);
    acid_pump_hit(&p, 1.0f);
    for (int i = 0; i < MS(10); i++) {
        acid_pump_next(&p);
    }
    acid_pump_set_depth(&p, 0.0f);
    for (int i = 0; i < 48000; i++) {
        acid_pump_next(&p);
    }
    acid_pump_set_depth(&p, 1.0f);
    float lo = 1.0f;
    for (int i = 0; i < MS(200); i++) {
        float g = acid_pump_next(&p);
        lo = g < lo ? g : lo;
    }
    CHECK(lo > 0.999f);
}

// Огибающая после возврата доходит до нуля, а не висит в субнормалах.
TEST(test_pump_envelope_reaches_zero)
{
    acid_pump_t p;
    pump_ready(&p, 0.03f);
    acid_pump_hit(&p, 1.0f);
    for (int i = 0; i < 20 * 48000; i++) {
        acid_pump_next(&p);
    }
    CHECK(p.e == 0.0f);
}

// Атака линейная: дно приседа ровно через 3 мс.
TEST(test_pump_attack_3ms)
{
    acid_pump_t p;
    pump_ready(&p, 0.3f);
    acid_pump_hit(&p, 1.0f);
    float g = 1.0f;
    for (int i = 0; i <= MS(3) + 1; i++) {
        g = acid_pump_next(&p);
    }
    CHECK(g < 0.03f);
}

// Ручка возврата - время почти полного возврата: к T громкость > 95%, к T/2 ещё заметно ниже.
TEST(test_pump_release_is_recovery_time)
{
    acid_pump_t p;
    pump_ready(&p, 0.3f);
    acid_pump_hit(&p, 1.0f);
    float half = 0.0f, full = 0.0f;
    for (int i = 0; i < MS(3) + MS(300); i++) {
        float g = acid_pump_next(&p);
        half = i == MS(3) + MS(150) ? g : half;
        full = g;
    }
    CHECK(half < 0.9f);
    CHECK(full > 0.95f);
}

// --- DJ-фильтр ---

TEST(test_djf_map)
{
    CHECK(acid_djf_cutoff_hz(64) == 0.0f); // обход только ровно на 64
    CHECK_NEAR(acid_djf_cutoff_hz(63), 18000.0f, 1500.0f);
    CHECK_NEAR(acid_djf_cutoff_hz(0), 150.0f, 15.0f);
    CHECK_NEAR(acid_djf_cutoff_hz(65), 20.0f, 3.0f);
    CHECK_NEAR(acid_djf_cutoff_hz(127), 3000.0f, 300.0f);
    int up = 1;
    for (int k = 1; k <= 63; k++) {
        up &= acid_djf_cutoff_hz(k) > acid_djf_cutoff_hz(k - 1);
    }
    for (int k = 66; k <= 127; k++) {
        up &= acid_djf_cutoff_hz(k) > acid_djf_cutoff_hz(k - 1);
    }
    CHECK(up);
}

TEST(test_djf_q_ramp)
{
    CHECK_NEAR(acid_djf_q(63, 127), 0.7f, 0.01f);
    CHECK_NEAR(acid_djf_q(51, 127), 8.0f, 0.1f);
    CHECK_NEAR(acid_djf_q(0, 127), 8.0f, 0.1f);
    CHECK_NEAR(acid_djf_q(0, 0), 0.7f, 0.01f);
    CHECK_NEAR(acid_djf_q(65, 127), 0.7f, 0.01f);
    CHECK_NEAR(acid_djf_q(77, 127), 8.0f, 0.1f);
}

TEST(test_djf_bypass_bitexact)
{
    acid_djf_t f;
    acid_djf_init(&f, SR);
    acid_djf_set(&f, 64, 127);
    CHECK(!acid_djf_active(&f));
    uint32_t rnd = 1;
    int exact = 1;
    for (int i = 0; i < 48000; i++) {
        float x = acid_noise(&rnd);
        exact &= acid_djf_next(&f, x) == x;
    }
    CHECK(exact);
}

// Синус через фильтр 1 с, отношение RMS выхода к входу за последние 0.5 с.
static float sine_gain(acid_djf_t *f, float hz)
{
    double si = 0.0, so = 0.0;
    int n = (int)SR;
    for (int i = 0; i < n; i++) {
        float x = 0.5f * sinf(2.0f * PI_F * hz * (float)i / SR);
        float y = acid_djf_next(f, x);
        if (i >= n / 2) {
            si += (double)x * x;
            so += (double)y * y;
        }
    }
    return (float)sqrt(so / si);
}

static void djf_at(acid_djf_t *f, int knob, int res)
{
    acid_djf_init(f, SR);
    acid_djf_set(f, knob, res);
}

TEST(test_djf_lp_kills_highs)
{
    acid_djf_t f;
    djf_at(&f, 0, 0);
    CHECK(sine_gain(&f, 5000.0f) < 0.03f);
    djf_at(&f, 0, 0);
    CHECK(sine_gain(&f, 50.0f) > 0.6f);
}

TEST(test_djf_hp_kills_lows)
{
    acid_djf_t f;
    djf_at(&f, 127, 0);
    CHECK(sine_gain(&f, 50.0f) < 0.03f);
    djf_at(&f, 127, 0);
    CHECK(sine_gain(&f, 10000.0f) > 0.6f);
}

// Наибольшая разность соседних сэмплов синуса через фильтр на n сэмплах, t - счётчик фазы.
static float sine_jump(acid_djf_t *f, float hz, int *t, int n, float *prev, int *finite)
{
    float j = 0.0f;
    for (int i = 0; i < n; i++, (*t)++) {
        float y = acid_djf_next(f, 0.5f * sinf(2.0f * PI_F * hz * (float)*t / SR));
        *finite &= isfinite(y);
        j = fabsf(y - *prev) > j ? fabsf(y - *prev) : j;
        *prev = y;
    }
    return j;
}

TEST(test_djf_knob_step_no_zipper)
{
    acid_djf_t f;
    djf_at(&f, 30, 64);
    int t = 0, fin = 1;
    float prev = 0.0f;
    sine_jump(&f, 1000.0f, &t, MS(200), &prev, &fin);
    acid_djf_set(&f, 31, 64);
    CHECK(sine_jump(&f, 1000.0f, &t, MS(50), &prev, &fin) < 0.12f);
    CHECK(fin);
}

TEST(test_djf_cross_centre_no_click)
{
    acid_djf_t f;
    djf_at(&f, 58, 127);
    int t = 0, fin = 1;
    float prev = 0.0f;
    sine_jump(&f, 200.0f, &t, MS(200), &prev, &fin);
    acid_djf_set(&f, 70, 127); // BSP пропустил значения через центр
    CHECK(sine_jump(&f, 200.0f, &t, MS(50), &prev, &fin) < 0.03f); // у синуса ~0.013
    CHECK(fin);
    CHECK(f.mode == 2);
}

TEST(test_djf_far_jump_no_click)
{
    acid_djf_t f;
    djf_at(&f, 20, 127); // ФНЧ ~700 Гц и ФВЧ ~600 Гц сдвигают фазу синуса по-разному
    int t = 0, fin = 1;
    float prev = 0.0f;
    sine_jump(&f, 300.0f, &t, MS(200), &prev, &fin);
    acid_djf_set(&f, 110, 127);
    // У самого синуса ~0.024, мгновенное переключение даёт ~0.06.
    CHECK(sine_jump(&f, 300.0f, &t, MS(50), &prev, &fin) < 0.04f);
    CHECK(fin);
}

// --- Хруст ---

TEST(test_crush_off_bitexact)
{
    acid_crush_t c;
    acid_crush_init(&c);
    acid_crush_set(&c, 0, 0);
    CHECK(!acid_crush_active(&c));
    uint32_t rnd = 3;
    int exact = 1;
    for (int i = 0; i < 4800; i++) {
        float x = acid_noise(&rnd);
        exact &= acid_crush_next(&c, x) == x;
    }
    CHECK(exact);
}

TEST(test_crush_3_bits)
{
    acid_crush_t c;
    acid_crush_init(&c);
    acid_crush_set(&c, 127, 0);
    CHECK(acid_crush_active(&c));
    float lv[64];
    int n = 0;
    for (int i = 0; i < 4800; i++) {
        float y = acid_crush_next(&c, 0.9f * sinf(2.0f * PI_F * 100.0f * (float)i / SR));
        int seen = 0;
        for (int k = 0; k < n; k++) {
            seen |= lv[k] == y;
        }
        if (!seen && n < 64) {
            lv[n++] = y;
        }
    }
    CHECK(n <= 9);
    CHECK(n >= 5);
}

TEST(test_crush_rate_holds_32)
{
    acid_crush_t c;
    acid_crush_init(&c);
    acid_crush_set(&c, 0, 127);
    uint32_t rnd = 5;
    float prev = acid_crush_next(&c, acid_noise(&rnd));
    int run = 1, min_run = 1000, changes = 0;
    for (int i = 0; i < 4800; i++) {
        float y = acid_crush_next(&c, acid_noise(&rnd));
        if (y != prev) {
            changes++;
            min_run = changes > 1 && run < min_run ? run : min_run;
            run = 0;
        }
        run++;
        prev = y;
    }
    CHECK(changes > 100);
    CHECK(min_run >= 32);
}

TEST(test_crush_bits_multiple_of_step)
{
    acid_crush_t c;
    acid_crush_init(&c);
    acid_crush_set(&c, 64, 0);
    uint32_t rnd = 9;
    int ok = 1;
    for (int i = 0; i < 4800; i++) {
        float y = acid_crush_next(&c, acid_noise(&rnd));
        float q = y / c.step;
        ok &= fabsf(q - roundf(q)) < 1e-3f;
    }
    CHECK(ok);
    CHECK(c.step > 0.0f && c.step < 0.01f);
}

// --- Дилэй ---

static acid_delay_t dl;
static int16_t dl_buf[2 * ACID_DELAY_LEN]; // буфер большой: не на стеке

#define DL_ZONE(z) (z) // значение ручки времени = доля: 0 1/16 .. 4 3/8

static void delay_ready(int zone, int fb_knob, int tone_knob)
{
    acid_delay_init(&dl, SR, dl_buf);
    acid_delay_set(&dl, DL_ZONE(zone), fb_knob, tone_knob);
    for (int i = 0; i < 48000; i++) { // время доехало до цели
        acid_delay_next(&dl, 0.0f);
    }
}

// Пик |y| и его позиция на отрезке [from, to) от начала, вход - x(i).
TEST(test_delay_quarter_at_120)
{
    delay_ready(3, 0, 127);
    CHECK_NEAR(acid_delay_time_samples(&dl), 24000.0f, 0.5f);
    float pk = 0.0f;
    int at = -1;
    for (int i = 0; i < 30000; i++) {
        float y = acid_delay_next(&dl, i == 0 ? 1.0f : 0.0f);
        if (fabsf(y) > pk) {
            pk = fabsf(y);
            at = i;
        }
    }
    CHECK(at >= 23999 && at <= 24003);
    CHECK(pk > 0.3f);
}

// Пачка синуса 1 кГц: пики первого и второго повтора.
static void burst_peaks(float hz, float *p1, float *p2)
{
    int t = (int)acid_delay_time_samples(&dl);
    *p1 = *p2 = 0.0f;
    for (int i = 0; i < 3 * t; i++) {
        float x = i < 2000 ? 0.5f * sinf(2.0f * PI_F * hz * (float)i / SR) : 0.0f;
        float y = fabsf(acid_delay_next(&dl, x));
        if (i >= t / 2 && i < t + t / 2) {
            *p1 = y > *p1 ? y : *p1;
        } else if (i >= t + t / 2) {
            *p2 = y > *p2 ? y : *p2;
        }
    }
}

TEST(test_delay_feedback_half)
{
    delay_ready(1, 67, 127); // fb 67 * 0.95 / 127 = 0.5
    float p1, p2;
    burst_peaks(1000.0f, &p1, &p2);
    CHECK_NEAR(p1, 0.5f, 0.05f);
    CHECK_NEAR(p2 / p1, 0.5f, 0.05f);
}

TEST(test_delay_tone_darkens)
{
    delay_ready(0, 0, 0); // ФНЧ 500 Гц
    float p1, p2;
    burst_peaks(5000.0f, &p1, &p2);
    CHECK(p1 > 0.01f);
    CHECK(p1 < 0.1f); // -14 дБ от пачки 0.5, однополюсный ФНЧ
    CHECK(p2 == 0.0f);
}

TEST(test_delay_time_change_smooth)
{
    delay_ready(1, 0, 127);
    float prev = 0.0f, own = 0.0f, j = 0.0f;
    int t = 0;
    for (; t < 48000; t++) {
        float y = acid_delay_next(&dl, 0.5f * sinf(2.0f * PI_F * 200.0f * (float)t / SR));
        own = t > 24000 && fabsf(y - prev) > own ? fabsf(y - prev) : own;
        prev = y;
    }
    acid_delay_set(&dl, DL_ZONE(3), 0, 127); // 1/8 -> 1/4
    for (int i = 0; i < 48000; i++, t++) {
        float y = acid_delay_next(&dl, 0.5f * sinf(2.0f * PI_F * 200.0f * (float)t / SR));
        j = fabsf(y - prev) > j ? fabsf(y - prev) : j;
        prev = y;
    }
    CHECK(own > 0.01f);
    CHECK(j < 3.0f * own);
}

TEST(test_delay_long_time_fits)
{
    delay_ready(4, 100, 127);
    acid_delay_set_spb(&dl, 48000.0f); // 60 BPM: 3/8 = 72000 сэмплов
    CHECK(acid_delay_time_samples(&dl) <= ACID_DELAY_LEN - 2);
    CHECK(acid_delay_time_samples(&dl) >= ACID_DELAY_LEN / 4);
    uint32_t rnd = 11;
    int fin = 1;
    for (int i = 0; i < 4 * 48000; i++) {
        fin &= isfinite(acid_delay_next(&dl, i < 48000 ? 0.5f * acid_noise(&rnd) : 0.0f));
    }
    CHECK(fin);
}

TEST(test_delay_no_stale_tail)
{
    delay_ready(1, 120, 127); // fb 0.9
    CHECK(!acid_delay_active(&dl));
    uint32_t rnd = 13;
    for (int i = 0; i < MS(200); i++) {
        acid_delay_next(&dl, 0.5f * acid_noise(&rnd));
    }
    CHECK(acid_delay_active(&dl));
    int n = 0;
    while (acid_delay_active(&dl) && n < 60 * 48000) {
        acid_delay_next(&dl, 0.0f);
        n++;
    }
    CHECK(!acid_delay_active(&dl));
    int zero = 1;
    for (int i = 0; i < ACID_DELAY_LEN + 100; i++) {
        zero &= acid_delay_next(&dl, 0.0f) == 0.0f;
    }
    CHECK(zero);
}

// --- Статтер ---

static acid_stutter_t st;
static int16_t st_buf[2 * ACID_STUTTER_LEN];

#define ST_ZONE(z) ((z) + 1) // значение ручки: 1 1/4, 2 1/8, 3 1/16, 4 1/32

static float yb[48000];

TEST(test_stutter_off_bitexact)
{
    acid_stutter_init(&st, SR, st_buf);
    acid_stutter_set(&st, 0);
    CHECK(!acid_stutter_active(&st));
    uint32_t rnd = 17;
    int exact = 1;
    for (int i = 0; i < 4800; i++) {
        float x = acid_noise(&rnd);
        exact &= acid_stutter_next(&st, x) == x;
    }
    CHECK(exact);
}

// Выход периодичен с периодом n на [from, to).
static int periodic(int n, int from, int to)
{
    int ok = 1;
    for (int i = from; i < to; i++) {
        ok &= yb[i] == yb[i - n];
    }
    return ok;
}

TEST(test_stutter_repeats_sixteenth)
{
    acid_stutter_init(&st, SR, st_buf);
    acid_stutter_set(&st, ST_ZONE(2)); // 1/16 при 120 BPM = 6000
    CHECK(acid_stutter_active(&st));
    for (int i = 0; i < 48000; i++) {
        yb[i] = acid_stutter_next(&st, (float)i / 100000.0f); // нарастающая пила: без петли не повторяется
    }
    CHECK(periodic(6000, 12000, 48000));
    CHECK(yb[1000] == 1000.0f / 100000.0f); // во время записи выход - вход
}

TEST(test_stutter_shorter_while_on)
{
    acid_stutter_init(&st, SR, st_buf);
    acid_stutter_set(&st, ST_ZONE(2));
    for (int i = 0; i < 10000; i++) {
        acid_stutter_next(&st, (float)i / 100000.0f);
    }
    acid_stutter_set(&st, ST_ZONE(3)); // 1/32 = 3000: крутится начало куска
    for (int i = 0; i < 48000; i++) {
        yb[i] = acid_stutter_next(&st, (float)(i + 10000) / 100000.0f);
    }
    CHECK(periodic(3000, 6000, 48000)); // первые 2 мс после смены сводятся со старой головкой
    CHECK(st.len == 3000);
}

TEST(test_stutter_click_free)
{
    // 210 Гц: в петлю 1/16 (6000) не целое число периодов, шов без кроссфейда щёлкнет.
    acid_stutter_init(&st, SR, st_buf);
    float prev = 0.0f, own = 0.0f, j = 0.0f;
    int t = 0;
    for (; t < 4800; t++) {
        float y = acid_stutter_next(&st, 0.5f * sinf(2.0f * PI_F * 210.0f * (float)t / SR));
        own = fabsf(y - prev) > own ? fabsf(y - prev) : own;
        prev = y;
    }
    acid_stutter_set(&st, ST_ZONE(2));
    for (int i = 0; i < MS(200); i++, t++) {
        float y = acid_stutter_next(&st, 0.5f * sinf(2.0f * PI_F * 210.0f * (float)t / SR));
        j = fabsf(y - prev) > j ? fabsf(y - prev) : j;
        prev = y;
    }
    acid_stutter_set(&st, 0);
    for (int i = 0; i < MS(50); i++, t++) {
        float y = acid_stutter_next(&st, 0.5f * sinf(2.0f * PI_F * 210.0f * (float)t / SR));
        j = fabsf(y - prev) > j ? fabsf(y - prev) : j;
        prev = y;
    }
    CHECK(j < own + 0.02f);
    CHECK(!acid_stutter_active(&st));
}

TEST(test_stutter_long_fits)
{
    acid_stutter_init(&st, SR, st_buf);
    acid_stutter_set_spb(&st, 48000.0f); // 60 BPM: 1/4 = 48000
    acid_stutter_set(&st, ST_ZONE(0));
    CHECK(st.len + ACID_STUTTER_SEAM <= ACID_STUTTER_LEN);
    CHECK(st.len >= ACID_STUTTER_LEN / 4);
    int fin = 1;
    for (int i = 0; i < 96000; i++) {
        fin &= isfinite(acid_stutter_next(&st, 0.5f));
    }
    CHECK(fin);
}

// --- Правки по ревью этапа 7 ---

// Медленно докрутили из 1/8 в 1/16, пока шла запись: статтер не должен замолчать.
TEST(test_stutter_slow_dial_into_shorter_zone)
{
    acid_stutter_init(&st, SR, st_buf);
    acid_stutter_set(&st, ST_ZONE(1)); // 1/8 = 12000
    for (int i = 0; i < 7000; i++) {
        acid_stutter_next(&st, (float)i / 100000.0f);
    }
    acid_stutter_set(&st, ST_ZONE(2)); // 1/16 = 6000, записано уже больше
    for (int i = 0; i < 40000; i++) {
        yb[i] = acid_stutter_next(&st, (float)(i + 7000) / 100000.0f);
    }
    CHECK(periodic(6000, 20000, 40000));
}

// Синус через статтер, наибольший скачок соседних сэмплов; set - что сделать на шаге step.
static float stutter_jump(const int *steps, const int *knobs, int nsteps, int total)
{
    acid_stutter_init(&st, SR, st_buf);
    float prev = 0.0f, j = 0.0f;
    int s = 0;
    for (int t = 0; t < total; t++) {
        while (s < nsteps && steps[s] == t) {
            acid_stutter_set(&st, knobs[s++]);
        }
        float y = acid_stutter_next(&st, 0.5f * sinf(2.0f * PI_F * 233.7f * (float)t / SR));
        j = t > 0 && fabsf(y - prev) > j ? fabsf(y - prev) : j;
        prev = y;
    }
    return j;
}

#define OWN_233 (0.5f * 2.0f * PI_F * 233.7f / SR) // наибольший шаг самого синуса

TEST(test_stutter_length_change_click_free)
{
    const int steps[] = {1000, MS(300), MS(500), MS(900)};
    const int knobs[] = {ST_ZONE(2), ST_ZONE(3), ST_ZONE(1), ST_ZONE(2)};
    CHECK(stutter_jump(steps, knobs, 4, MS(1300)) < OWN_233 + 0.02f);
}

TEST(test_stutter_reengage_during_fade)
{
    const int steps[] = {1000, MS(200), MS(200) + 48, MS(500), MS(500)};
    const int knobs[] = {ST_ZONE(2), 0, ST_ZONE(2), 0, ST_ZONE(3)};
    CHECK(stutter_jump(steps, knobs, 5, MS(900)) < OWN_233 + 0.02f);
}

// Значение ручки времени = доля, вне 0..4 - игнор; после включения 3/16.
TEST(test_delay_time_is_screen_value)
{
    acid_delay_init(&dl, SR, dl_buf);
    CHECK(dl.time_knob == 2);
    acid_delay_set(&dl, 1, 0, 127);
    CHECK_NEAR(acid_delay_time_samples(&dl), 12000.0f, 0.5f);
    acid_delay_set(&dl, 5, 0, 127);
    CHECK_NEAR(acid_delay_time_samples(&dl), 12000.0f, 0.5f);
    acid_delay_set(&dl, 127, 0, 127);
    CHECK_NEAR(acid_delay_time_samples(&dl), 12000.0f, 0.5f);
    acid_delay_set(&dl, 4, 0, 127);
    CHECK_NEAR(acid_delay_time_samples(&dl), 36000.0f, 0.5f);
}

// Значение ручки статтера = доля (1..4), вне - игнор.
TEST(test_stutter_is_screen_value)
{
    acid_stutter_init(&st, SR, st_buf);
    acid_stutter_set(&st, 2); // 1/8 = 12000
    CHECK(st.len == 12000);
    acid_stutter_set(&st, 5);
    CHECK(st.knob == 2 && st.len == 12000);
    acid_stutter_set(&st, 127);
    CHECK(st.knob == 2 && st.on);
    acid_stutter_set(&st, 0);
    CHECK(!st.on);
}

// На 120 BPM самые длинные доли влезают целиком.
TEST(test_longest_zones_fit_at_120)
{
    delay_ready(4, 0, 127);
    CHECK_NEAR(acid_delay_time_samples(&dl), 36000.0f, 0.5f);
    acid_stutter_init(&st, SR, st_buf);
    acid_stutter_set(&st, ST_ZONE(0));
    CHECK(st.len == 24000);
}

// Чтение чуть позади точки записи не выходит за буфер: из тишины - тишина.
TEST(test_delay_read_wrap_in_bounds)
{
    delay_ready(1, 0, 127);
    dl.d = dl.d_target = 1000.0005f;
    int zero = 1;
    for (int i = 0; i < 2 * ACID_DELAY_LEN; i++) {
        zero &= acid_delay_next(&dl, 0.0f) == 0.0f;
    }
    CHECK(zero);
}

// Поворот ручки в том же режиме: срез доезжает до цели ровно, коэффициенты снова в кэше.
TEST(test_djf_settles_after_knob_move)
{
    acid_djf_t f;
    djf_at(&f, 10, 0);
    for (int i = 0; i < 4800; i++) {
        acid_djf_next(&f, 0.0f);
    }
    acid_djf_set(&f, 61, 90);
    for (int i = 0; i < 48000; i++) {
        acid_djf_next(&f, 0.0f);
    }
    CHECK(f.g == f.g_target);
    CHECK(f.k == f.k_target);
}

int main(void)
{
    RUN(test_pump_neutral);
    RUN(test_pump_dips_and_recovers);
    RUN(test_pump_attack_not_instant);
    RUN(test_pump_velocity);
    RUN(test_pump_soft_hit_keeps_dip);
    RUN(test_pump_depth_off_mid_dip_smooth);
    RUN(test_pump_no_stale_dip_after_enable);
    RUN(test_pump_no_stale_dip_after_reenable);
    RUN(test_pump_envelope_reaches_zero);
    RUN(test_pump_attack_3ms);
    RUN(test_pump_release_is_recovery_time);
    RUN(test_djf_map);
    RUN(test_djf_q_ramp);
    RUN(test_djf_bypass_bitexact);
    RUN(test_djf_lp_kills_highs);
    RUN(test_djf_hp_kills_lows);
    RUN(test_djf_knob_step_no_zipper);
    RUN(test_djf_cross_centre_no_click);
    RUN(test_djf_far_jump_no_click);
    RUN(test_crush_off_bitexact);
    RUN(test_crush_3_bits);
    RUN(test_crush_rate_holds_32);
    RUN(test_crush_bits_multiple_of_step);
    RUN(test_delay_quarter_at_120);
    RUN(test_delay_feedback_half);
    RUN(test_delay_tone_darkens);
    RUN(test_delay_time_change_smooth);
    RUN(test_delay_long_time_fits);
    RUN(test_delay_no_stale_tail);
    RUN(test_stutter_off_bitexact);
    RUN(test_stutter_repeats_sixteenth);
    RUN(test_stutter_shorter_while_on);
    RUN(test_stutter_click_free);
    RUN(test_stutter_long_fits);
    RUN(test_stutter_slow_dial_into_shorter_zone);
    RUN(test_stutter_length_change_click_free);
    RUN(test_stutter_reengage_during_fade);
    RUN(test_longest_zones_fit_at_120);
    RUN(test_delay_read_wrap_in_bounds);
    RUN(test_djf_settles_after_knob_move);
    RUN(test_delay_time_is_screen_value);
    RUN(test_stutter_is_screen_value);
    TEST_MAIN_END();
}
