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

int main(void)
{
    RUN(test_pages);
    RUN(test_knob_mirror);
    RUN(test_plays_on_clock);
    RUN(test_enable_mid_bar);
    RUN(test_stop_silent);
    RUN(test_bsp_drums_pass);
    TEST_MAIN_END();
}
