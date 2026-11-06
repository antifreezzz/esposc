#include "acid_tonal.h"
#include "test.h"

// Энкодеры с 1, как на BSP: 2 длина, 3 шагов, 4 порядок, 5 кого, 6 звучащая нота, 7 лад, 8 тоника, 9-16 шаги.
static acid_tonal_t t;

static void enc(int n, int v)
{
    acid_tonal_set(&t, n - 1, v);
}

static void step_val(int k, int v) // шаг k с 1
{
    enc(8 + k, v);
}

// Тиков подряд; возвращает число границ.
static int ticks(int n)
{
    int b = 0;
    for (int i = 0; i < n; i++) {
        b += acid_tonal_tick(&t);
    }
    return b;
}

// Шаги после каждой из n границ.
static void walk(int *out, int n)
{
    for (int i = 0; i < n; i++) {
        int n = 0;
        while (!acid_tonal_tick(&t) && ++n < 100000) { // предохранитель: границы может не быть
        }
        out[i] = acid_tonal_step(&t);
    }
}

TEST(test_tonal_defaults_inactive)
{
    acid_tonal_init(&t);
    CHECK(!acid_tonal_active(&t));
    CHECK(t.nsteps == 4 && t.order == 0 && t.target == 0 && t.held_mode == 1);
    CHECK(t.scale == 0 && t.tonic == 0);
    CHECK(acid_tonal_note(&t, 60) == 60);
}

TEST(test_tonal_values_screen_exact)
{
    acid_tonal_init(&t);
    enc(2, 3);
    CHECK(t.len == 3 && acid_tonal_active(&t));
    enc(2, 17);
    CHECK(t.len == 3);
    enc(3, 0);
    enc(3, 9);
    CHECK(t.nsteps == 4);
    enc(3, 8);
    CHECK(t.nsteps == 8);
    enc(4, 3);
    CHECK(t.order == 0);
    enc(5, 3);
    CHECK(t.target == 0);
    enc(6, 3);
    CHECK(t.held_mode == 1);
    enc(7, 8);
    CHECK(t.scale == 0);
    enc(8, 12);
    CHECK(t.tonic == 0);
    step_val(1, 76);
    CHECK(t.raw[0] == 12);
    step_val(1, 77);
    step_val(1, 51);
    CHECK(t.raw[0] == 12);
    CHECK(t.knob[8] == 76);
}

TEST(test_tonal_bar_boundary)
{
    acid_tonal_init(&t);
    enc(2, 1);
    acid_tonal_start(&t);
    CHECK(ticks(96) == 0); // первый тик после Start - сильная доля такта 1
    CHECK(acid_tonal_tick(&t) == 1); // 97-й - сильная доля такта 2
    CHECK(acid_tonal_step(&t) == 1);
    enc(2, 2);
    CHECK(ticks(96) == 1); // текущий шаг доигрывает старую длину
    CHECK(ticks(191) == 0); // дальше шаг в 2 такта

    CHECK(acid_tonal_tick(&t) == 1);
}

TEST(test_tonal_wraps_nsteps)
{
    acid_tonal_init(&t);
    enc(2, 1);
    enc(3, 3);
    acid_tonal_start(&t);
    int s[4];
    walk(s, 4);
    CHECK(s[0] == 1 && s[1] == 2 && s[2] == 0 && s[3] == 1);
}

TEST(test_tonal_pingpong)
{
    acid_tonal_init(&t);
    enc(2, 1);
    enc(4, 1);
    acid_tonal_start(&t);
    int s[7];
    walk(s, 7);
    int want[7] = {1, 2, 3, 2, 1, 0, 1};
    int ok = 1;
    for (int i = 0; i < 7; i++) {
        ok &= s[i] == want[i];
    }
    CHECK(ok);
}

TEST(test_tonal_random_no_repeat)
{
    acid_tonal_init(&t);
    enc(2, 1);
    enc(4, 2);
    acid_tonal_start(&t);
    int prev = acid_tonal_step(&t), seen[4] = {0}, ok = 1;
    for (int i = 0; i < 200; i++) {
        int s;
        walk(&s, 1);
        ok &= s != prev && s >= 0 && s < 4;
        seen[s] = 1;
        prev = s;
    }
    CHECK(ok);
    CHECK(seen[0] && seen[1] && seen[2] && seen[3]);
    enc(3, 1);
    int s[3];
    walk(s, 3);
    CHECK(s[1] == 0 && s[2] == 0);
}

