// Модуляторы: формы, запуски, вероятность, огибающая, синхронизация, сдвиги целей, blob.
#include <string.h>

#include "acid_mod.h"
#include "test.h"

#define DT (32.0f / 48000.0f) // кусок

static acid_mod_t m;

static void fresh(void)
{
    acid_mod_init(&m, DT);
}

// Шагов до конца одного периода при свободном ходе.
static int run_n(int n, unsigned trig)
{
    for (int i = 0; i < n; i++) {
        acid_mod_run(&m, i == 0 ? trig : 0, 0);
    }
    return n;
}

TEST(test_defaults)
{
    fresh();
    for (int k = 0; k < ACID_MODS; k++) {
        CHECK(acid_mod_knob(&m, k, ACID_MOD_SHAPE) == ACID_SHAPE_SIN);
        CHECK(acid_mod_knob(&m, k, ACID_MOD_PROB) == 127 && acid_mod_knob(&m, k, ACID_MOD_AMOUNT) == 127);
        for (int i = 0; i < ACID_MOD_TARGETS; i++) {
            CHECK(acid_mod_knob(&m, k, ACID_MOD_DEPTH1 + i) == 64);
            CHECK(m.m[k].t[i].page == ACID_MOD_NONE);
        }
    }
    CHECK(!acid_mod_take_save(&m));
}

TEST(test_lists_take_only_their_values)
{
    fresh();
    CHECK(acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_ENV));
    CHECK(!acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPES) && acid_mod_knob(&m, 0, ACID_MOD_SHAPE) == ACID_SHAPE_ENV);
    CHECK(acid_mod_set(&m, 0, ACID_MOD_SYNC, ACID_MOD_SYNCS - 1) && !acid_mod_set(&m, 0, ACID_MOD_SYNC, ACID_MOD_SYNCS));
    CHECK(acid_mod_set(&m, 0, ACID_MOD_TRIG, ACID_MOD_TRIGS - 1) && !acid_mod_set(&m, 0, ACID_MOD_TRIG, ACID_MOD_TRIGS));
    CHECK(acid_mod_set(&m, 0, ACID_MOD_RATE, 127) && !acid_mod_set(&m, 0, ACID_MOD_RATE, 128));
    CHECK(!acid_mod_set(&m, 0, 0, 5) && !acid_mod_set(&m, ACID_MODS, ACID_MOD_RATE, 5) && !acid_mod_set(&m, 0, 16, 5));
    CHECK(acid_mod_take_save(&m) && !acid_mod_take_save(&m));
    CHECK(acid_mod_set(&m, 0, ACID_MOD_RATE, 127) && !acid_mod_take_save(&m)); // то же значение - писать нечего
}

// Свободный LFO: границы, период по ручке RATE, форма.
TEST(test_free_shapes)
{
    for (int shape = ACID_SHAPE_SIN; shape <= ACID_SHAPE_SQR; shape++) {
        fresh();
        acid_mod_set(&m, 0, ACID_MOD_SHAPE, shape);
        acid_mod_set(&m, 0, ACID_MOD_RATE, 127); // 20 Гц: период 75 кусков
        float lo = 2, hi = -2;
        int cross = 0;
        float was = 0;
        for (int i = 0; i < 1500; i++) { // 1 с
            acid_mod_run(&m, 0, 0);
            float o = acid_mod_out(&m, 0);
            lo = o < lo ? o : lo;
            hi = o > hi ? o : hi;
            cross += i > 0 && was < 0 && o >= 0;
            was = o;
        }
        CHECK(lo >= -1.0001f && hi <= 1.0001f && lo < -0.95f && hi > 0.95f);
        CHECK(cross >= 19 && cross <= 21);
    }
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_RATE, 0); // 0.05 Гц: за секунду 1/20 периода, синус едва поднялся
    run_n(1500, 0);
    CHECK_NEAR(acid_mod_out(&m, 0), 0.309f, 0.02f); // sin(2 pi * 0.05)
    // Формы в четверти периода.
    static const float Q[] = {1, 1, 0.5f, -0.5f, 1};
    for (int shape = ACID_SHAPE_SIN; shape <= ACID_SHAPE_SQR; shape++) {
        fresh();
        acid_mod_set(&m, 0, ACID_MOD_SHAPE, shape);
        acid_mod_set(&m, 0, ACID_MOD_RATE, 0);
        m.m[0].phase = 0.25f;
        acid_mod_run(&m, 0, 0);
        CHECK_NEAR(acid_mod_out(&m, 0), Q[shape], 0.01f);
    }
}

