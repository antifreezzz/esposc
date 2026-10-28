#include "acid.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f
#define BASS 4  // MIDI канал 5
#define DRUMS 1 // MIDI канал 2

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
    float f1 = s.fenv.value;
    CHECK(f1 < 0.9f);
    on(BASS, 57, 100);
    rms(1);
    CHECK(s.fenv.value <= f1); // огибающая не перезапущена
    rms(480);
    float hz = acid_synth_pitch_hz(&s);
    CHECK(hz > 111.0f && hz < 218.0f); // ещё ползёт
    rms(24000); // глайд с постоянной 60 мс: за 0.5 с доходит
    CHECK_NEAR(acid_synth_pitch_hz(&s), 220.0f, 1.0f);
    CHECK(s.amp.gate == 1);
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
    CHECK(s.amp.gate == 1);
    CHECK(rms(480) > 0.005f);
}

TEST(test_live_note_off_closes_gate)
{
    live();
    on(BASS, 45, 100);
    rms(2400);
    off(BASS, 45);
    rms(4800);
    CHECK(s.amp.gate == 0);
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

TEST(test_live_cc_params)
{
    live();
    cc(74, 0);
    CHECK_NEAR(s.res, 0.0f, 1e-6f);
    cc(74, 127);
    CHECK_NEAR(s.res, 0.97f, 1e-4f);
    cc(10, 0);
    CHECK_NEAR(s.cut_base, 60.0f, 0.1f);
    cc(10, 127);
    CHECK_NEAR(s.cut_base, 3000.0f, 1.0f);
    cc(93, 0);
    CHECK(s.wave == 0);
    cc(93, 127);
    CHECK(s.wave == 1);
    cc(91, 0); // мастер в ноль: тишина
    on(BASS, 45, 127);
    on(DRUMS, 36, 127);
    CHECK(rms(2400) < 1e-6f);
}

TEST(test_live_ignores_other_channels)
{
    live();
    on(5, 45, 100); // SEQ2, канал 6
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
    cc(74, 0);
    on(BASS, 45, 100);
    rms(4800);
    acid_synth_set_live(&s, 0);
    CHECK(s.arr_on == 1 && s.bar == 0 && s.cycle == 0 && s.started == 0);
    rms(4800);
    CHECK(s.bar == 0 && s.started == 1);
    CHECK(s.res > 0.5f); // резонанс снова от автоматизации, а не от ручки
    CHECK(s.kick.amp_env > 0.1f); // бочка автопилота на месте
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
    RUN(test_live_cc_params);
    RUN(test_live_ignores_other_channels);
    RUN(test_live_stop_silences);
    RUN(test_live_transitions_no_click);
    RUN(test_live_off_restarts_track);
    TEST_MAIN_END();
}
