// Модуляторы: формы, запуски, вероятность, огибающая, синхронизация, сдвиги целей, blob.
#include <string.h>

#include "acid_mod.h"
#include "acid_synth.h"
#include "pl_labels.h"
#include "pl_pick.h"
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

static acid_synth_t syn;

static void midi_ev(int type, int ch, int d1, int d2)
{
    acid_midi_event_t ev = {.type = type, .ch = (uint8_t)ch, .d1 = (uint8_t)d1, .d2 = (uint8_t)d2};
    acid_synth_midi(&syn, &ev);
}

TEST(test_synth_triggers)
{
    acid_synth_init(&syn, 48000.0f);
    CHECK(acid_synth_take_trig(&syn) == 0);
    midi_ev(ACID_MIDI_NOTE_ON, 4, 60, 100);
    CHECK(acid_synth_take_trig(&syn) == ACID_TRIG_NOTE1);
    CHECK(acid_synth_take_trig(&syn) == 0);
    midi_ev(ACID_MIDI_NOTE_ON, 4, 62, 101);
    CHECK(acid_synth_take_trig(&syn) == (ACID_TRIG_NOTE1 | ACID_TRIG_ACC1));
    midi_ev(ACID_MIDI_NOTE_ON, 5, 48, 120);
    CHECK(acid_synth_take_trig(&syn) == (ACID_TRIG_NOTE2 | ACID_TRIG_ACC2));
    midi_ev(ACID_MIDI_NOTE_ON, 1, 36, 100);
    midi_ev(ACID_MIDI_NOTE_ON, 1, 38, 100);
    midi_ev(ACID_MIDI_NOTE_ON, 1, 39, 100);
    midi_ev(ACID_MIDI_NOTE_ON, 1, 42, 100);
    midi_ev(ACID_MIDI_NOTE_ON, 1, 41, 100);
    CHECK(acid_synth_take_trig(&syn) ==
          (ACID_TRIG_KICK | ACID_TRIG_SNARE | ACID_TRIG_CLAP | ACID_TRIG_HAT | ACID_TRIG_PERC));
    midi_ev(ACID_MIDI_START, 0, 0, 0);
    midi_ev(ACID_MIDI_CLOCK, 0, 0, 0);
    CHECK(acid_synth_take_trig(&syn) == (ACID_TRIG_STEP | ACID_TRIG_BEAT | ACID_TRIG_BAR | ACID_TRIG_BAR4));
    for (int i = 1; i < 6; i++) {
        midi_ev(ACID_MIDI_CLOCK, 0, 0, 0);
    }
    CHECK(acid_synth_take_trig(&syn) == 0 && acid_synth_clock_pos(&syn) == 5);
    midi_ev(ACID_MIDI_CLOCK, 0, 0, 0); // вторая шестнадцатая
    CHECK(acid_synth_take_trig(&syn) == ACID_TRIG_STEP && acid_synth_clock_pos(&syn) == 6);
    for (int i = 7; i <= 96; i++) {
        midi_ev(ACID_MIDI_CLOCK, 0, 0, 0);
    }
    CHECK((acid_synth_take_trig(&syn) & (ACID_TRIG_BAR | ACID_TRIG_BAR4)) == ACID_TRIG_BAR);
}

TEST(test_synth_overlay_and_blob)
{
    acid_synth_init(&syn, 48000.0f);
    acid_synth_set_knob(&syn, 6, 3, 90);
    acid_synth_take_save(&syn);
    static uint8_t blob0[ACID_SYNTH_BLOB], blob1[ACID_SYNTH_BLOB];
    CHECK(acid_synth_export(&syn, blob0, sizeof blob0) == ACID_SYNTH_BLOB);
    float cut0 = syn.slot[0].cut_base;

    // Нетронутая ручка синта (6, 2 = 69), тронутая (6, 3 = 90) и ручка микса (0, 1 = 127).
    CHECK(acid_synth_mod_apply(&syn, 6, 2, 115) == 1);
    CHECK(acid_synth_mod_apply(&syn, 6, 2, 115) == 0); // то же значение повторно не пересчитывает
    CHECK(acid_synth_mod_apply(&syn, 6, 3, 30) == 1);
    CHECK(acid_synth_mod_apply(&syn, 0, 1, 50) == 1);
    CHECK(syn.slot[0].cut_base > cut0 * 2.0f && syn.master < 0.5f);
    CHECK(acid_synth_knob(&syn, 6, 2) == 69 && acid_synth_knob(&syn, 6, 3) == 90 && acid_synth_knob(&syn, 0, 1) == 127);
    CHECK(!acid_synth_take_save(&syn));
    CHECK(acid_synth_export(&syn, blob1, sizeof blob1) == ACID_SYNTH_BLOB);
    CHECK(memcmp(blob0, blob1, ACID_SYNTH_BLOB) == 0);

    // Правка базы под модуляцией: меняет базу и ставит флаг записи, звук обновит следующий шаг модулятора.
    float mod_cut = syn.slot[0].cut_base;
    acid_synth_set_knob(&syn, 6, 2, 80);
    CHECK(acid_synth_knob(&syn, 6, 2) == 80 && acid_synth_take_save(&syn));
    CHECK(syn.slot[0].cut_base == mod_cut);

    acid_synth_mod_release(&syn, 6, 2);
    CHECK(acid_synth_knob(&syn, 6, 2) == 80 && syn.slot[0].cut_base != mod_cut && syn.slot[0].cut_base != cut0);
    CHECK(!acid_synth_take_save(&syn));

    acid_synth_mod_clear(&syn);
    CHECK(syn.master == 1.0f && acid_synth_knob(&syn, 6, 3) == 90);
}

