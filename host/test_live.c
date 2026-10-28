#include "acid.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f
#define BASS 4  // MIDI канал 5
#define DRUMS 1 // MIDI канал 2
#define SEQ2 5  // MIDI канал 6

static acid_synth_t s;
static float buf[48000];

static void live(void)
{
    acid_synth_init(&s, SR);
    acid_synth_set_live(&s, 1);
}

static void send(uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2)
{
    acid_midi_event_t e = {.type = type, .ch = ch, .d1 = d1, .d2 = d2};
    acid_synth_midi(&s, &e);
}

static void on(uint8_t ch, uint8_t note, uint8_t vel) { send(ACID_MIDI_NOTE_ON, ch, note, vel); }
static void off(uint8_t ch, uint8_t note) { send(ACID_MIDI_NOTE_OFF, ch, note, 0); }
static void cc(uint8_t num, uint8_t val) { send(ACID_MIDI_CC, DRUMS, num, val); }

static float rms(int n)
{
    acid_synth_render(&s, buf, (size_t)n);
    double sq = 0.0;
    for (int i = 0; i < n; i++) {
        sq += (double)buf[i] * buf[i];
    }
    return (float)sqrt(sq / n);
}

static float peak(int n)
{
    acid_synth_render(&s, buf, (size_t)n);
    float p = 0.0f;
    for (int i = 0; i < n; i++) {
        p = fabsf(buf[i]) > p ? fabsf(buf[i]) : p;
    }
    return p;
}

// Наибольший скачок между соседними сэмплами, prev - последний сэмпл до этого.
static float max_jump(float prev, int n)
{
    acid_synth_render(&s, buf, (size_t)n);
    float j = 0.0f;
    for (int i = 0; i < n; i++) {
        j = fabsf(buf[i] - prev) > j ? fabsf(buf[i] - prev) : j;
        prev = buf[i];
    }
    return j;
}

TEST(test_live_silent_until_note)
{
    live();
    CHECK(rms(9600) < 1e-4f);
}

TEST(test_live_note_pitch)
{
    live();
    on(BASS, 45, 100);
    CHECK(rms(2400) > 0.01f);
    CHECK_NEAR(acid_synth_pitch_hz(&s), 110.0f, 0.5f);
}

TEST(test_live_overlap_slides)
{
    live();
    on(BASS, 45, 100);
    rms(4800);
    float f1 = s.slot[0].fenv.value;
    CHECK(f1 < 0.9f);
    on(BASS, 57, 100);
    rms(1);
    CHECK(s.slot[0].fenv.value <= f1); // огибающая не перезапущена
    rms(480);
    float hz = acid_synth_pitch_hz(&s);
    CHECK(hz > 111.0f && hz < 218.0f); // ещё ползёт
    rms(24000); // глайд с постоянной 60 мс: за 0.5 с доходит
    CHECK_NEAR(acid_synth_pitch_hz(&s), 220.0f, 1.0f);
    CHECK(s.slot[0].amp.gate == 1);
}

TEST(test_live_release_returns_to_held)
{
    live();
    on(BASS, 45, 100);
    on(BASS, 57, 100);
    rms(9600);
    off(BASS, 57);
    rms(24000);
    CHECK_NEAR(acid_synth_pitch_hz(&s), 110.0f, 0.5f);
    CHECK(s.slot[0].amp.gate == 1);
    CHECK(rms(480) > 0.005f);
}

TEST(test_live_note_off_closes_gate)
{
    live();
    on(BASS, 45, 100);
    rms(2400);
    off(BASS, 45);
    rms(4800);
    CHECK(s.slot[0].amp.gate == 0);
    CHECK(rms(480) < 1e-3f);
}

TEST(test_live_accent_velocity)
{
    live();
    on(BASS, 45, 100);
    float normal = rms(4800);
    live();
    on(BASS, 45, 127);
    float accent = rms(4800);
    CHECK(accent > normal * 1.2f);
}

TEST(test_live_drums)
{
    live();
    on(DRUMS, 36, 127);
    rms(1);
    CHECK(s.kick.amp_env > 0.9f);
    on(DRUMS, 39, 127);
    rms(1);
    CHECK(s.clap.env > 0.0f);
    on(DRUMS, 42, 127);
    rms(1);
    CHECK(s.hat.env > 0.0f && s.hat.coef == s.hat.coef_closed);
    on(DRUMS, 46, 127);
    CHECK(s.hat.coef == s.hat.coef_open);

    live();
    on(DRUMS, 36, 127);
    float loud = peak(2400);
    live();
    on(DRUMS, 36, 64);
    float soft = peak(2400);
    CHECK(soft < loud * 0.8f);
}

