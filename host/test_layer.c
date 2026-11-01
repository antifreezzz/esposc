// Слои синтов: второй голос на нотах своего синта, страницы 10-11 (на BSP 16-17), громкость FX 13-14.
#include <math.h>
#include <string.h>

#include "acid.h"
#include "test.h"

#define SR 48000.0f
#define N 9600 // 0.2 с

static acid_synth_t a, b; // a - без слоя, b - со слоем
static float la[N], ra[N], lb[N], rb[N];

static void init2(void)
{
    acid_synth_init(&a, SR);
    acid_synth_init(&b, SR);
}

static void both_note(int ch, int on, int note)
{
    acid_midi_event_t e = {(uint8_t)(on ? ACID_MIDI_NOTE_ON : ACID_MIDI_NOTE_OFF), (uint8_t)ch, (uint8_t)note, 90};
    acid_synth_midi(&a, &e);
    acid_synth_midi(&b, &e);
}

static void render2(void)
{
    acid_synth_render_lr(&a, la, ra, N);
    acid_synth_render_lr(&b, lb, rb, N);
}

static float max_diff(void)
{
    float d = 0.0f;
    for (int i = 0; i < N; i++) {
        float x = fabsf(lb[i] - la[i]) + fabsf(rb[i] - ra[i]);
        d = x > d ? x : d;
    }
    return d;
}

TEST(test_layer_off_by_default)
{
    init2();
    CHECK(acid_synth_knob(&a, ACID_PAGE_LAYER1, 1) == ACID_LAYER_OFF);
    CHECK(acid_synth_knob(&a, ACID_PAGE_LAYER1 + 1, 1) == ACID_LAYER_OFF);
    acid_synth_set_knob(&b, 8, 12, 127); // громкость выключенного слоя ничего не меняет
    both_note(4, 1, 45);
    render2();
    CHECK(max_diff() == 0.0f);
}

TEST(test_layer_plays_parent_notes)
{
    init2();
    acid_synth_set_knob(&b, ACID_PAGE_LAYER1, 1, 0);  // слой синта 1 - 303
    acid_synth_set_knob(&b, ACID_PAGE_LAYER1, 9, 76); // TRANS +12
    both_note(4, 1, 45);                              // A2, 110 Гц: слой на 220
    render2();
    render2();
    CHECK(max_diff() > 0.05f); // слой слышен
    CHECK(fabsf(exp2f(b.layer[0].pitch_log2) - 220.0f) < 0.5f); // на октаву выше синта
    CHECK(fabsf(exp2f(b.slot[0].pitch_log2) - 110.0f) < 0.5f);
    both_note(4, 0, 45);
    for (int k = 0; k < 3; k++) {
        render2();
    }
    CHECK(max_diff() < 1e-3f);
}

TEST(test_layer2_on_slot_render)
{
    init2();
    acid_synth_set_knob(&b, ACID_PAGE_LAYER1 + 1, 1, 0);
    both_note(5, 1, 57);
    static float sa[N], sb[N];
    acid_synth_render_slot(&a, 1, sa, N);
    acid_synth_render_slot(&b, 1, sb, N);
    float d = 0.0f;
    for (int i = 0; i < N; i++) {
        d += fabsf(sb[i] - sa[i]);
    }
    CHECK(d > 1.0f); // слой 2 считается вместе с синтом 2 (ядро 0)
}

TEST(test_layer_type_switch_no_click)
{
    init2();
    acid_synth_set_knob(&b, ACID_PAGE_LAYER1, 1, 0);
    both_note(4, 1, 45);
    static const int types[] = {21, ACID_LAYER_OFF, 7, 0};
    float jump = 0.0f, prev = 0.0f;
    for (int t = 0; t < 5; t++) {
        render2();
        for (int i = 0; i < N; i++) {
            float d = lb[i] - la[i];
            jump = fabsf(d - prev) > jump ? fabsf(d - prev) : jump;
            prev = d;
        }
        if (t < 4) {
            acid_synth_set_knob(&b, ACID_PAGE_LAYER1, 1, types[t]);
        }
    }
    CHECK(jump < 0.2f);
}

TEST(test_layer_gain_fx13)
{
    init2();
    acid_synth_set_knob(&b, ACID_PAGE_LAYER1, 1, 0);
    acid_synth_set_knob(&b, 8, 12, 0);
    both_note(4, 1, 45);
    render2();
    CHECK(max_diff() < 1e-9f);
    CHECK(acid_synth_knob(&b, 8, 12) == 0 && acid_synth_knob(&a, 8, 13) == 100);
}

TEST(test_layer_follows_tonal)
{
    init2();
    acid_synth_set_knob(&b, ACID_PAGE_LAYER1, 1, 0);
    acid_synth_set_knob(&b, 9, 1, 1);  // слой тональности включён
    acid_synth_set_knob(&b, 9, 8, 67); // шаг 1: +3
    acid_midi_event_t e = {ACID_MIDI_NOTE_ON, 4, 45, 90};
    acid_synth_midi(&b, &e);
    CHECK(b.layer[0].target_log2 == b.slot[0].target_log2);
    CHECK(fabsf(b.slot[0].target_log2 - log2f(acid_midi_to_hz(48))) < 1e-5f);
}

