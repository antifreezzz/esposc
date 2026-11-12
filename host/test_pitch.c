// Страница PITCH (только панель): строй от тоники страницы 9 и слайд лесенкой по полутонам или четвертьтонам.
#include "acid.h"
#include "test.h"

#include <math.h>
#include <string.h>

#define SR 48000.0f
#define BASS 4 // MIDI канал 5: синт 1
#define SEQ2 5 // MIDI канал 6: синт 2
#define P ACID_PAGE_PITCH

static acid_synth_t s;
static float buf[256];

static void live(void)
{
    acid_synth_init(&s, SR);
    acid_synth_notes_off(&s);
}

static void on(int ch, int note)
{
    acid_midi_event_t e = {.type = ACID_MIDI_NOTE_ON, .ch = (uint8_t)ch, .d1 = (uint8_t)note, .d2 = 100};
    acid_synth_midi(&s, &e);
}

static void render(int n)
{
    while (n > 0) {
        int m = n > 256 ? 256 : n;
        acid_synth_render(&s, buf, m);
        n -= m;
    }
}

// Отклонение звучащей высоты голоса v от ноты note равномерного строя, в центах.
static double cents(const acid_slot_t *v, int note)
{
    return 1200.0 * ((double)acid_slot_pitch(v) - log2((double)acid_midi_to_hz(note)));
}

TEST(test_defaults_equal)
{
    live();
    CHECK(acid_synth_knob(&s, P, 1) == 0 && acid_synth_knob(&s, P, 2) == 0 && acid_synth_knob(&s, P, 3) == 0);
    CHECK(acid_synth_knob(&s, P, 0) == 0 && acid_synth_knob(&s, P, ACID_PITCH_KNOBS + 1) == 0);
    on(BASS, 52);
    render(64);
    CHECK(acid_slot_pitch(&s.slot[0]) == s.slot[0].target_log2);
    CHECK(s.slot[0].target_log2 == log2f(acid_midi_to_hz(52))); // равномерный строй - ровно прежняя высота
}

TEST(test_tuning_just_and_root)
{
    live();
    acid_synth_set_knob(&s, P, 3, ACID_TUNING_JUST);
    CHECK(acid_synth_knob(&s, P, 3) == ACID_TUNING_JUST);
    on(BASS, 52); // E: чистая большая терция от C, 5/4
    render(64);
    CHECK_NEAR(cents(&s.slot[0], 52), -13.69, 0.05);
    on(BASS, 55); // G: 3/2 (гладкий слайд: ждём доезда)
    render(4 * 48000);
    CHECK_NEAR(cents(&s.slot[0], 55), 1.96, 1.0); // гладкий глайд во float застревает в ~0.8 цента от цели
    // Тоника страницы 9 - D: строй считается от неё.
    live();
    acid_synth_set_knob(&s, 9, 7, 2);
    acid_synth_set_knob(&s, P, 3, ACID_TUNING_JUST);
    on(BASS, 54); // F# - терция от D
    render(64);
    CHECK_NEAR(cents(&s.slot[0], 54), -13.69, 0.05);
}

TEST(test_tuning_presets)
{
    // Отклонения ступеней от тоники C в центах: E (4), F# (6), B (11).
    static const struct {
        int t;
        double e, fs, b;
    } T[] = {
        {ACID_TUNING_EQUAL, 0, 0, 0},
        {ACID_TUNING_JUST, -13.69, -17.49, -11.73},
        {ACID_TUNING_PYTHAG, 7.82, 11.73, 9.78},
        {ACID_TUNING_MEANTONE, -13.69, -20.53, -17.11},
        {ACID_TUNING_RAST, -50, 0, -50},
        {ACID_TUNING_BAYATI, 0, 0, 0},
    };
    for (int i = 0; i < (int)(sizeof T / sizeof T[0]); i++) {
        static const int note[3] = {52, 54, 59};
        const double *want = &T[i].e;
        for (int k = 0; k < 3; k++) {
            live();
            acid_synth_set_knob(&s, P, 3, T[i].t);
            on(BASS, note[k]);
            render(64);
            CHECK_NEAR(cents(&s.slot[0], note[k]), want[k], 0.05);
        }
    }
    live();
    acid_synth_set_knob(&s, P, 3, ACID_TUNING_BAYATI);
    on(BASS, 50); // вторая ступень на четверть тона ниже
    render(64);
    CHECK_NEAR(cents(&s.slot[0], 50), -50.0, 0.05);
}