// CC энкодеров BSP по порядку на панели: верхний ряд 1-8, нижний 9-16.
static const uint8_t ENC[17] = {0, 10, 74, 71, 76, 77, 93, 73, 75, 114, 18, 19, 16, 17, 91, 79, 72};

static void page(int p)
{
    cc(ENC[1], (uint8_t)(p * 127 / 3)); // 4 страницы: 0, 42, 84, 127
}

TEST(test_live_page_select)
{
    live();
    CHECK(s.page == 2); // до касания энкодера 1 - синт 1
    cc(ENC[1], 0);
    CHECK(s.page == 0);
    cc(ENC[1], 31);
    CHECK(s.page == 0);
    cc(ENC[1], 32);
    CHECK(s.page == 1);
    cc(ENC[1], 63);
    CHECK(s.page == 1);
    cc(ENC[1], 64);
    CHECK(s.page == 2);
    cc(ENC[1], 95);
    CHECK(s.page == 2);
    cc(ENC[1], 96);
    CHECK(s.page == 3);
    cc(ENC[1], 127);
    CHECK(s.page == 3);
}

TEST(test_live_bass_page)
{
    live();
    page(2);
    cc(ENC[3], 0);
    CHECK_NEAR(s.slot[0].cut_base, 60.0f, 0.1f);
    cc(ENC[3], 127);
    CHECK_NEAR(s.slot[0].cut_base, 3000.0f, 1.0f);
    cc(ENC[4], 0);
    CHECK_NEAR(s.slot[0].res, 0.0f, 1e-6f);
    cc(ENC[4], 127);
    CHECK_NEAR(s.slot[0].res, 0.97f, 1e-4f);
    cc(ENC[8], 0);
    CHECK(s.slot[0].wave == 0);
    cc(ENC[8], 127);
    CHECK(s.slot[0].wave == 1);
}

TEST(test_live_transpose)
{
    live();
    page(2);
    cc(ENC[10], 127); // +12
    on(BASS, 45, 100);
    rms(480);
    CHECK_NEAR(acid_synth_pitch_hz(&s), 220.0f, 1.0f);
    off(BASS, 45);
    cc(ENC[10], 0); // -12
    rms(4800);
    on(BASS, 45, 100);
    rms(480);
    CHECK_NEAR(acid_synth_pitch_hz(&s), 55.0f, 0.5f);
}

TEST(test_live_glide_knob)
{
    live();
    page(2);
    cc(ENC[9], 0); // самый быстрый глайд, 10 мс
    on(BASS, 45, 100);
    rms(480);
    on(BASS, 57, 100);
    rms(2400); // 50 мс = 5 постоянных времени
    CHECK_NEAR(acid_synth_pitch_hz(&s), 220.0f, 2.0f);
}

TEST(test_live_mix_page)
{
    live();
    page(0);
    float cut = s.slot[0].cut_base;
    cc(ENC[2], 0); // мастер в ноль
    CHECK_NEAR(s.slot[0].cut_base, cut, 1e-6f); // ручка страницы микса не трогает бас
    on(BASS, 45, 127);
    on(DRUMS, 36, 127);
    CHECK(rms(2400) < 1e-6f);
    cc(ENC[3], 127);
    CHECK_NEAR(s.drive, 3.0f, 1e-3f);
    cc(ENC[4], 0);
    CHECK_NEAR(s.slot[0].gain, 0.0f, 1e-6f);
    cc(ENC[5], 127);
    CHECK_NEAR(s.slot[1].gain, 1.5f, 1e-6f);
    cc(ENC[9], 0);
    CHECK_NEAR(s.snare_gain, 0.0f, 1e-6f);
}

TEST(test_live_drum_page)
{
    live();
    page(1);
    cc(ENC[2], 127);
    CHECK_NEAR(s.kick.low_hz, 80.0f, 0.1f);
    cc(ENC[12], 0);
    CHECK_NEAR(s.snare.snappy, 0.0f, 1e-6f);
    cc(ENC[12], 127);
    CHECK_NEAR(s.snare.snappy, 1.0f, 1e-6f);
    cc(ENC[11], 0);
    CHECK_NEAR(s.snare.tune_hz, 120.0f, 0.1f);
}