TEST(test_mod_step_and_type_change)
{
    acid_synth_init(&syn, 48000.0f);
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_SQR); // в начале периода out = +1
    for (int i = 0; i < ACID_MOD_TARGETS; i++) {
        acid_mod_target(&m, 0, i, 0, 2 + i); // DRIVE, SYNTH 1, SYNTH 2, KICK
        acid_mod_set(&m, 0, ACID_MOD_DEPTH1 + i, 100);
    }
    acid_mod_take_save(&m);
    // За кусок не больше 1 ручки, по кругу. Общий бюджет с макро ограничивает её ещё сильнее.
    CHECK(acid_mod_step(&m, &syn, 2) == 1);
    CHECK(syn.mod_base[0][2] != 0xFF && syn.mod_base[0][3] == 0xFF);
    CHECK(acid_mod_step(&m, &syn, 1) == 1);
    CHECK(syn.mod_base[0][3] != 0xFF && syn.mod_base[0][4] == 0xFF);
    CHECK(acid_mod_step(&m, &syn, 2) == 1);
    CHECK(syn.mod_base[0][4] != 0xFF && syn.mod_base[0][5] == 0xFF);
    CHECK(acid_mod_step(&m, &syn, 2) == 1);
    CHECK(syn.mod_base[0][5] != 0xFF);
    CHECK(acid_mod_step(&m, &syn, 0) == 0); // обе ручки общего бюджета заняты макро
    CHECK(acid_mod_step(&m, &syn, 2) == 0); // все 4 уже на своих значениях

    // На любой ступени защиты модуляция и действующие значения замирают, база остаётся под оверлеем.
    for (int tight = 1; tight <= 3; tight++) {
        syn.tight = tight;
        float out = acid_mod_out(&m, 0);
        int effective = syn.raw[0][2];
        int base = acid_synth_knob(&syn, 0, 2);
        acid_mod_set(&m, 0, ACID_MOD_DEPTH1, tight == 1 ? 40 : 30);
        CHECK(acid_mod_step(&m, &syn, 2) == 0);
        CHECK(acid_mod_out(&m, 0) == out && syn.raw[0][2] == effective);
        CHECK(acid_synth_knob(&syn, 0, 2) == base && syn.mod_base[0][2] != 0xFF);
    }
    syn.tight = 0;
    CHECK(acid_mod_step(&m, &syn, 2) == 1); // после защиты движение продолжается с новой глубиной

    // Глубина 64 (ноль) и удаление цели возвращают базу и снимают оверлей, когда есть бюджет.
    acid_mod_set(&m, 0, ACID_MOD_DEPTH1, 64);
    acid_mod_target(&m, 0, 1, ACID_MOD_NONE, 0);
    float phase = m.m[0].phase;
    CHECK(acid_mod_step(&m, &syn, 0) == 0 && m.m[0].phase != phase); // фазы идут без обхода целей
    CHECK(syn.mod_base[0][2] != 0xFF && syn.mod_base[0][3] != 0xFF);
    acid_mod_step(&m, &syn, 4);
    CHECK(syn.mod_base[0][2] == 0xFF && syn.mod_base[0][3] == 0xFF);

    // Смена типа синта снимает оверлей; ручка, которой у нового типа нет (enc 10 у 303), не модулируется.
    acid_synth_set_knob(&syn, 6, 1, 1); // Plaits
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_SQR);
    acid_mod_target(&m, 0, 0, 6, 2);  // HARMONICS -> у 303 CUTOFF (плавная)
    acid_mod_target(&m, 0, 1, 6, 10); // M ENV -> у 303 пусто
    acid_mod_set(&m, 0, ACID_MOD_DEPTH1, 100);
    acid_mod_set(&m, 0, ACID_MOD_DEPTH1 + 1, 100);
    acid_mod_step(&m, &syn, 4);
    acid_mod_step(&m, &syn, 4);
    CHECK(syn.mod_base[6][2] == 64 && syn.mod_base[6][10] == 64);
    acid_synth_set_knob(&syn, 6, 1, 0); // 303: оверлей снят сразу
    CHECK(syn.mod_base[6][2] == 0xFF && syn.mod_base[6][10] == 0xFF);
    acid_mod_step(&m, &syn, 4);
    CHECK(syn.mod_base[6][2] == 69 && syn.mod_base[6][10] == 0xFF);
}