// Запуск возвращает LFO к началу периода; чужой запуск и FREE - нет.
TEST(test_trigger_resets_phase)
{
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_RAMP);
    acid_mod_set(&m, 0, ACID_MOD_TRIG, 5); // KICK
    run_n(200, 0);
    CHECK(acid_mod_out(&m, 0) > -0.9f);
    acid_mod_run(&m, ACID_TRIG_SNARE, 0);
    CHECK(acid_mod_out(&m, 0) > -0.9f);
    acid_mod_run(&m, ACID_TRIG_KICK | ACID_TRIG_HAT, 0);
    CHECK_NEAR(acid_mod_out(&m, 0), -1.0f, 0.01f);
    for (int t = 1; t < ACID_MOD_TRIGS; t++) { // значение ручки t слушает бит t - 1
        fresh();
        acid_mod_set(&m, 1, ACID_MOD_SHAPE, ACID_SHAPE_RAMP);
        acid_mod_set(&m, 1, ACID_MOD_TRIG, t);
        run_n(200, 0);
        acid_mod_run(&m, ~(1u << (t - 1)) & 0x1FFF, 0);
        CHECK(acid_mod_out(&m, 1) > -0.9f);
        acid_mod_run(&m, 1u << (t - 1), 0);
        CHECK_NEAR(acid_mod_out(&m, 1), -1.0f, 0.01f);
    }
}

TEST(test_probability)
{
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_ENV);
    acid_mod_set(&m, 0, ACID_MOD_TRIG, 5);
    acid_mod_set(&m, 0, ACID_MOD_PROB, 0);
    for (int i = 0; i < 200; i++) {
        acid_mod_run(&m, ACID_TRIG_KICK, 0);
        CHECK(acid_mod_out(&m, 0) == 0);
    }
    acid_mod_set(&m, 0, ACID_MOD_PROB, 64);
    acid_mod_set(&m, 0, ACID_MOD_DECAY, 0);
    int fired = 0;
    for (int i = 0; i < 1000; i++) {
        acid_mod_run(&m, ACID_TRIG_KICK, 0);
        fired += acid_mod_out(&m, 0) > 0.5f;
        run_n(30, 0); // спад 1 мс: успел затихнуть
    }
    CHECK(fired > 400 && fired < 600);
}

// Огибающая: без запуска молчит, атака линейно до 1, спад до нуля за свое время.
TEST(test_envelope)
{
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_ENV);
    acid_mod_set(&m, 0, ACID_MOD_TRIG, 1);
    run_n(100, 0);
    CHECK(acid_mod_out(&m, 0) == 0);
    acid_mod_set(&m, 0, ACID_MOD_ATTACK, 64); // ~0.1 с
    acid_mod_set(&m, 0, ACID_MOD_DECAY, 64);
    acid_mod_run(&m, ACID_TRIG_NOTE1, 0);
    float peak = 0, prev = 0;
    int peak_at = 0, mono = 1;
    for (int i = 0; i < 1500; i++) {
        acid_mod_run(&m, 0, 0);
        float o = acid_mod_out(&m, 0);
        CHECK(o >= 0 && o <= 1.0001f);
        if (o > peak) {
            peak = o;
            peak_at = i;
        }
        mono &= i > peak_at || o >= prev;
        prev = o;
    }
    CHECK(mono && peak > 0.99f);
    CHECK(peak_at * DT > 0.07f && peak_at * DT < 0.15f);
    CHECK(acid_mod_out(&m, 0) < 0.02f); // спад ~0.1 с давно прошёл
    run_n(3000, 0);
    CHECK(acid_mod_out(&m, 0) == 0);
    // Мгновенная атака: пик сразу.
    acid_mod_set(&m, 0, ACID_MOD_ATTACK, 0);
    acid_mod_run(&m, ACID_TRIG_NOTE1, 0);
    acid_mod_run(&m, 0, 0);
    CHECK(acid_mod_out(&m, 0) > 0.9f);
}

