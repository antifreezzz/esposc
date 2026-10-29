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
    for (int k = 62; k <= 66; k++) {
        CHECK(acid_djf_cutoff_hz(k) == 0.0f);
    }
    CHECK_NEAR(acid_djf_cutoff_hz(61), 18000.0f, 1500.0f);
    CHECK_NEAR(acid_djf_cutoff_hz(0), 150.0f, 15.0f);
    CHECK_NEAR(acid_djf_cutoff_hz(67), 20.0f, 3.0f);
    CHECK_NEAR(acid_djf_cutoff_hz(127), 3000.0f, 300.0f);
    int up = 1;
    for (int k = 1; k <= 61; k++) {
        up &= acid_djf_cutoff_hz(k) > acid_djf_cutoff_hz(k - 1);
    }
    for (int k = 68; k <= 127; k++) {
        up &= acid_djf_cutoff_hz(k) > acid_djf_cutoff_hz(k - 1);
    }
    CHECK(up);
}

TEST(test_djf_q_ramp)
{
    CHECK_NEAR(acid_djf_q(61, 127), 0.7f, 0.01f);
    CHECK_NEAR(acid_djf_q(49, 127), 8.0f, 0.1f);
    CHECK_NEAR(acid_djf_q(0, 127), 8.0f, 0.1f);
    CHECK_NEAR(acid_djf_q(0, 0), 0.7f, 0.01f);
    CHECK_NEAR(acid_djf_q(67, 127), 0.7f, 0.01f);
    CHECK_NEAR(acid_djf_q(79, 127), 8.0f, 0.1f);
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
    TEST_MAIN_END();
}