TEST(test_tonal_start_resets)
{
    acid_tonal_init(&t);
    enc(2, 2);
    acid_tonal_start(&t);
    ticks(2 * 96 * 2 + 10);
    CHECK(acid_tonal_step(&t) == 2);
    acid_tonal_start(&t);
    CHECK(acid_tonal_step(&t) == 0);
    CHECK(acid_tonal_ticks_left(&t) == 193);
}

TEST(test_tonal_len_change_next_boundary)
{
    acid_tonal_init(&t);
    enc(2, 4);
    acid_tonal_start(&t);
    ticks(100);
    enc(2, 1);
    CHECK(ticks(284) == 0);
    CHECK(acid_tonal_tick(&t) == 1); // 385-й
    CHECK(ticks(95) == 0);
    CHECK(acid_tonal_tick(&t) == 1);
}

TEST(test_tonal_nsteps_shrink_clamps)
{
    acid_tonal_init(&t);
    enc(2, 1);
    enc(3, 8);
    acid_tonal_start(&t);
    int s[5];
    walk(s, 5);
    CHECK(acid_tonal_step(&t) == 5);
    enc(3, 4);
    CHECK(acid_tonal_step(&t) < 8);
    walk(s, 1);
    CHECK(s[0] == 0);
}

// Слой включили посреди такта: граница всё равно на тактовой черте от Start.
TEST(test_tonal_enable_mid_bar_aligns)
{
    acid_tonal_init(&t);
    acid_tonal_start(&t);
    ticks(51); // сильная доля и 50 тиков такта 1
    enc(2, 1);
    CHECK(ticks(45) == 0);
    CHECK(acid_tonal_tick(&t) == 1); // 97-й тик - такт 2
    enc(2, 0);
    ticks(130);
    enc(2, 2); // включили в такте 3: граница через два тактовых черты
    CHECK(ticks(96 - 34 + 96 - 1) == 0);
    CHECK(acid_tonal_tick(&t) == 1);
}

TEST(test_tonal_off_when_len_zero)
{
    acid_tonal_init(&t);
    step_val(1, 67);
    CHECK(acid_tonal_note(&t, 60) == 60);
    CHECK(ticks(500) == 0);
}

TEST(test_tonal_chromatic)
{
    acid_tonal_init(&t);
    enc(2, 1);
    step_val(1, 67);
    CHECK(acid_tonal_note(&t, 60) == 63);
    step_val(1, 52);
    CHECK(acid_tonal_note(&t, 60) == 48);
}

TEST(test_tonal_scale_major)
{
    acid_tonal_init(&t);
    enc(2, 1);
    enc(7, 1);
    step_val(1, 66); // +2 ступени
    CHECK(acid_tonal_note(&t, 64) == 67); // E -> G
    CHECK(acid_tonal_note(&t, 71) == 74); // B -> D октавой выше
    CHECK(acid_tonal_note(&t, 66) == 70); // F# = F + 1: F -> A, плюс остаток = A#
}

TEST(test_tonal_scale_down_octave)
{
    acid_tonal_init(&t);
    enc(2, 1);
    enc(7, 1);
    step_val(1, 57); // -7 ступеней = октава вниз
    CHECK(acid_tonal_note(&t, 60) == 48);
    CHECK(acid_tonal_note(&t, 59) == 47);
    step_val(1, 52); // -12 в ладу клип до -7
    CHECK(acid_tonal_note(&t, 60) == 48);
}

TEST(test_tonal_scale_tonic)
{
    acid_tonal_init(&t);
    enc(2, 1);
    enc(7, 2);
    enc(8, 9); // ля минор
    step_val(1, 65);
    CHECK(acid_tonal_note(&t, 57) == 59); // A -> B
    CHECK(acid_tonal_note(&t, 64) == 65); // E -> F
    CHECK(acid_tonal_note(&t, 55) == 57); // G -> A
}

TEST(test_tonal_pentatonic)
{
    acid_tonal_init(&t);
    enc(2, 1);
    enc(7, 7);
    enc(8, 9); // A C D E G
    step_val(1, 65);
    CHECK(acid_tonal_note(&t, 57) == 60); // A -> C
    CHECK(acid_tonal_note(&t, 67) == 69); // G -> A
}