// S&H: держит значение, меняет на запуск; при FREE - раз в период. DRIFT плавный.
TEST(test_random_shapes)
{
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_SH);
    acid_mod_set(&m, 0, ACID_MOD_TRIG, 5);
    acid_mod_run(&m, ACID_TRIG_KICK, 0);
    float a = acid_mod_out(&m, 0);
    run_n(500, 0);
    CHECK(acid_mod_out(&m, 0) == a && a >= -1 && a <= 1);
    int changed = 0;
    for (int i = 0; i < 20; i++) {
        float was = acid_mod_out(&m, 0);
        acid_mod_run(&m, ACID_TRIG_KICK, 0);
        changed += acid_mod_out(&m, 0) != was;
    }
    CHECK(changed >= 18);
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_SH);
    acid_mod_set(&m, 0, ACID_MOD_RATE, 127); // 20 Гц
    int steps = 0;
    float was = 0;
    for (int i = 0; i < 1500; i++) {
        acid_mod_run(&m, 0, 0);
        steps += acid_mod_out(&m, 0) != was;
        was = acid_mod_out(&m, 0);
    }
    CHECK(steps >= 18 && steps <= 21);
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_DRIFT);
    acid_mod_set(&m, 0, ACID_MOD_RATE, 100);
    float lo = 2, hi = -2, jump = 0;
    was = 0;
    for (int i = 0; i < 15000; i++) {
        acid_mod_run(&m, 0, 0);
        float o = acid_mod_out(&m, 0), d = o > was ? o - was : was - o;
        jump = d > jump ? d : jump;
        lo = o < lo ? o : lo;
        hi = o > hi ? o : hi;
        was = o;
    }
    CHECK(jump < 0.1f && lo >= -1 && hi <= 1 && hi - lo > 0.5f);
}

// SYNC без запуска: фаза привязана к позиции клока, период в тиках.
TEST(test_sync_follows_clock)
{
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_RAMP);
    acid_mod_set(&m, 0, ACID_MOD_SYNC, 3); // 1/4 = 24 тика
    // 120 уд/мин: тик раз в 1/48 с = 31.25 куска.
    uint32_t pos = 0;
    float t = 0, next = 0;
    float at_q[4] = {0}, at_h[4] = {0};
    for (int i = 0; i < 1500 * 4; i++) {
        if (t >= next) {
            pos++;
            next += 1.0f / 48.0f;
            acid_mod_run(&m, 0, pos);
            if (pos >= 96 && pos % 24 == 0 && pos / 24 - 4 < 4) {
                at_q[pos / 24 - 4] = acid_mod_out(&m, 0);
            }
            if (pos >= 96 && pos % 24 == 12 && pos / 24 - 4 < 4) {
                at_h[pos / 24 - 4] = acid_mod_out(&m, 0);
            }
        } else {
            acid_mod_run(&m, 0, pos);
        }
        t += DT;
    }
    for (int i = 0; i < 4; i++) {
        CHECK_NEAR(at_q[i], -1.0f, 0.05f); // начало четверти
        CHECK_NEAR(at_h[i], 0.0f, 0.05f);  // середина
    }
    // Между тиками фаза идёт, а не стоит ступенькой.
    float a = acid_mod_out(&m, 0);
    acid_mod_run(&m, 0, pos);
    acid_mod_run(&m, 0, pos);
    CHECK(acid_mod_out(&m, 0) != a);
}