TEST(test_live_snare_and_clap_notes)
{
    live();
    on(DRUMS, 38, 127);
    rms(1);
    CHECK(s.snare.noise_env > 0.0f);
    CHECK(s.clap.t < 0);
    live();
    on(DRUMS, 40, 127);
    rms(1);
    CHECK(s.snare.noise_env > 0.0f);
    live();
    on(DRUMS, 39, 127);
    rms(1);
    CHECK(s.clap.t >= 0);
    CHECK(s.snare.noise_env == 0.0f);
}

TEST(test_live_ignores_other_channels)
{
    live();
    on(0, 45, 100); // канал 1
    on(9, 37, 127); // метроном, канал 10
    CHECK(rms(4800) < 1e-4f);
}

TEST(test_live_stop_silences)
{
    live();
    on(BASS, 45, 100);
    on(BASS, 50, 100);
    rms(2400);
    send(ACID_MIDI_STOP, 0, 0, 0);
    rms(4800);
    CHECK(rms(480) < 1e-3f);
    on(BASS, 45, 100); // после Stop всё играет снова
    CHECK(rms(2400) > 0.01f);
}

TEST(test_live_transitions_no_click)
{
    // Автопилот -> live посреди ноты.
    acid_synth_init(&s, SR);
    rms(30000);
    float prev = buf[29999];
    acid_synth_set_live(&s, 1);
    CHECK(max_jump(prev, 960) < 0.3f);

    // Live с зажатой нотой -> автопилот.
    on(BASS, 45, 127);
    rms(4800);
    prev = buf[4799];
    acid_synth_set_live(&s, 0);
    CHECK(max_jump(prev, 960) < 0.3f);
}

TEST(test_live_off_restarts_track)
{
    acid_synth_init(&s, SR);
    rms(48000);
    acid_synth_set_live(&s, 1);
    cc(ENC[4], 0); // резонанс на странице синта 1
    on(BASS, 45, 100);
    rms(4800);
    acid_synth_set_live(&s, 0);
    CHECK(s.arr_on == 1 && s.bar == 0 && s.cycle == 0 && s.started == 0);
    rms(4800);
    CHECK(s.bar == 0 && s.started == 1);
    CHECK(s.slot[0].res > 0.5f); // резонанс снова от автоматизации, а не от ручки
    CHECK(s.kick.amp_env > 0.1f); // бочка автопилота на месте
}

static float slot_hz(int k)
{
    return exp2f(s.slot[k].pitch_log2);
}

TEST(test_live_seq2_plays_slot1)
{
    live();
    on(SEQ2, 45, 100);
    CHECK(rms(2400) > 0.01f);
    CHECK_NEAR(slot_hz(1), 110.0f, 0.5f);
    CHECK(s.slot[0].amp.gate == 0);
    on(BASS, 57, 100);
    rms(480);
    CHECK_NEAR(slot_hz(0), 220.0f, 0.5f); // свой стек: не слайд из ноты синта 2
    CHECK_NEAR(slot_hz(1), 110.0f, 0.5f);
    off(SEQ2, 45);
    rms(4800);
    CHECK(s.slot[1].amp.gate == 0);
    CHECK(s.slot[0].amp.gate == 1);
}

TEST(test_live_slot_knobs_independent)
{
    live();
    page(3);
    cc(ENC[3], 0);
    CHECK_NEAR(s.slot[1].cut_base, 60.0f, 0.1f);
    CHECK(s.slot[0].cut_base > 400.0f);
    page(2);
    cc(ENC[3], 127);
    CHECK_NEAR(s.slot[0].cut_base, 3000.0f, 1.0f);
    CHECK_NEAR(s.slot[1].cut_base, 60.0f, 0.1f);
}

TEST(test_live_stop_silences_both)
{
    live();
    on(BASS, 45, 100);
    on(SEQ2, 50, 100);
    rms(2400);
    send(ACID_MIDI_STOP, 0, 0, 0);
    rms(4800);
    CHECK(s.slot[0].amp.gate == 0 && s.slot[1].amp.gate == 0);
    CHECK(rms(480) < 1e-3f);
}