TEST(test_list_ignore)
{
    live();
    acid_synth_set_knob(&s, P, 3, ACID_TUNINGS);
    CHECK(acid_synth_knob(&s, P, 3) == 0);
    acid_synth_set_knob(&s, P, 1, ACID_GLIDES);
    CHECK(acid_synth_knob(&s, P, 1) == 0);
    acid_synth_set_knob(&s, P, 1, ACID_GLIDE_QUARTER);
    CHECK(acid_synth_knob(&s, P, 1) == ACID_GLIDE_QUARTER);
    CHECK(acid_synth_take_save(&s) == 1); // ручка пользователя: память пора записать
}

// Слайд октавой вверх: каждые 1/12 мс смотрим звучащую высоту голоса. Возвращает число разных значений,
// grid - ступень сетки в полутонах (0 - гладко, проверка сетки не делается).
static int slide_values(acid_slot_t *v, int ch, int from, int to, double grid, int *on_grid, int *monotonic)
{
    on(ch, from);
    render(4800);
    on(ch, to);
    double prev = -1e9, last = 1e9;
    int distinct = 0;
    *on_grid = *monotonic = 1;
    for (int i = 0; i < 48000 / 4; i++) {
        render(4);
        double st = cents(v, to) / 100.0;
        if (grid > 0) {
            double q = st / grid;
            *on_grid &= fabs(q - floor(q + 0.5)) < 1e-3;
        }
        *monotonic &= st >= prev - 1e-6;
        distinct += fabs(st - last) > 1e-6;
        prev = st;
        last = st;
    }
    return distinct;
}

TEST(test_glide_semitone_steps)
{
    live();
    acid_synth_set_knob(&s, P, 1, ACID_GLIDE_SEMI);
    int grid, mono;
    int n = slide_values(&s.slot[0], BASS, 45, 57, 1.0, &grid, &mono);
    CHECK(grid && mono);
    CHECK(n >= 10 && n <= 13); // 12 ступеней лесенкой, не сотни
    CHECK(fabs(cents(&s.slot[0], 57)) < 1e-3);
    CHECK(s.slot[0].ctl_log2 == acid_slot_pitch(&s.slot[0])); // осциллятор 303 берёт ту же высоту
}

TEST(test_glide_quarter_steps)
{
    live();
    acid_synth_set_knob(&s, P, 1, ACID_GLIDE_QUARTER);
    int grid, mono;
    int n = slide_values(&s.slot[0], BASS, 45, 57, 0.5, &grid, &mono);
    CHECK(grid && mono);
    CHECK(n >= 20 && n <= 25);
}

TEST(test_glide_smooth_unchanged)
{
    live();
    int grid, mono;
    int n = slide_values(&s.slot[0], BASS, 45, 57, 0, &grid, &mono);
    CHECK(mono && n > 200);
}

// Шаг слайда - свой у синта: синт 2 лесенкой, синт 1 гладко; слой синта 2 - тоже лесенкой.
TEST(test_glide_per_synth_and_layer)
{
    live();
    acid_synth_set_knob(&s, P, 2, ACID_GLIDE_SEMI);
    acid_synth_set_knob(&s, ACID_PAGE_LAYER1 + 1, 1, 0); // слой синта 2 - 303
    int grid, mono;
    int n = slide_values(&s.slot[1], SEQ2, 45, 57, 1.0, &grid, &mono);
    CHECK(grid && n <= 13);
    CHECK(fabs(cents(&s.layer[1], 57)) < 1e-3);
    on(SEQ2, 45);
    render(480);
    double st = cents(&s.layer[1], 45) / 100.0;
    CHECK(fabs(st - floor(st + 0.5)) < 1e-3);
    n = slide_values(&s.slot[0], BASS, 45, 57, 0, &grid, &mono);
    CHECK(n > 200);
}

