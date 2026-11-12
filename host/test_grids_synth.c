// Синт и автобарабаны: страница Grids, игра от MIDI clock.
#include "acid_drummer.h"
#include "acid_synth.h"
#include "test.h"

#define SR 48000.0f

static acid_synth_t s;

static void ev(int type, int ch, int d1, int d2)
{
    acid_midi_event_t e = {(uint8_t)type, (uint8_t)ch, (uint8_t)d1, (uint8_t)d2};
    acid_synth_midi(&s, &e);
}

static void ticks(int n)
{
    for (int i = 0; i < n; i++) {
        ev(ACID_MIDI_CLOCK, 0, 0, 0);
    }
}

static void grids_on(void)
{
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_KICK, 127);
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_ON, 1);
}

TEST(test_pages)
{
    CHECK(acid_page_to_bsp(ACID_PAGE_GRIDS) == -1);
    CHECK(acid_page_to_bsp(10) == 16);
    CHECK(acid_page_to_bsp(11) == 17);
    for (int v = 0; v < 128; v++) {
        CHECK(acid_page_from_bsp(v) != ACID_PAGE_GRIDS);
    }
}

TEST(test_knob_mirror)
{
    acid_synth_init(&s, SR);
    CHECK(acid_synth_knob(&s, ACID_PAGE_GRIDS, DRM_ON) == 0);
    CHECK(acid_synth_knob(&s, ACID_PAGE_GRIDS, DRM_X) == 64);
    CHECK(acid_synth_knob(&s, ACID_PAGE_GRIDS, DRM_KICK) == 80);
    CHECK(acid_synth_knob(&s, ACID_PAGE_GRIDS, DRM_SNARE) == 70);
    CHECK(acid_synth_knob(&s, ACID_PAGE_GRIDS, DRM_HAT) == 90);
    CHECK(acid_synth_knob(&s, ACID_PAGE_GRIDS, DRM_FILL_EVERY) == 2);
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_X, 30);
    CHECK(acid_synth_knob(&s, ACID_PAGE_GRIDS, DRM_X) == 30);
    CHECK(s.drummer.knob[DRM_X] == 30);
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_HAT_MODE, 3); // вне списка
    CHECK(acid_synth_knob(&s, ACID_PAGE_GRIDS, DRM_HAT_MODE) == 0);
}

TEST(test_plays_on_clock)
{
    acid_synth_init(&s, SR);
    ev(ACID_MIDI_START, 0, 0, 0);
    ticks(96);
    CHECK(s.grids_hits == 0); // выключен
    grids_on();
    s.kick_vel = 0.0f;
    ticks(96);
    CHECK(s.grids_hits > 0);
    CHECK(s.kick_vel > 0.0f);
    CHECK(s.grids_step >= 0 && s.grids_step < 32);
}

TEST(test_enable_mid_bar)
{
    acid_synth_init(&s, SR);
    ev(ACID_MIDI_START, 0, 0, 0);
    ticks(40);
    grids_on();
    ticks(6);
    CHECK(s.grids_hits <= ACID_DRUMMER_MAX); // один шаг, без догоняющих
}

TEST(test_stop_silent)
{
    acid_synth_init(&s, SR);
    grids_on();
    ev(ACID_MIDI_START, 0, 0, 0);
    ticks(24);
    CHECK(s.grids_hits > 0);
    ev(ACID_MIDI_STOP, 0, 0, 0);
    CHECK(s.grids_step == -1);
    uint32_t h = s.grids_hits;
    ticks(96);
    CHECK(s.grids_hits == h);
    CHECK(s.grids_step == -1);
}

TEST(test_bsp_drums_pass)
{
    acid_synth_init(&s, SR);
    grids_on();
    ev(ACID_MIDI_START, 0, 0, 0);
    ticks(10);
    s.snare_vel = 0.0f;
    ev(ACID_MIDI_NOTE_ON, 1, 38, 127);
    CHECK(s.snare_vel == 1.0f);
}

// Свинг: нечётный шаг Grids приходит на swing тиков позже; триольный рисунок идёт 12 шагами по 8 тиков.
TEST(test_swing_and_triplets)
{
    acid_synth_init(&s, SR);
    grids_on();
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SRC, 1);
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SWING, 2);
    CHECK(acid_synth_knob(&s, ACID_PAGE_GRIDS, DRM_SWING) == 2);
    ev(ACID_MIDI_START, 0, 0, 0);
    for (int t = 0; t < 96; t++) {
        ticks(1);
        int st = t / 6, in = t % 6;
        int want = (st & 1) && in < 2 ? st - 1 : st; // нечётный шаг ещё не начался
        CHECK(s.grids_step == want);
    }
    acid_synth_init(&s, SR);
    grids_on();
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SRC, PL_GRIDS_BANK); // SHUFFLE 12/8
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SWING, 3);
    ev(ACID_MIDI_START, 0, 0, 0);
    uint32_t kicks0 = s.grids_hits;
    for (int t = 0; t < 192; t++) {
        ticks(1);
        CHECK(s.grids_step == (t / 96) * 16 + (t % 96) / 8); // второй такт - слоты с 16
    }
    CHECK(s.grids_hits > kicks0 + 8);
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SRC, PL_GRIDS_SRCS); // вне списка
    CHECK(acid_synth_knob(&s, ACID_PAGE_GRIDS, DRM_SRC) == PL_GRIDS_BANK);
}

