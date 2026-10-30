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

int main(void)
{
    RUN(test_kick_silent_until_trigger);
    RUN(test_kick_decays);
    RUN(test_kick_pitch_drops);
    RUN(test_synth_kick_on_beats);
    RUN(test_synth_kick_level);
    TEST_MAIN_END();
}