// Полулесенка: на каждой ступени полшага стоим, полшага едем к следующей. Нота приходит точно.
static void ramp_case(int mode, double grid, int steps)
{
    live();
    acid_synth_set_knob(&s, P, 1, mode);
    CHECK(acid_synth_knob(&s, P, 1) == mode);
    acid_slot_t *v = &s.slot[0];
    on(BASS, 45);
    render(4800);
    on(BASS, 57);
    double prev = -1e9;
    int mono = 1, held = 0, moving = 0, total = 0;
    for (int i = 0; i < 48000 / 4; i++) {
        render(4);
        double st = cents(v, 57) / 100.0;
        if (st > -1e-6) {
            break;
        }
        double q = st / grid;
        held += fabs(q - floor(q + 0.5)) < 1e-3;
        moving += fabs(st - prev) > 1e-6;
        mono &= st >= prev - 1e-6;
        prev = st;
        total++;
    }
    CHECK(mono);
    CHECK(total > 100);
    CHECK(held > total / 5 && held < total * 4 / 5); // стоим на ступенях часть времени, не всё
    CHECK(moving > steps * 3);                         // между ступенями едем, а не прыгаем
    render(48000);
    CHECK(fabs(cents(v, 57)) < 1e-3);
}

TEST(test_glide_semi_ramp)
{
    ramp_case(ACID_GLIDE_SEMI_RAMP, 1.0, 12);
}

TEST(test_glide_quarter_ramp)
{
    ramp_case(ACID_GLIDE_QUARTER_RAMP, 0.5, 24);
}

// Plaits: нота модели на сетке полутонов, пока едет слайд.
TEST(test_glide_steps_plaits)
{
    live();
    acid_synth_set_knob(&s, 6, 1, 1); // синт 1 - VA VCF
    render(9600);                     // смена типа доходит
    acid_synth_set_knob(&s, P, 1, ACID_GLIDE_SEMI);
    on(BASS, 45);
    render(4800);
    on(BASS, 57);
    int ok = 1, moved = 0;
    float first = s.slot[0].pp.note;
    for (int i = 0; i < 400; i++) {
        render(12);
        float x = s.slot[0].pp.note;
        ok &= fabsf(x - floorf(x + 0.5f)) < 1e-3f;
        moved |= x != first;
    }
    CHECK(ok && moved);
}

// Строй и лесенка вместе: ступени считаются от цели, слайд приходит в настроенную ноту.
TEST(test_glide_steps_tuned)
{
    live();
    acid_synth_set_knob(&s, P, 3, ACID_TUNING_JUST);
    acid_synth_set_knob(&s, P, 1, ACID_GLIDE_SEMI);
    int grid, mono;
    slide_values(&s.slot[0], BASS, 48, 52, 0, &grid, &mono);
    CHECK(mono);
    CHECK_NEAR(cents(&s.slot[0], 52), -13.69, 0.05);
}

TEST(test_blob)
{
    static acid_synth_t b;
    static uint8_t blob[ACID_SYNTH_BLOB];
    live();
    acid_synth_set_knob(&s, P, 1, 2);
    acid_synth_set_knob(&s, P, 2, 1);
    acid_synth_set_knob(&s, P, 3, ACID_TUNING_RAST);
    CHECK(acid_synth_export(&s, blob, sizeof blob) == ACID_SYNTH_BLOB);
    CHECK(blob[3] == 9);
    acid_synth_init(&b, SR);
    CHECK(acid_synth_import(&b, blob, sizeof blob) == 1);
    CHECK(acid_synth_knob(&b, P, 1) == 2 && acid_synth_knob(&b, P, 2) == 1);
    CHECK(acid_synth_knob(&b, P, 3) == ACID_TUNING_RAST);
    // Версия 8 (без PITCH) читается, строй и слайд - по умолчанию.
    blob[3] = 8;
    acid_synth_init(&b, SR);
    CHECK(acid_synth_import(&b, blob, ACID_SYNTH_BLOB - ACID_PITCH_KNOBS) == 1);
    CHECK(acid_synth_knob(&b, P, 1) == 0 && acid_synth_knob(&b, P, 3) == 0);
}

int main(void)
{
    RUN(test_defaults_equal);
    RUN(test_tuning_just_and_root);
    RUN(test_tuning_presets);
    RUN(test_list_ignore);
    RUN(test_glide_semitone_steps);
    RUN(test_glide_quarter_steps);
    RUN(test_glide_smooth_unchanged);
    RUN(test_glide_per_synth_and_layer);
    RUN(test_glide_semi_ramp);
    RUN(test_glide_quarter_ramp);
    RUN(test_glide_steps_plaits);
    RUN(test_glide_steps_tuned);
    RUN(test_blob);
    TEST_MAIN_END();
}