TEST(test_render_split_identical)
{
    // Слот 1 отдельным буфером (как на ядре 0) звучит так же, как общий рендер.
    static acid_synth_t a, b;
    static float x[96], y[96], ext[96];
    acid_synth_init(&a, SR);
    acid_synth_init(&b, SR);
    acid_synth_set_live(&a, 1);
    acid_synth_set_live(&b, 1);
    int same = 1;
    for (int blk = 0; blk < 500; blk++) {
        if (blk % 50 == 0) {
            acid_midi_event_t ev[3] = {
                {.type = ACID_MIDI_NOTE_ON, .ch = SEQ2, .d1 = (uint8_t)(40 + blk / 50), .d2 = 120},
                {.type = ACID_MIDI_NOTE_ON, .ch = BASS, .d1 = 45, .d2 = 100},
                {.type = ACID_MIDI_NOTE_ON, .ch = DRUMS, .d1 = 36, .d2 = 100},
            };
            for (int e = 0; e < 3; e++) {
                acid_synth_midi(&a, &ev[e]);
                acid_synth_midi(&b, &ev[e]);
            }
        }
        acid_synth_render(&a, x, 96);
        acid_synth_render_slot(&b, 1, ext, 96);
        acid_synth_render_with(&b, y, 96, ext);
        for (int i = 0; i < 96; i++) {
            same &= x[i] == y[i];
        }
    }
    CHECK(same);
}

TEST(test_live_soft_drum_hit_audible)
{
    live();
    on(DRUMS, 36, 127);
    float loud = peak(2400);
    live();
    on(DRUMS, 36, 10);
    CHECK(peak(2400) > loud * 0.4f); // слабый удар пэда должен быть слышен
}

TEST(test_live_bass_bounce_merged)
{
    // Дребезг пэда: нота 10 мс, отпускание, через 30 мс та же нота снова.
    live();
    on(BASS, 45, 60);
    rms(480);
    off(BASS, 45);
    int gate_dropped = 0;
    for (int i = 0; i < 1440; i++) {
        rms(1);
        gate_dropped |= s.slot[0].amp.gate == 0;
    }
    float f = s.slot[0].fenv.value;
    on(BASS, 45, 5);
    rms(1);
    CHECK(!gate_dropped);
    CHECK(s.slot[0].fenv.value <= f); // огибающая не перезапущена: одна нота, а не две
    off(BASS, 45);
    rms(9600);
    CHECK(s.slot[0].amp.gate == 0);
}

TEST(test_live_short_note_still_ends)
{
    live();
    on(BASS, 45, 100);
    rms(480);
    off(BASS, 45);
    rms(4800);
    CHECK(s.slot[0].amp.gate == 0);
}

TEST(test_live_fast_repeat_retriggers)
{
    // Настоящий быстрый повтор (дальше окна склейки) играет новую ноту.
    live();
    on(BASS, 45, 100);
    rms(2880);
    off(BASS, 45);
    rms(2880);
    on(BASS, 45, 100);
    rms(1);
    CHECK(s.slot[0].fenv.value > 0.99f);
}

TEST(test_live_short_note_then_other_note_retriggers)
{
    // Короткая нота и сразу другая: не слайд, а новая нота.
    live();
    on(BASS, 45, 100);
    rms(480);
    off(BASS, 45);
    rms(480);
    on(BASS, 50, 100);
    rms(1);
    CHECK(s.slot[0].fenv.value > 0.99f);
    CHECK_NEAR(acid_synth_pitch_hz(&s), acid_midi_to_hz(50), 0.5f);
}

int main(void)
{
    RUN(test_live_silent_until_note);
    RUN(test_live_note_pitch);
    RUN(test_live_overlap_slides);
    RUN(test_live_release_returns_to_held);
    RUN(test_live_note_off_closes_gate);
    RUN(test_live_accent_velocity);
    RUN(test_live_drums);
    RUN(test_live_page_select);
    RUN(test_live_bass_page);
    RUN(test_live_transpose);
    RUN(test_live_glide_knob);
    RUN(test_live_mix_page);
    RUN(test_live_drum_page);
    RUN(test_live_snare_and_clap_notes);
    RUN(test_live_ignores_other_channels);
    RUN(test_live_stop_silences);
    RUN(test_live_transitions_no_click);
    RUN(test_live_off_restarts_track);
    RUN(test_live_seq2_plays_slot1);
    RUN(test_live_slot_knobs_independent);
    RUN(test_live_stop_silences_both);
    RUN(test_render_split_identical);
    RUN(test_live_soft_drum_hit_audible);
    RUN(test_live_bass_bounce_merged);
    RUN(test_live_short_note_still_ends);
    RUN(test_live_fast_repeat_retriggers);
    RUN(test_live_short_note_then_other_note_retriggers);
    TEST_MAIN_END();
}
