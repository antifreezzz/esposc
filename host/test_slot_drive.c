// Перегруз синта на страницах 303 и Plaits: энкодеры 14 перегруз, 15 тон, 16 вой. Верх резонанса 303 - свист.
#include "acid.h"
#include "test.h"

#include <math.h>
#include <string.h>

#define SR 48000.0f
#define BASS 4 // MIDI канал 5
#define SEQ2 5 // MIDI канал 6
#define N 9600

static acid_synth_t s;
static float a[N], b[N];

static const uint8_t ENC[17] = {0, 10, 74, 71, 76, 77, 93, 73, 75, 114, 18, 19, 16, 17, 91, 79, 72};

static void send(uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2)
{
    acid_midi_event_t e = {.type = type, .ch = ch, .d1 = d1, .d2 = d2};
    acid_synth_midi(&s, &e);
}

static void cc(int enc, int val) { send(ACID_MIDI_CC, 1, ENC[enc], (uint8_t)val); }
static void page(int p) { cc(1, p); }
static void on(uint8_t ch, uint8_t note) { send(ACID_MIDI_NOTE_ON, ch, note, 100); }

static void live(void)
{
    acid_synth_init(&s, SR);
    acid_synth_notes_off(&s);
}

// Нота синта на канале ch, ручки задаёт knobs(), рендер в out.
static void play(uint8_t ch, int pg, void (*knobs)(void), float *out)
{
    live();
    page(pg);
    if (knobs) {
        knobs();
    }
    on(ch, 45);
    acid_synth_render(&s, out, N);
}

static double diff(void)
{
    double e = 0.0;
    for (int i = 0; i < N; i++) {
        e += fabs((double)a[i] - b[i]);
    }
    return e;
}

static int finite_all(const float *x)
{
    for (int i = 0; i < N; i++) {
        if (!isfinite(x[i])) {
            return 0;
        }
    }
    return 1;
}

static void drive_off(void) { cc(14, 0), cc(15, 100), cc(16, 127); }
static void drive_max(void) { cc(14, 127), cc(15, 64), cc(16, 127); }

// Перегруз 0 - звук как будто ручек не трогали, бит в бит; любой другой - звук другой.
TEST(test_drive_303)
{
    play(BASS, 6, NULL, a);
    play(BASS, 6, drive_off, b);
    CHECK(memcmp(a, b, sizeof a) == 0);
    play(BASS, 6, drive_max, b);
    CHECK(finite_all(b));
    CHECK(diff() > 1.0);
}

// У Plaits общий на все экземпляры генератор случайных чисел: два прогона бит в бит не сравнить. Выключенный
// перегруз тут - флаг, а путь целиком бит в бит проверяет демо против main.
TEST(test_drive_plaits)
{
    play(SEQ2, 7, NULL, a);
    play(SEQ2, 7, drive_off, b);
    CHECK(!acid_drive_on(&s.slot[1].drv));
    play(SEQ2, 7, drive_max, b);
    CHECK(finite_all(b));
    CHECK(diff() > 1.0);
}

// Тон перегруза едет за нотой: выше нота - выше полосовой.
TEST(test_drive_gets_pitch)
{
    for (int k = 0; k < 2; k++) {
        live();
        page(6 + k);
        drive_max();
        on(k ? SEQ2 : BASS, 45);
        acid_synth_render(&s, a, N);
        float g1 = s.slot[k].drv.bp.g;
        on(k ? SEQ2 : BASS, 57);
        acid_synth_render(&s, a, N);
        CHECK(s.slot[k].drv.bp.g > 1.5f * g1);
    }
}

// Ручки перегруза помнятся по типам, как остальные.
TEST(test_drive_per_type)
{
    live();
    page(6);
    cc(14, 100);
    CHECK(s.slot[0].drv.drive == 100);
    cc(2, 21);
    acid_synth_render(&s, a, 960);
    CHECK(s.slot[0].drv.drive == 0);
    cc(14, 30);
    cc(2, 0);
    acid_synth_render(&s, a, 960);
    CHECK(s.slot[0].drv.drive == 100);
}

// Резонанс 303: до 112 как раньше (0.97 на 127 старой шкалы), выше - в самовозбуждение до максимума фильтра.
TEST(test_res_top_is_whistle)
{
    live();
    page(6);
    cc(4, 112);
    CHECK(s.slot[0].res == 0.97f * 112.0f / 127.0f);
    cc(4, 127);
    CHECK_NEAR(s.slot[0].res, ACID_LADDER_RES_MAX, 1e-6);
    cc(4, 120);
    CHECK(s.slot[0].res > 1.0f && s.slot[0].res < ACID_LADDER_RES_MAX);
}

int main(void)
{
    RUN(test_drive_303);
    RUN(test_drive_plaits);
    RUN(test_drive_gets_pitch);
    RUN(test_drive_per_type);
    RUN(test_res_top_is_whistle);
    TEST_MAIN_END();
}
