// Синт и встроенный секвенсер: ноты с тика клока, смешивание с BSP, Stop и ▶.
#include "acid_seq.h"
#include "acid_synth.h"
#include "test.h"

#define SR 48000.0f
#define TICK_SAMPLES 1000 // 120 BPM

static acid_synth_t s;
static acid_seq_t q;
static float buf[200]; // синт рендерит кусками не больше пары сотен сэмплов

static void render(int n)
{
    for (int i = 0; i < n; i += 200) {
        acid_synth_render(&s, buf, 200);
    }
}

static void ev(int type, int ch, int d1, int d2)
{
    acid_midi_event_t e = {(uint8_t)type, (uint8_t)ch, (uint8_t)d1, (uint8_t)d2};
    acid_synth_midi(&s, &e);
}

// Тики с реальным временем между ними: короткие ноты синт считает дребезгом.
static void ticks(int n)
{
    for (int i = 0; i < n; i++) {
        ev(ACID_MIDI_CLOCK, 0, 0, 0);
        render(TICK_SAMPLES);
    }
}

static void setup(void)
{
    acid_synth_init(&s, SR);
    acid_seq_init(&q);
    acid_seq_set_step(&q, 0, 0, 0, 0); // такт A синта 1: шаг 0 - тоника (C2)
}

TEST(test_seq_plays_note)
{
    setup();
    acid_synth_use_seq(&s, &q);
    ev(ACID_MIDI_START, 0, 0, 0);
    acid_synth_seq_run(&s, 0, 1);
    CHECK(q.t[0].run == 1);
    ticks(1); // общий такт идёт: ждём его шага 0 - это первый тик после Start
    CHECK(s.slot[0].nheld == 1 && s.slot[0].held[0] == 36);
    CHECK(s.layer[0].nheld == 1);
    ticks(3);
    CHECK(s.slot[0].nheld == 0);
}

TEST(test_no_seq_no_notes)
{
    setup();
    q.t[0].run = 1;
    ev(ACID_MIDI_START, 0, 0, 0);
    ticks(96);
    CHECK(s.slot[0].nheld == 0);
    CHECK(q.t[0].sounding == -1);
}

TEST(test_bsp_other_note_keeps_seq)
{
    setup();
    acid_synth_use_seq(&s, &q);
    acid_seq_set_step(&q, 0, 0, 0, 0 | ACID_SEQ_SLIDE); // тянется весь шаг
    ev(ACID_MIDI_START, 0, 0, 0);
    acid_synth_seq_run(&s, 0, 1);
    ticks(1);
    ev(ACID_MIDI_NOTE_ON, 4, 50, 100); // BSP, канал синта 1
    render(TICK_SAMPLES * 2);
    ev(ACID_MIDI_NOTE_OFF, 4, 50, 0);
    render(TICK_SAMPLES * 4);
    CHECK(s.slot[0].nheld == 1 && s.slot[0].held[0] == 36);
}

TEST(test_stop_then_panel_play)
{
    setup();
    acid_synth_use_seq(&s, &q);
    ev(ACID_MIDI_START, 0, 0, 0);
    acid_synth_seq_run(&s, 0, 1);
    ticks(1);
    ev(ACID_MIDI_STOP, 0, 0, 0);
    CHECK(q.t[0].run == 0);
    CHECK(s.slot[0].nheld == 0);
    acid_synth_seq_run(&s, 0, 1); // общий такт стоит: играем сразу
    ticks(1);
    CHECK(s.slot[0].nheld == 1);
}

TEST(test_panel_stop_releases)
{
    setup();
    acid_synth_use_seq(&s, &q);
    acid_seq_set_step(&q, 0, 0, 0, 0 | ACID_SEQ_SLIDE);
    acid_synth_seq_run(&s, 0, 1);
    ticks(2);
    render(TICK_SAMPLES * 2);
    acid_synth_seq_run(&s, 0, 0);
    render(TICK_SAMPLES * 4);
    CHECK(s.slot[0].nheld == 0);
}

TEST(test_start_rewinds)
{
    setup();
    acid_synth_use_seq(&s, &q);
    acid_synth_seq_run(&s, 0, 1);
    ticks(30);
    ev(ACID_MIDI_START, 0, 0, 0);
    CHECK(q.t[0].step == 0 && q.t[0].tick == 0);
}

TEST(test_same_note_seq_gate_keeps_bsp)
{
    setup();
    acid_synth_use_seq(&s, &q);
    ev(ACID_MIDI_NOTE_ON, 4, 36, 100); // BSP держит C2
    ev(ACID_MIDI_START, 0, 0, 0);
    acid_synth_seq_run(&s, 0, 1);
    ticks(6); // секвенсер сыграл C2 и отпустил на гейте
    CHECK(s.slot[0].nheld == 1 && s.slot[0].held[0] == 36);
    ev(ACID_MIDI_NOTE_OFF, 4, 36, 0);
    render(TICK_SAMPLES * 4);
    CHECK(s.slot[0].nheld == 0);
}

TEST(test_same_note_bsp_release_keeps_seq)
{
    setup();
    acid_synth_use_seq(&s, &q);
    acid_seq_set_step(&q, 0, 0, 0, 0 | ACID_SEQ_SLIDE);
    ev(ACID_MIDI_START, 0, 0, 0);
    acid_synth_seq_run(&s, 0, 1);
    ticks(1);
    ev(ACID_MIDI_NOTE_ON, 4, 36, 100);
    render(TICK_SAMPLES * 2);
    ev(ACID_MIDI_NOTE_OFF, 4, 36, 0);
    render(TICK_SAMPLES * 4);
    CHECK(s.slot[0].nheld == 1 && s.slot[0].held[0] == 36);
}

int main(void)
{
    RUN(test_seq_plays_note);
    RUN(test_no_seq_no_notes);
    RUN(test_bsp_other_note_keeps_seq);
    RUN(test_stop_then_panel_play);
    RUN(test_panel_stop_releases);
    RUN(test_start_rewinds);
    RUN(test_same_note_seq_gate_keeps_bsp);
    RUN(test_same_note_bsp_release_keeps_seq);
    TEST_MAIN_END();
}