TEST(test_tonal_step_follows_cycle)
{
    acid_tonal_init(&t);
    enc(2, 1);
    step_val(1, 64);
    step_val(2, 69);
    acid_tonal_start(&t);
    CHECK(acid_tonal_note(&t, 45) == 45);
    ticks(97);
    CHECK(acid_tonal_note(&t, 45) == 50);
}

TEST(test_tonal_moves)
{
    acid_tonal_init(&t);
    CHECK(!acid_tonal_moves(&t, 0)); // выключен - никого
    enc(2, 1);
    CHECK(acid_tonal_moves(&t, 0) && acid_tonal_moves(&t, 1));
    enc(5, 1);
    CHECK(acid_tonal_moves(&t, 0) && !acid_tonal_moves(&t, 1));
    enc(5, 2);
    CHECK(!acid_tonal_moves(&t, 0) && acid_tonal_moves(&t, 1));
}

TEST(test_tonal_drone_note)
{
    acid_tonal_init(&t);
    CHECK(!acid_tonal_active(&t));
    CHECK(acid_tonal_drone_note(&t, 36) == 36);
    enc(8, 7); // тоника G даже при выключенном шагателе
    CHECK(acid_tonal_drone_note(&t, 36) == 43);
    step_val(1, 69); // +5 полутонов в хроматике
    CHECK(acid_tonal_drone_note(&t, 36) == 48);
    enc(7, 2); // натуральный минор
    enc(8, 9); // ля минор: 36 + 9 = 45 (A2)
    step_val(1, 66); // +2 ступени от A2 = C3 (48)
    CHECK(acid_tonal_drone_note(&t, 36) == 48);
    enc(2, 1);
    step_val(2, 63); // -1 ступень от A2 = G2 (43)
    acid_tonal_start(&t);
    ticks(97);
    CHECK(acid_tonal_drone_note(&t, 36) == 43);
}

TEST(test_degree_note)
{
    acid_tonal_t t;
    acid_tonal_init(&t);
    CHECK(acid_tonal_degree_note(&t, 36, 0) == 36); // хроматика: ступень = полутон
    CHECK(acid_tonal_degree_note(&t, 36, 14) == 50);
    acid_tonal_set(&t, 6, 1); // мажор
    CHECK(acid_tonal_degree_note(&t, 36, 0) == 36);
    CHECK(acid_tonal_degree_note(&t, 36, 2) == 40);
    CHECK(acid_tonal_degree_note(&t, 36, 7) == 48);
    CHECK(acid_tonal_degree_note(&t, 36, 14) == 60);
    acid_tonal_set(&t, 6, 7); // пентатоника, 5 ступеней
    CHECK(acid_tonal_degree_note(&t, 36, 5) == 48);
    CHECK(acid_tonal_degree_note(&t, 36, 6) == 51);
}

TEST(test_scale_note)
{
    CHECK(acid_tonal_scale_note(1, 36, 7) == 48);  // мажор: 8-я ступень - октава
    CHECK(acid_tonal_scale_note(2, 36, 2) == 39);  // минор: терция малая
    CHECK(acid_tonal_scale_note(0, 36, 7) == 43);  // хроматика
    CHECK(acid_tonal_scale_note(9, 36, 2) == 38);  // вне таблицы - хроматика
    CHECK(acid_tonal_scale_note(-1, 36, 2) == 38);
}

int main(void)
{
    RUN(test_scale_note);
    RUN(test_tonal_defaults_inactive);
    RUN(test_tonal_values_screen_exact);
    RUN(test_tonal_bar_boundary);
    RUN(test_tonal_wraps_nsteps);
    RUN(test_tonal_pingpong);
    RUN(test_tonal_random_no_repeat);
    RUN(test_tonal_start_resets);
    RUN(test_tonal_len_change_next_boundary);
    RUN(test_tonal_nsteps_shrink_clamps);
    RUN(test_tonal_enable_mid_bar_aligns);
    RUN(test_tonal_off_when_len_zero);
    RUN(test_tonal_chromatic);
    RUN(test_tonal_scale_major);
    RUN(test_tonal_scale_down_octave);
    RUN(test_tonal_scale_tonic);
    RUN(test_tonal_pentatonic);
    RUN(test_tonal_step_follows_cycle);
    RUN(test_tonal_moves);
    RUN(test_tonal_drone_note);
    RUN(test_degree_note);
    TEST_MAIN_END();
}