TEST(test_targets_and_offsets)
{
    fresh();
    CHECK(acid_mod_page_ok(6, 3) && acid_mod_page_ok(0, 15) && acid_mod_page_ok(8, 1) && acid_mod_page_ok(10, 2));
    CHECK(acid_mod_page_ok(11, 2) && acid_mod_page_ok(12, 2));
    CHECK(!acid_mod_page_ok(9, 3) && !acid_mod_page_ok(13, 3) && !acid_mod_page_ok(14, 3)); // TONAL, чашка, свои
    CHECK(!acid_mod_page_ok(6, 0) && !acid_mod_page_ok(6, 16) && !acid_mod_page_ok(-1, 3));
    CHECK(acid_mod_target(&m, 0, 0, 6, 3) && acid_mod_take_save(&m));
    CHECK(!acid_mod_target(&m, 0, 0, 9, 3) && m.m[0].t[0].page == 6);
    CHECK(!acid_mod_target(&m, 0, ACID_MOD_TARGETS, 6, 3) && !acid_mod_target(&m, ACID_MODS, 0, 6, 3));
    // Глубина 64 - ноль.
    m.m[0].out = 1.0f;
    CHECK(acid_mod_offset(&m, 0, 0) == 0);
    acid_mod_set(&m, 0, ACID_MOD_DEPTH1, 127);
    CHECK(acid_mod_offset(&m, 0, 0) == 126);
    acid_mod_set(&m, 0, ACID_MOD_DEPTH1, 0);
    CHECK(acid_mod_offset(&m, 0, 0) == -128);
    m.m[0].out = -0.5f;
    CHECK(acid_mod_offset(&m, 0, 0) == 64);
    acid_mod_set(&m, 0, ACID_MOD_AMOUNT, 0);
    CHECK(acid_mod_offset(&m, 0, 0) == 0);
    acid_mod_set(&m, 0, ACID_MOD_AMOUNT, 127);
    CHECK(acid_mod_offset(&m, 0, 1) == 0); // пустая цель
    // Два модулятора на одной ручке складываются.
    acid_mod_target(&m, 1, 2, 6, 3);
    acid_mod_set(&m, 1, ACID_MOD_DEPTH1 + 2, 74);
    m.m[1].out = 1.0f;
    int n = 0;
    CHECK(acid_mod_sum(&m, 6, 3, &n) == 64 + 20 && n == 2);
    CHECK(acid_mod_sum(&m, 6, 4, &n) == 0 && n == 0);
    // Убрать цель.
    CHECK(acid_mod_target(&m, 0, 0, ACID_MOD_NONE, 0) && m.m[0].t[0].page == ACID_MOD_NONE);
    CHECK(acid_mod_sum(&m, 6, 3, &n) == 20 && n == 1);
}

TEST(test_blob)
{
    fresh();
    acid_mod_set(&m, 2, ACID_MOD_SHAPE, ACID_SHAPE_ENV);
    acid_mod_set(&m, 2, ACID_MOD_RATE, 99);
    acid_mod_target(&m, 3, 1, 8, 4);
    uint8_t buf[ACID_MOD_BLOB];
    acid_mod_export(&m, buf);
    acid_mod_t b;
    acid_mod_init(&b, DT);
    CHECK(acid_mod_import(&b, buf, sizeof buf));
    CHECK(acid_mod_knob(&b, 2, ACID_MOD_SHAPE) == ACID_SHAPE_ENV && acid_mod_knob(&b, 2, ACID_MOD_RATE) == 99);
    CHECK(b.m[3].t[1].page == 8 && b.m[3].t[1].enc == 4 && b.m[0].t[0].page == ACID_MOD_NONE);
    CHECK(!acid_mod_take_save(&b)); // чтение памяти - не повод писать
    CHECK(!acid_mod_import(&b, buf, sizeof buf - 1));
    buf[0] ^= 1;
    CHECK(!acid_mod_import(&b, buf, sizeof buf));
    buf[0] ^= 1;
    buf[4 + ACID_MOD_SHAPE] = 200; // мусор в списке: blob чужой
    acid_mod_init(&b, DT);
    CHECK(!acid_mod_import(&b, buf, sizeof buf) && acid_mod_knob(&b, 2, ACID_MOD_RATE) != 99);
}

int main(void)
{
    RUN(test_defaults);
    RUN(test_lists_take_only_their_values);
    RUN(test_free_shapes);
    RUN(test_trigger_resets_phase);
    RUN(test_probability);
    RUN(test_envelope);
    RUN(test_random_shapes);
    RUN(test_sync_follows_clock);
    RUN(test_targets_and_offsets);
    RUN(test_blob);
    TEST_MAIN_END();
}
