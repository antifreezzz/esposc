// Синт и встроенный секвенсер: ноты с тика клока, смешивание с BSP, Stop и ▶.
#include "acid_mod.h"
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
    CHECK(s.slot[0].nheld == 1); // гейт - тик 5 из 6
    ticks(2);
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

// Ретриггер x3: удары по тику (21 мс при 120 BPM) короче окна дребезга пэдов, но секвенсер не пэд -
// каждое отпускание настоящее и каждый удар запускает ноту заново.
TEST(test_retrig_not_debounced)
{
    setup();
    acid_seq_set_step(&q, 0, 0, 0, 0 | ACID_SEQ_RTG | ACID_SEQ_SLIDE);
    acid_synth_use_seq(&s, &q);
    ev(ACID_MIDI_START, 0, 0, 0);
    acid_synth_seq_run(&s, 0, 1);
    int hits = 0;
    for (int t = 0; t < 6; t++) {
        s.trig = 0;
        ticks(1);
        hits += (s.trig & ACID_TRIG_NOTE1) != 0;
        CHECK(s.slot[0].nheld == (t % 2 == 0));
        CHECK(s.layer[0].nheld == (t % 2 == 0));
    }
    CHECK(hits == 3);
}

// GATE < 127 режет удары ретриггера в сэмплах по темпу (короче одного тика клока), но не трогает обычные шаги.
TEST(test_retrig_gate)
{
    setup();
    acid_seq_set_step(&q, 0, 0, 0, 0 | ACID_SEQ_RTG); // 2 удара по 3 тика (3000 сэмплов)
    acid_seq_set_step(&q, 0, 0, 1, 2);                // обычный шаг: не режется
    acid_synth_use_seq(&s, &q);
    acid_synth_set_knob(&s, ACID_PAGE_PITCH, 4, 32); // ~25% удара (~756 сэмплов)
    ev(ACID_MIDI_START, 0, 0, 0);
    acid_synth_seq_run(&s, 0, 1);
    ev(ACID_MIDI_CLOCK, 0, 0, 0); // тик 0: первый удар ретриггера
    int open = 0;
    for (int i = 0; i < TICK_SAMPLES; i++) {
        acid_synth_render(&s, buf, 1);
        open += s.slot[0].amp.gate;
    }
    CHECK(open > 650 && open < 850); // закрылся раньше первого тика клока
    CHECK(s.slot[0].amp.gate == 0);
    // Доезжаем до шага 1 (обычный шаг): гейт открыт весь тик.
    ticks(5);
    ev(ACID_MIDI_CLOCK, 0, 0, 0);
    int open_norm = 0;
    for (int i = 0; i < TICK_SAMPLES; i++) {
        acid_synth_render(&s, buf, 1);
        open_norm += s.slot[0].amp.gate;
    }
    CHECK(open_norm == TICK_SAMPLES);
}

TEST(test_slide_step_to_step)
{
    setup();
    acid_seq_set_step(&q, 0, 0, 0, 0 | ACID_SEQ_SLIDE); // C2 + SLIDE
    acid_seq_set_step(&q, 0, 0, 5, 0 | ACID_SEQ_UP);    // C3 через 5 шагов
    acid_synth_use_seq(&s, &q);
    ev(ACID_MIDI_START, 0, 0, 0);
    acid_synth_seq_run(&s, 0, 1);
    ev(ACID_MIDI_CLOCK, 0, 0, 0); // шаг 0, тик 0
    float p0 = s.slot[0].pitch_log2;
    render(TICK_SAMPLES);
    // Середина пути (14 тиков = 15000 сэмплов = 2.5 шага): высота уже едет, середина октавы.
    ticks(14);
    float p_mid = s.slot[0].pitch_log2;
    CHECK(fabsf(p_mid - (p0 + 0.5f)) < 0.05f);
    // Доезжаем до конца 5-го шага (ещё 15 тиков = 30000 сэмплов): высота дошла до C3.
    ticks(15);
    float p_end = s.slot[0].pitch_log2;
    CHECK(fabsf(p_end - (p0 + 1.0f)) < 0.001f);
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
    RUN(test_retrig_not_debounced);
    RUN(test_retrig_gate);
    RUN(test_slide_step_to_step);
    TEST_MAIN_END();
}
