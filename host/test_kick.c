#include "acid.h"
#include "test.h"

#include <math.h>

#define SR 48000.0f

static int rising_crossings(acid_kick_t *k, int n)
{
    float prev = acid_kick_next(k);
    int c = 0;
    for (int i = 1; i < n; i++) {
        float x = acid_kick_next(k);
        c += prev < 0.0f && x >= 0.0f;
        prev = x;
    }
    return c;
}

// Без libm и деления в рендере, но звучит как прежняя формула (sinf, деление на sr) с точностью ниже 16 бит.
TEST(test_kick_matches_reference)
{
    acid_kick_t k;
    acid_kick_init(&k, SR);
    acid_kick_trigger(&k);
    float phase = 0.0f, pitch = 1.0f, amp = 1.0f, worst = 0.0f;
    for (int i = 0; i < 48000; i++) {
        float ref = amp * sinf(2.0f * 3.14159265f * phase);
        phase += (k.low_hz + k.sweep_hz * pitch) / SR;
        phase -= phase >= 1.0f ? 1.0f : 0.0f;
        pitch *= k.pitch_coef;
        amp *= k.amp_coef;
        worst = fmaxf(worst, fabsf(acid_kick_next(&k) - ref));
    }
    CHECK(worst < 2e-5f);
}

TEST(test_kick_silent_until_trigger)
{
    acid_kick_t k;
    acid_kick_init(&k, SR);
    float worst = 0.0f;
    for (int i = 0; i < 1000; i++) {
        float a = fabsf(acid_kick_next(&k));
        worst = a > worst ? a : worst;
    }
    CHECK(worst == 0.0f);
}

TEST(test_kick_decays)
{
    acid_kick_t k;
    acid_kick_init(&k, SR);
    acid_kick_trigger(&k);
    float peak = 0.0f;
    double tail_sq = 0.0;
    for (int i = 0; i < 24000; i++) {
        float x = acid_kick_next(&k);
        if (i < 480) {
            peak = fabsf(x) > peak ? fabsf(x) : peak;
        }
        if (i >= 19200) {
            tail_sq += (double)x * x;
        }
    }
    CHECK(peak > 0.5f);
    CHECK(sqrt(tail_sq / 4800) < 0.01);
}

TEST(test_kick_pitch_drops)
{
    acid_kick_t k;
    acid_kick_init(&k, SR);
    acid_kick_trigger(&k);
    int early = rising_crossings(&k, 960);   // 0-20 мс
    for (int i = 0; i < 3840; i++) {
        acid_kick_next(&k);                  // до 100 мс
    }
    int late = rising_crossings(&k, 960);    // 100-120 мс
    CHECK(early > late);
    CHECK(late >= 0);
}

TEST(test_synth_kick_on_beats)
{
    // Бочка на долях от MIDI: на доле удар, между долями только хвост.
    static acid_synth_t s;
    acid_synth_init(&s, SR);
    acid_midi_event_t e = {.type = ACID_MIDI_NOTE_ON, .ch = 1, .d1 = 36, .d2 = 127};
    float x;
    double on_beat[16] = {0};
    for (int step = 0; step < 16; step++) {
        if (step % 4 == 0) {
            acid_synth_midi(&s, &e);
        }
        for (int n = 0; n < 5760; n++) {
            acid_synth_render(&s, &x, 1);
            if (n < 480) { // первые 10 мс шага
                on_beat[step] += (double)x * x;
            }
        }
    }
    for (int i = 0; i < 16; i++) {
        if (i % 4 == 0) {
            CHECK(on_beat[i] > 1.0);
        } else {
            CHECK(on_beat[i] * 5.0 < on_beat[i - i % 4]);
        }
    }
}

TEST(test_synth_kick_level)
{
    // Пик бочки на выходе: было ~0.37, по просьбе +10% -> ~0.41.
    static acid_synth_t s;
    acid_synth_init(&s, SR);
    acid_midi_event_t e = {.type = ACID_MIDI_NOTE_ON, .ch = 1, .d1 = 36, .d2 = 127};
    acid_synth_midi(&s, &e);
    float x, peak = 0.0f;
    for (int n = 0; n < 4800; n++) {
        acid_synth_render(&s, &x, 1);
        peak = fabsf(x) > peak ? fabsf(x) : peak;
    }
    CHECK(peak >= 0.40f);
    CHECK(peak <= 0.42f);
}

// Удар с силой: тихий - меньше щелчка (высота стартует ниже), полный - как старый триггер, плюс малый разброс
// тона и глубины от удара к удару, чтобы очередь не звучала пулемётом.
TEST(test_kick_velocity_timbre)
{
    acid_kick_t k;
    acid_kick_init(&k, SR);
    acid_kick_trigger_vel(&k, 1.0f);
    CHECK(k.pitch_env == 1.0f && k.amp_env == 1.0f && k.phase == 0.0f);
    int loud = rising_crossings(&k, 960); // первые 20 мс
    acid_kick_init(&k, SR);
    acid_kick_trigger_vel(&k, 0.3f);
    CHECK(k.pitch_env > 0.4f && k.pitch_env < 0.8f);
    int soft = rising_crossings(&k, 960);
    CHECK(soft < loud);

    acid_kick_init(&k, SR);
    float lo_min = 1e9f, lo_max = 0.0f, sw_min = 1e9f, sw_max = 0.0f;
    for (int i = 0; i < 200; i++) {
        acid_kick_trigger_vel(&k, 1.0f);
        lo_min = fminf(lo_min, k.hit_low), lo_max = fmaxf(lo_max, k.hit_low);
        sw_min = fminf(sw_min, k.hit_sweep), sw_max = fmaxf(sw_max, k.hit_sweep);
    }
    // Тон гуляет в пределах +-0.5 % (саб не плывёт), глубина щелчка - +-5 %.
    CHECK(lo_min >= k.low_hz * 0.995f && lo_max <= k.low_hz * 1.005f && lo_max - lo_min > k.low_hz * 0.004f);
    CHECK(sw_min >= k.sweep_hz * 0.95f && sw_max <= k.sweep_hz * 1.05f && sw_max - sw_min > k.sweep_hz * 0.04f);
    CHECK(k.low_hz == ACID_KICK_LOW_HZ && k.sweep_hz == ACID_KICK_SWEEP_HZ); // ручки не дрейфуют
    // Старый триггер и смена ручек - без разброса.
    acid_kick_trigger(&k);
    CHECK(k.hit_low == k.low_hz && k.hit_sweep == k.sweep_hz);
    acid_kick_trigger_vel(&k, 0.8f);
    acid_kick_set(&k, 60.0f, 200.0f, 0.03f, 0.08f);
    CHECK(k.hit_low == 60.0f && k.hit_sweep == 200.0f);
}

int main(void)
{
    RUN(test_kick_velocity_timbre);
    RUN(test_kick_matches_reference);
    RUN(test_kick_silent_until_trigger);
    RUN(test_kick_decays);
    RUN(test_kick_pitch_drops);
    RUN(test_synth_kick_on_beats);
    RUN(test_synth_kick_level);
    TEST_MAIN_END();
}