TEST(test_smooth_matches_pick)
{
    acid_synth_init(&syn, 48000.0f);
    for (int page = 0; page < ACID_PAGES; page++) {
        int is_syn = page == 6 || page == 7 || page == 10 || page == 11;
        int max_t = is_syn ? (page >= 10 ? ACID_LAYER_OFF : ACID_TYPES - 1) : 0;
        for (int t = 0; t <= max_t; t++) {
            if (page >= 10 && t == ACID_TYPE_PETRI) {
                continue;
            }
            if (is_syn) {
                acid_synth_set_knob(&syn, page, 1, t);
            }
            uint8_t raw[16] = {0};
            for (int e = 1; e < ACID_KNOBS; e++) {
                raw[e] = (uint8_t)acid_synth_knob(&syn, page, e);
            }
            for (int e = 0; e < ACID_KNOBS; e++) {
                int want = acid_mod_page_ok(page, e) && pl_knob_label(page, e, raw) != NULL &&
                           pl_pick_kind(page, e, raw) == PL_PICK_NONE;
                CHECK(acid_synth_knob_smooth(&syn, page, e) == want);
            }
        }
    }
}

// Блок синта - три куска: применение целей раз в блок, фазы каждый кусок. Под защитой замирает всё, и на кусках
// без применения: фазы стоят, запуск ждёт в синте и срабатывает после защиты.
TEST(test_chunk_freezes_under_guard)
{
    acid_synth_init(&syn, 48000.0f);
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_SAW);
    acid_mod_set(&m, 1, ACID_MOD_SHAPE, ACID_SHAPE_ENV);
    acid_mod_set(&m, 1, ACID_MOD_TRIG, 5); // KICK
    acid_mod_target(&m, 0, 0, 0, 2);
    acid_mod_set(&m, 0, ACID_MOD_DEPTH1, 100);
    // Без защиты: кусок без применения двигает фазу, ручку не трогает; с применением - ставит.
    float phase = m.m[0].phase;
    CHECK(acid_mod_chunk(&m, &syn, 0, 2) == 0 && m.m[0].phase != phase && syn.mod_base[0][2] == 0xFF);
    CHECK(acid_mod_chunk(&m, &syn, 1, 2) == 1 && syn.mod_base[0][2] != 0xFF);
    for (int tight = 1; tight <= 3; tight++) {
        syn.tight = tight;
        acid_midi_event_t kick = {.type = ACID_MIDI_NOTE_ON, .ch = 1, .d1 = 36, .d2 = 100};
        acid_synth_midi(&syn, &kick);
        phase = m.m[0].phase;
        float out = acid_mod_out(&m, 0);
        for (int c = 0; c < 6; c++) {
            CHECK(acid_mod_chunk(&m, &syn, c % 3 == 0, 2) == 0);
        }
        CHECK(m.m[0].phase == phase && acid_mod_out(&m, 0) == out);
        CHECK(m.m[1].stage == 0 && acid_mod_out(&m, 1) == 0.0f); // огибающая не запустилась
        CHECK(syn.trig & ACID_TRIG_KICK);                       // запуск ждёт
    }
    syn.tight = 0;
    acid_mod_chunk(&m, &syn, 0, 2);
    CHECK(m.m[0].phase != phase && m.m[1].stage == 1);
}

// Позиция для панели: у LFO - фаза 0..255, у огибающей - стадия (0 молчит, 1 атака, 2 спад).
TEST(test_pos_byte)
{
    fresh();
    acid_mod_set(&m, 0, ACID_MOD_SHAPE, ACID_SHAPE_SAW);
    CHECK(acid_mod_pos(&m, 0) == 0);
    int last = 0, wraps = 0;
    for (int i = 0; i < 3000; i++) {
        acid_mod_run(&m, 0, 0);
        int p = acid_mod_pos(&m, 0);
        CHECK(p >= 0 && p <= 255 && p == (int)(m.m[0].phase * 256.0f));
        wraps += p < last;
        last = p;
    }
    CHECK(wraps >= 1); // RATE 64 - ~1 Гц, 3000 кусков - 2 с
    acid_mod_set(&m, 1, ACID_MOD_SHAPE, ACID_SHAPE_ENV);
    acid_mod_set(&m, 1, ACID_MOD_TRIG, 1);
    acid_mod_set(&m, 1, ACID_MOD_ATTACK, 64);
    acid_mod_run(&m, 0, 0);
    CHECK(acid_mod_pos(&m, 1) == 0);
    acid_mod_run(&m, ACID_TRIG_NOTE1, 0);
    CHECK(acid_mod_pos(&m, 1) == 1);
    run_n(200, 0); // атака ~0.1 с (150 кусков) прошла, спад ещё идёт
    CHECK(acid_mod_pos(&m, 1) == 2);
    run_n(6000, 0);
    CHECK(acid_mod_pos(&m, 1) == 0);
    CHECK(acid_mod_pos(&m, -1) == 0 && acid_mod_pos(&m, ACID_MODS) == 0);
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
    RUN(test_synth_triggers);
    RUN(test_synth_overlay_and_blob);
    RUN(test_mod_step_and_type_change);
    RUN(test_smooth_matches_pick);
    RUN(test_chunk_freezes_under_guard);
    RUN(test_pos_byte);
    TEST_MAIN_END();
}
