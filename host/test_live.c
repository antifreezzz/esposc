#include "acid.h"
#include "test.h"

#include <math.h>
#include <string.h>

#define SR 48000.0f
#define BASS 4  // MIDI канал 5
#define DRUMS 1 // MIDI канал 2
#define SEQ2 5  // MIDI канал 6

static acid_synth_t s, s2;
static acid_synth_t *cur = &s; // куда идут send/on/off/cc
static float buf[48000], buf2[48000];

static void live(void)
{
    acid_synth_init(&s, SR);
    acid_synth_set_live(&s, 1);
    cur = &s;
}

// Оба экземпляра в live, команды идут в s.
static void live2(void)
{
    acid_synth_init(&s2, SR);
    acid_synth_set_live(&s2, 1);
    live();
}

static void send(uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2)
{
    acid_midi_event_t e = {.type = type, .ch = ch, .d1 = d1, .d2 = d2};
    acid_synth_midi(cur, &e);
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

// Энкодер 1: значение на экране BSP - номер страницы с нуля.
static void page(int p)
{
    cc(ENC[1], (uint8_t)p);
}

TEST(test_live_page_select)
{
    live();
    CHECK(s.page == 2); // до касания энкодера 1 - синт 1
    for (int p = 0; p < 6; p++) {
        cc(ENC[1], (uint8_t)p);
        CHECK(s.page == p);
    }
    cc(ENC[1], 6); // такой страницы нет: остаёмся
    CHECK(s.page == 5);
    cc(ENC[1], 127);
    CHECK(s.page == 5);
    cc(ENC[1], 1);
    CHECK(s.page == 1);
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
    cc(ENC[8], 1);
    CHECK(s.slot[0].wave == 1);
    cc(ENC[8], 2); // вне списка: игнор
    CHECK(s.slot[0].wave == 1);
    cc(ENC[8], 0);
    CHECK(s.slot[0].wave == 0);
}

TEST(test_live_transpose)
{
    live();
    page(2);
    cc(ENC[10], 65); // 64 - без транспозиции, 65 - +1
    CHECK(s.slot[0].transpose == 1);
    cc(ENC[10], 51); // вне -12..+12: игнор
    CHECK(s.slot[0].transpose == 1);
    cc(ENC[10], 76); // +12
    on(BASS, 45, 100);
    rms(480);
    CHECK_NEAR(acid_synth_pitch_hz(&s), 220.0f, 1.0f);
    off(BASS, 45);
    cc(ENC[10], 52); // -12
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
    cc(ENC[2], 0); // синт 2 тоже 303 (по умолчанию он FM)
    rms(960);
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
    CHECK(s.slot[0].nheld == 0 && s.slot[1].nheld == 0);
    rms(48000); // хвост Plaits (синт 2 по умолчанию FM) доигрывает сам
    CHECK(s.slot[0].amp.gate == 0);
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

#define MS(x) ((int)((x) * SR / 1000.0f))

// --- Этап 6: пампинг и DJ-фильтр (страница 0, энкодеры 10-13 = CC 18, 19, 16, 17) ---

static float rms_of(acid_synth_t *x, int n)
{
    acid_synth_render(x, buf, (size_t)n);
    double sq = 0.0;
    for (int i = 0; i < n; i++) {
        sq += (double)buf[i] * buf[i];
    }
    return (float)sqrt(sq / n);
}

TEST(test_live_pump_ghost_kick)
{
    live();
    cc(10, 0);   // страница микса
    cc(93, 0);   // бочка (энкодер 6) в нуле: мерим только синт
    cc(18, 127); // глубина
    cc(19, 64);  // возврат ~0.16 с
    on(BASS, 45, 90);
    rms(MS(200));
    float r0 = rms(MS(20));
    on(DRUMS, 36, 127);
    rms(MS(5));
    CHECK(rms(MS(10)) < 0.3f * r0);
    rms(MS(500));
    CHECK(rms(MS(20)) > 0.9f * r0);
}

TEST(test_live_pump_kick_not_ducked)
{
    live2();
    cc(10, 0);
    cc(18, 127);
    cur = &s2;
    cc(10, 0);
    cur = &s;
    on(DRUMS, 36, 127);
    cur = &s2;
    on(DRUMS, 36, 127);
    float a = rms_of(&s, MS(100));
    float b = rms_of(&s2, MS(100));
    CHECK(b > 0.0f);
    CHECK_NEAR(a / b, 1.0f, 0.01f);
}

// Одинаковые ноты синта, бочка и хэт в оба экземпляра, рендер 1 с, буферы равны.
static int play_same(void)
{
    for (int k = 0; k < 2; k++) {
        cur = k ? &s2 : &s;
        on(BASS, 45, 110);
        on(DRUMS, 36, 127);
        on(DRUMS, 42, 100);
    }
    cur = &s;
    acid_synth_render(&s, buf, 48000);
    acid_synth_render(&s2, buf2, 48000);
    return memcmp(buf, buf2, sizeof buf) == 0;
}

TEST(test_live_pump_depth0_no_effect)
{
    live2();
    cc(10, 0);
    cc(18, 0);
    CHECK(play_same());
}

TEST(test_live_fx_back_to_neutral_bitexact)
{
    live2();
    cur = &s2;
    cc(10, 0);
    cc(18, 127);
    cc(16, 20);
    acid_synth_render(&s2, buf2, (size_t)MS(100));
    cc(18, 0);
    cc(16, 64);
    acid_synth_render(&s2, buf2, (size_t)MS(200));
    cur = &s;
    acid_synth_render(&s, buf, (size_t)MS(300)); // тот же сдвиг времени
    CHECK(!acid_pump_active(&s2.pump) && !acid_djf_active(&s2.djf)); // снова прежний путь микса
    CHECK(play_same());
}

TEST(test_live_djf_cuts_highs)
{
    live2();
    cc(10, 0);
    cc(16, 0); // ФНЧ 150 Гц
    float a = 0.0f, b = 0.0f;
    for (int i = 0; i < 10; i++) {
        cur = &s;
        on(DRUMS, 42, 127);
        cur = &s2;
        on(DRUMS, 42, 127);
        a += rms_of(&s, MS(100));
        b += rms_of(&s2, MS(100));
    }
    cur = &s;
    CHECK(a < 0.2f * b);
}

TEST(test_live_djf_resonance_bounded)
{
    live();
    cc(10, 0);
    cc(16, 70); // ФВЧ у самого низа
    cc(17, 127);
    int ok = 1;
    for (int i = 0; i < 4; i++) {
        on(DRUMS, 36, 127);
        acid_synth_render(&s, buf, 24000);
        for (int j = 0; j < 24000; j++) {
            ok &= isfinite(buf[j]) && fabsf(buf[j]) <= 0.5f + 1e-6f;
        }
    }
    CHECK(ok);
}

// --- Этап 7: страница 4 (эффекты) ---

TEST(test_live_fx_delay_echo)
{
    live2();
    for (int k = 0; k < 2; k++) {
        cur = k ? &s2 : &s;
        cc(10, 4);
        if (!k) {
            cc(76, 127); // посыл синта 1 в дилэй
        }
        on(BASS, 45, 127);
    }
    acid_synth_render(&s, buf, (size_t)MS(50));
    acid_synth_render(&s2, buf2, (size_t)MS(50));
    for (int k = 0; k < 2; k++) {
        cur = k ? &s2 : &s;
        off(BASS, 45);
    }
    cur = &s;
    acid_synth_render(&s, buf, (size_t)MS(350));
    acid_synth_render(&s2, buf2, (size_t)MS(350));
    float a = rms_of(&s, MS(100));  // 400..500 мс: повтор через 3/16 (375 мс) от ноты
    float b = rms_of(&s2, MS(100));
    CHECK(a > 5.0f * b + 1e-4f);
}

TEST(test_live_clock_sets_delay_time)
{
    live();
    for (int i = 0; i < 48; i++) {
        send(ACID_MIDI_CLOCK, 0, 0, 0);
        acid_synth_render(&s, buf, 1200); // 1200 сэмплов на тик = 100 BPM
    }
    CHECK_NEAR(acid_delay_time_samples(&s.delay), 0.75f * 28800.0f, 1.0f);
}

TEST(test_live_crush_changes_sound)
{
    live2();
    for (int k = 0; k < 2; k++) {
        cur = k ? &s2 : &s;
        cc(10, 4);
        if (!k) {
            cc(74, 127); // 3 бита
        }
        on(BASS, 45, 127);
    }
    cur = &s;
    acid_synth_render(&s, buf, 24000);
    acid_synth_render(&s2, buf2, 24000);
    double d = 0.0, e = 0.0;
    for (int i = 0; i < 24000; i++) {
        d += (double)(buf[i] - buf2[i]) * (buf[i] - buf2[i]);
        e += (double)buf2[i] * buf2[i];
    }
    CHECK(e > 0.0 && d > 0.01 * e);
}

TEST(test_live_stutter_repeats)
{
    live();
    cc(10, 4);
    on(BASS, 45, 127);
    on(DRUMS, 42, 127);
    acid_synth_render(&s, buf, (size_t)MS(100));
    cc(18, 4); // 1/32 при 120 BPM = 3000 сэмплов
    acid_synth_render(&s, buf, 12000);
    float md = 0.0f;
    for (int i = 6200; i < 12000; i++) {
        md = fabsf(buf[i] - buf[i - 3000]) > md ? fabsf(buf[i] - buf[i - 3000]) : md;
    }
    CHECK(md < 1e-6f);
}

TEST(test_live_fx_page_back_to_neutral)
{
    live2();
    cur = &s2;
    cc(10, 4);
    cc(74, 64);
    cc(76, 127);
    cc(18, 2);
    acid_synth_render(&s2, buf2, (size_t)MS(100));
    cc(74, 0);
    cc(76, 0);
    cc(18, 0);
    acid_synth_render(&s2, buf2, 40000);
    cur = &s;
    acid_synth_render(&s, buf, (size_t)MS(100));
    acid_synth_render(&s, buf, 40000);
    CHECK(!acid_crush_active(&s2.crush) && !acid_delay_active(&s2.delay) && !acid_stutter_active(&s2.stutter));
    CHECK(play_same());
}

// --- Слой тональности (страница 5) ---

static int tick_n; // тиков с последнего Start

// n тиков клока по 250 сэмплов (120 BPM).
static void clk(int n)
{
    for (int i = 0; i < n; i++) {
        send(ACID_MIDI_CLOCK, 0, 0, 0);
        rms(250);
        tick_n++;
    }
}

static void start(void)
{
    send(ACID_MIDI_START, 0, 0, 0);
    tick_n = 0;
}

static float hz(int k)
{
    return exp2f(s.slot[k].pitch_log2);
}

static float note_hz(int n)
{
    return 440.0f * exp2f((float)(n - 69) / 12.0f);
}

// Слой на 1 такт, шаг 2 = +5 полутонов, звучащая нота по режиму mode.
static void tonal_setup(int mode)
{
    live();
    cc(10, 5);
    cc(74, 1);   // длина шага 1 такт
    cc(93, mode);
    cc(18, 69);  // шаг 2 (энкодер 10) = +5
    start();
}

TEST(test_live_tonal_page)
{
    live();
    cc(10, 5);
    CHECK(s.page == 5);
    cc(74, 1);
    CHECK(s.tonal.len == 1);
    cc(10, 4);
    cc(74, 3);
    CHECK(s.tonal.len == 1); // на странице эффектов CC 74 - хруст
    CHECK(s.crush.bits_knob == 3);
}

TEST(test_live_tonal_shifts_next_bar)
{
    tonal_setup(0);
    clk(1);
    on(BASS, 45, 100);
    rms(480);
    CHECK_NEAR(hz(0), 110.0f, 0.5f);
    off(BASS, 45);
    clk(96); // граница такта 2
    on(BASS, 45, 100);
    rms(480);
    CHECK_NEAR(hz(0), note_hz(50), 0.5f);
    off(BASS, 45);
}

TEST(test_live_tonal_held_modes)
{
    for (int mode = 0; mode <= 2; mode++) {
        tonal_setup(mode);
        clk(1);
        on(BASS, 45, 100);
        clk(95);
        clk(1); // граница, нота держится
        if (mode == 0) {
            CHECK_NEAR(hz(0), 110.0f, 0.5f);
        } else if (mode == 1) {
            CHECK(s.slot[0].gliding);
            CHECK_NEAR(exp2f(s.slot[0].target_log2), note_hz(50), 0.5f);
            CHECK(hz(0) < note_hz(50) - 0.5f); // ещё едет
        } else {
            CHECK_NEAR(hz(0), note_hz(50), 0.5f);
        }
    }
}

TEST(test_live_tonal_early_note)
{
    tonal_setup(0);
    clk(96);
    rms(10);
    on(BASS, 45, 100); // за ~5 мс до границы
    clk(1);
    CHECK_NEAR(hz(0), note_hz(50), 0.5f);
}

TEST(test_live_tonal_release_no_stuck)
{
    tonal_setup(0);
    clk(1);
    on(BASS, 45, 100);
    clk(96);
    off(BASS, 45);
    rms(4800);
    CHECK(s.slot[0].nheld == 0);
    on(BASS, 40, 100);
    on(BASS, 47, 100);
    clk(96); // шаг 3 = 0
    clk(96); // шаг 4 = 0, затем по кругу шаг 1
    clk(96); // шаг 1 = 0
    clk(96); // шаг 2 = +5
    off(BASS, 47);
    rms(4800);
    CHECK_NEAR(exp2f(s.slot[0].target_log2), note_hz(45), 0.5f);
    off(BASS, 40);
    rms(4800);
    CHECK(s.slot[0].nheld == 0);
}

TEST(test_live_tonal_target)
{
    tonal_setup(2);
    cc(77, 2); // только синт 2
    clk(97);
    on(BASS, 45, 100);
    on(SEQ2, 45, 100);
    rms(480);
    CHECK_NEAR(hz(0), 110.0f, 0.5f);
    CHECK_NEAR(hz(1), note_hz(50), 0.5f);
}

TEST(test_live_tonal_continue)
{
    tonal_setup(0);
    clk(97);
    CHECK(s.tonal.step == 1);
    send(ACID_MIDI_STOP, 0, 0, 0);
    send(ACID_MIDI_CONTINUE, 0, 0, 0);
    CHECK(s.tonal.step == 1);
    start();
    CHECK(s.tonal.step == 0);
}

// Слой выключен (длина 0), шаги выставлены: звук бит в бит как без страницы 5.
// Только синт 1: у Plaits общий на все экземпляры генератор случайных чисел (stmlib::Random),
// два экземпляра с синтом 2 бит в бит не совпадут и без слоя.
TEST(test_live_tonal_off_bitexact)
{
    live2();
    cur = &s2;
    cc(10, 5);
    cc(114, 70);
    cc(18, 60);
    for (int pass = 0; pass < 2; pass++) {
        cur = pass ? &s2 : &s;
        send(ACID_MIDI_START, 0, 0, 0);
    }
    int same = 1;
    for (int b = 0; b < 300; b++) {
        for (int pass = 0; pass < 2; pass++) {
            cur = pass ? &s2 : &s;
            send(ACID_MIDI_CLOCK, 0, 0, 0);
            if (b % 24 == 0) {
                on(BASS, 45 + (b / 24) % 5, 100);
            }
            if (b % 24 == 12) {
                off(BASS, 45 + (b / 24) % 5);
            }
        }
        acid_synth_render(&s, buf, 250);
        acid_synth_render(&s2, buf2, 250);
        same &= memcmp(buf, buf2, 250 * sizeof buf[0]) == 0;
    }
    cur = &s;
    CHECK(same);
}

static float buf_l[48000], buf_r[48000];

static void render_lr(int n)
{
    acid_synth_render_lr(&s, buf_l, buf_r, (size_t)n);
}

static float chan_peak(const float *x, int n)
{
    float p = 0.0f;
    for (int i = 0; i < n; i++) {
        p = fabsf(x[i]) > p ? fabsf(x[i]) : p;
    }
    return p;
}

static float chan_rms(const float *x, int n)
{
    double sq = 0.0;
    for (int i = 0; i < n; i++) {
        sq += (double)x[i] * x[i];
    }
    return (float)sqrt(sq / n);
}

TEST(test_live_hat_pan_left)
{
    live();
    cc(10, 1);  // страница ударки
    cc(91, 0);  // энкодер 14: хэты влево до края
    on(DRUMS, 42, 127);
    render_lr(2400);
    CHECK(chan_peak(buf_l, 2400) > 0.01f);
    CHECK(chan_peak(buf_r, 2400) < 1e-6f);
}

TEST(test_live_synth_center_equal)
{
    live();
    on(BASS, 45, 100);
    render_lr(4800);
    int diff = 0;
    for (int i = 0; i < 4800; i++) {
        diff += buf_l[i] != buf_r[i];
    }
    CHECK(chan_peak(buf_l, 4800) > 0.01f);
    CHECK(diff == 0);
}

TEST(test_live_synth_pan_right)
{
    live();
    cc(10, 0);   // страница микса
    cc(91, 127); // энкодер 14: синт 1 вправо до края
    on(BASS, 45, 100);
    render_lr(4800);
    CHECK(chan_peak(buf_r, 4800) > 0.01f);
    CHECK(chan_peak(buf_l, 4800) < 1e-6f);
}

TEST(test_live_synth2_pan_left)
{
    live();
    cc(10, 0);
    cc(79, 0); // энкодер 15: синт 2 влево до края
    on(SEQ2, 57, 100);
    render_lr(9600);
    CHECK(chan_peak(buf_l, 9600) > 0.001f);
    CHECK(chan_peak(buf_r, 9600) < 1e-6f);
}

TEST(test_live_drum_pan_defaults)
{
    live();
    on(DRUMS, 42, 127); // хэты по умолчанию 80: правее
    render_lr(2400);
    CHECK(chan_rms(buf_r, 2400) > 1.1f * chan_rms(buf_l, 2400));
    live();
    on(DRUMS, 38, 127); // малый 64: центр
    render_lr(4800);
    int diff = 0;
    for (int i = 0; i < 4800; i++) {
        diff += buf_l[i] != buf_r[i];
    }
    CHECK(chan_peak(buf_l, 4800) > 0.01f);
    CHECK(diff == 0);
}

TEST(test_live_fx_motion_knobs)
{
    live();
    cc(10, 4);  // страница эффектов
    cc(19, 1);  // энкодер 11: пинг-понг
    cc(16, 100); // энкодер 12: ширина
    CHECK(s.delay.motion == 1);
    CHECK(s.delay.width == 100);
    cc(19, 3); // вне списка: игнор
    CHECK(s.delay.motion == 1);
}

int main(void)
{
    RUN(test_live_fx_motion_knobs);
    RUN(test_live_hat_pan_left);
    RUN(test_live_synth_center_equal);
    RUN(test_live_synth_pan_right);
    RUN(test_live_synth2_pan_left);
    RUN(test_live_drum_pan_defaults);
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
    RUN(test_live_pump_ghost_kick);
    RUN(test_live_pump_kick_not_ducked);
    RUN(test_live_pump_depth0_no_effect);
    RUN(test_live_fx_back_to_neutral_bitexact);
    RUN(test_live_djf_cuts_highs);
    RUN(test_live_djf_resonance_bounded);
    RUN(test_live_fx_delay_echo);
    RUN(test_live_clock_sets_delay_time);
    RUN(test_live_crush_changes_sound);
    RUN(test_live_stutter_repeats);
    RUN(test_live_fx_page_back_to_neutral);
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
    RUN(test_live_tonal_page);
    RUN(test_live_tonal_shifts_next_bar);
    RUN(test_live_tonal_held_modes);
    RUN(test_live_tonal_early_note);
    RUN(test_live_tonal_release_no_stuck);
    RUN(test_live_tonal_target);
    RUN(test_live_tonal_continue);
    RUN(test_live_tonal_off_bitexact);
    TEST_MAIN_END();
}