TEST(test_layer_knob_page_readback)
{
    init2();
    acid_synth_set_knob(&a, ACID_PAGE_LAYER1, 1, 0);
    acid_synth_set_knob(&a, ACID_PAGE_LAYER1, 2, 33);
    CHECK(acid_synth_knob(&a, ACID_PAGE_LAYER1, 1) == 0 && acid_synth_knob(&a, ACID_PAGE_LAYER1, 2) == 33);
    CHECK(acid_synth_knob(&a, 6, 2) != 33); // у синта своя память
    acid_synth_set_knob(&a, 6, 1, ACID_LAYER_OFF); // у синта OFF нет
    CHECK(acid_synth_knob(&a, 6, 1) == 0);
}

// Память ручек версии 2 (до слоёв): ручки синтов на месте, слои OFF.
TEST(test_blob_v2_imported)
{
    init2();
    acid_synth_set_knob(&a, 6, 2, 11);
    acid_synth_set_knob(&a, 7, 1, 3);
    static uint8_t v2[4 + ACID_SLOTS * (1 + ACID_TYPES * ACID_KNOBS) + ACID_TONAL_KNOBS];
    uint8_t *p = v2;
    memcpy(p, "AS5\x02", 4);
    p += 4;
    for (int k = 0; k < ACID_SLOTS; k++) {
        *p++ = (uint8_t)a.slot[k].sel_type;
        memcpy(p, a.slot[k].knob, sizeof a.slot[k].knob);
        p += sizeof a.slot[k].knob;
    }
    memcpy(p, a.tonal.knob, ACID_TONAL_KNOBS);
    CHECK(acid_synth_import(&b, v2, sizeof v2) == 1);
    CHECK(acid_synth_knob(&b, 6, 2) == 11 && acid_synth_knob(&b, 7, 1) == 3);
    CHECK(acid_synth_knob(&b, ACID_PAGE_LAYER1, 1) == ACID_LAYER_OFF);
}

TEST(test_blob_v3_roundtrip)
{
    init2();
    acid_synth_set_knob(&a, ACID_PAGE_LAYER1 + 1, 1, 7);
    acid_synth_set_knob(&a, ACID_PAGE_LAYER1 + 1, 3, 99);
    static uint8_t blob[ACID_SYNTH_BLOB];
    CHECK(acid_synth_export(&a, blob, sizeof blob) == ACID_SYNTH_BLOB);
    CHECK(acid_synth_import(&b, blob, sizeof blob) == 1);
    CHECK(acid_synth_knob(&b, ACID_PAGE_LAYER1 + 1, 1) == 7 && acid_synth_knob(&b, ACID_PAGE_LAYER1 + 1, 3) == 99);
    CHECK(acid_synth_knob(&b, ACID_PAGE_LAYER1, 1) == ACID_LAYER_OFF);
}

static void load_n(acid_synth_t *s, float load, int n)
{
    for (int i = 0; i < n; i++) {
        acid_synth_set_load(s, load);
    }
}

TEST(test_guard_steps)
{
    init2();
    load_n(&b, 1.0f, 30);
    CHECK(b.tight == 1); // сначала перкуссия и тарелки
    load_n(&b, 1.0f, ACID_LOAD_STEP);
    CHECK(b.tight == 2); // не помогло - слои
    load_n(&b, 0.5f, ACID_LOAD_CALM + 10);
    CHECK(b.tight == 1);
    load_n(&b, 0.5f, ACID_LOAD_CALM);
    CHECK(b.tight == 0);
}

// Выход слоя (b минус a) и его худший скачок между соседними сэмплами.
static float layer_jump(float *last_peak)
{
    render2();
    float jump = 0.0f, pk = 0.0f;
    for (int i = 1; i < N; i++) {
        float d0 = lb[i - 1] - la[i - 1], d1 = lb[i] - la[i];
        jump = fabsf(d1 - d0) > jump ? fabsf(d1 - d0) : jump;
        if (i > N - 480) {
            pk = fabsf(d1) > pk ? fabsf(d1) : pk;
        }
    }
    *last_peak = pk;
    return jump;
}

TEST(test_guard_ducks_layers)
{
    init2();
    acid_synth_set_knob(&b, ACID_PAGE_LAYER1, 1, 0);
    acid_synth_set_knob(&b, ACID_PAGE_LAYER1, 9, 76);
    both_note(4, 1, 45);
    float pk, base = layer_jump(&pk);
    CHECK(pk > 0.05f);
    load_n(&b, 1.0f, 30 + ACID_LOAD_STEP);
    CHECK(b.tight == 2);
    float j = layer_jump(&pk);
    CHECK(pk == 0.0f);          // слой затих и не считается
    CHECK(j < base + 0.05f);    // без щелчка
    load_n(&b, 0.5f, ACID_LOAD_CALM + 10);
    CHECK(b.tight == 1);
    j = layer_jump(&pk);
    CHECK(pk > 0.05f);          // вернулся
    CHECK(j < base + 0.05f);
}

int main(void)
{
    RUN(test_guard_steps);
    RUN(test_guard_ducks_layers);
    RUN(test_layer_off_by_default);
    RUN(test_layer_plays_parent_notes);
    RUN(test_layer2_on_slot_render);
    RUN(test_layer_type_switch_no_click);
    RUN(test_layer_gain_fx13);
    RUN(test_layer_follows_tonal);
    RUN(test_layer_knob_page_readback);
    RUN(test_blob_v2_imported);
    RUN(test_blob_v3_roundtrip);
    TEST_MAIN_END();
}