// Свои рисунки живут вне синта: синт играет их по указателю.
TEST(test_user_patterns)
{
    static pl_grids_pat_t u[PL_GRIDS_USER];
    pl_grids_user_init(u);
    for (int st = 0; st < 32; st += 2) {
        u[1].lv[st] = PL_GRIDS_LV_HIT;
    }
    acid_synth_init(&s, SR);
    grids_on();
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SRC, PL_GRIDS_SRC_USER + 1);
    ev(ACID_MIDI_START, 0, 0, 0);
    ticks(96);
    CHECK(s.grids_hits == 0); // рисунков синту не дали
    acid_synth_use_grids(&s, u);
    ev(ACID_MIDI_START, 0, 0, 0);
    ticks(96);
    CHECK(s.grids_hits == 8);
}

// Свинг крутят на ходу: нечётный шаг играет ровно один раз, новое значение действует со следующего шага.
TEST(test_swing_change_mid_step)
{
    static pl_grids_pat_t u[PL_GRIDS_USER];
    pl_grids_user_init(u);
    for (int st = 0; st < 32; st++) {
        u[0].lv[st] = PL_GRIDS_LV_ACC; // бочка на каждом шаге: удар на шаг
    }
    // Подняли свинг, когда нечётный шаг уже сыграл: второй раз он не играет.
    acid_synth_init(&s, SR);
    acid_synth_use_grids(&s, u);
    grids_on();
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SRC, PL_GRIDS_SRC_USER);
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_FILL_EVERY, 0);
    ev(ACID_MIDI_START, 0, 0, 0);
    ticks(8); // шаг 1 начался и сыграл на своём первом тике
    CHECK(s.grids_hits == 2 && s.grids_step == 1);
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SWING, 3);
    ticks(4); // до конца шага 1
    CHECK(s.grids_hits == 2);
    ticks(84);
    CHECK(s.grids_hits == 16);
    // Убрали свинг, когда нечётный шаг ещё ждал своего тика: он не пропадает.
    acid_synth_init(&s, SR);
    acid_synth_use_grids(&s, u);
    grids_on();
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SRC, PL_GRIDS_SRC_USER);
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_FILL_EVERY, 0);
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SWING, 3);
    ev(ACID_MIDI_START, 0, 0, 0);
    ticks(8); // шаг 1 начался, сыграет на 4-м тике
    CHECK(s.grids_hits == 1 && s.grids_step == 0);
    acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SWING, 0);
    ticks(4);
    CHECK(s.grids_hits == 2 && s.grids_step == 1);
    ticks(84);
    CHECK(s.grids_hits == 16);
}

// Дробь: повторы барабанщика идут в ударку с тиков между шагами.
TEST(test_roll_between_steps)
{
    static pl_grids_pat_t u[PL_GRIDS_USER];
    pl_grids_user_init(u);
    for (int st = 0; st < 32; st++) {
        u[0].lv[2 * 32 + st] = PL_GRIDS_LV_HIT; // хэт на каждом шаге, без акцентов
    }
    for (int roll = 0; roll < 2; roll++) {
        acid_synth_init(&s, SR);
        acid_synth_use_grids(&s, u);
        acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_ON, 1);
        acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_SRC, PL_GRIDS_SRC_USER);
        acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_FILL_EVERY, 0);
        acid_synth_set_knob(&s, ACID_PAGE_GRIDS, DRM_ROLL, roll ? 127 : 0);
        CHECK(acid_synth_knob(&s, ACID_PAGE_GRIDS, DRM_ROLL) == (roll ? 127 : 0));
        ev(ACID_MIDI_START, 0, 0, 0);
        ticks(96);
        if (roll) {
            CHECK(s.grids_hits >= 16 + 12 && s.grids_hits <= 16 + 32);
        } else {
            CHECK(s.grids_hits == 16);
        }
        ticks(1); // шаг 0 следующего такта, за ним могла бы идти дробь
        uint32_t hits = s.grids_hits;
        ev(ACID_MIDI_STOP, 0, 0, 0);
        ticks(12);
        CHECK(s.grids_hits == hits);
    }
}

int main(void)
{
    RUN(test_user_patterns);
    RUN(test_swing_and_triplets);
    RUN(test_swing_change_mid_step);
    RUN(test_pages);
    RUN(test_knob_mirror);
    RUN(test_plays_on_clock);
    RUN(test_enable_mid_bar);
    RUN(test_stop_silent);
    RUN(test_bsp_drums_pass);
    RUN(test_roll_between_steps);
    TEST_MAIN_END();
}
