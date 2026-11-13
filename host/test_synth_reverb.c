// Интеграция ревёрба: посылы, сухой путь, память ручек и защита.
#include "acid.h"
#include "acid_reverb.h"
#include "test.h"

#include <string.h>

#define SR 48000.0f
#define CH ACID_SPLIT_MAX

static acid_synth_t a, b;

static void note(acid_synth_t *s, int ch, int n)
{
    acid_midi_event_t ev = {.type = ACID_MIDI_NOTE_ON, .ch = ch, .d1 = n, .d2 = 110};
    acid_synth_midi(s, &ev);
}

static int play_differs(acid_synth_t *dry, acid_synth_t *wet, int ch, int n)
{
    float dl[CH], dr[CH], wl[CH], wr[CH];
    int different = 0;
    note(dry, ch, n);
    note(wet, ch, n);
    for (int k = 0; k < 32; k++) {
        acid_synth_render_lr(dry, dl, dr, CH);
        acid_synth_render_lr(wet, wl, wr, CH);
        different |= memcmp(dl, wl, sizeof dl) != 0 || memcmp(dr, wr, sizeof dr) != 0;
    }
    return different;
}

static void only_send(acid_synth_t *s, int enc)
{
    acid_synth_set_knob(s, 8, 15, 127);
    for (int e = 0; e < 3; e++) {
        acid_synth_set_knob(s, ACID_PAGE_REVERB, e + 1, e + 1 == enc ? 64 : 0);
    }
}

TEST(test_reverb_defaults_and_off_bit_exact)
{
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    acid_synth_init(&a, SR);
    acid_synth_init(&b, SR);
    static const int def[ACID_REVERB_KNOBS] = {64, 64, 64, 64, 50, 127};
    for (int e = 0; e < ACID_REVERB_KNOBS; e++) {
        CHECK(acid_synth_knob(&a, ACID_PAGE_REVERB, e + 1) == def[e]);
    }
    CHECK(acid_synth_knob(&a, 8, 15) == 0);
    CHECK(acid_reverb_active(&a.reverb) == 0);
    CHECK(!play_differs(&a, &b, 1, 42));
    acid_synth_init(&a, SR);
    acid_synth_init(&b, SR);
    acid_synth_set_knob(&b, 8, 15, 127);
    CHECK(play_differs(&a, &b, 1, 42));
    acid_synth_set_knob(&b, 8, 15, 0);
    CHECK(acid_reverb_active(&b.reverb) == 0);
}

TEST(test_reverb_kick_not_sent)
{
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    acid_synth_init(&a, SR);
    acid_synth_init(&b, SR);
    acid_synth_set_knob(&b, 8, 15, 127);
    CHECK(!play_differs(&a, &b, 1, 36));
    CHECK(acid_reverb_active(&b.reverb) == 0);
}

TEST(test_reverb_each_send)
{
    static const int ch[] = {4, 5, 1};
    static const int note_no[] = {48, 55, 42};
    for (int e = 0; e < 3; e++) {
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        acid_synth_init(&a, SR);
        acid_synth_init(&b, SR);
        only_send(&b, e + 1);
        CHECK(play_differs(&a, &b, ch[e], note_no[e]));
        CHECK(acid_reverb_active(&b.reverb));
    }
}

TEST(test_reverb_blob_import_and_roundtrip)
{
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    acid_synth_init(&a, SR);
    acid_synth_init(&b, SR);
    static const int val[ACID_REVERB_KNOBS] = {11, 22, 33, 77, 88, 99};
    for (int e = 0; e < ACID_REVERB_KNOBS; e++) {
        acid_synth_set_knob(&a, ACID_PAGE_REVERB, e + 1, val[e]);
    }
    uint8_t blob[ACID_SYNTH_BLOB];
    CHECK(acid_synth_export(&a, blob, sizeof blob) == ACID_SYNTH_BLOB);
    CHECK(blob[3] == 10);
    CHECK(acid_synth_import(&b, blob, sizeof blob));
    for (int e = 0; e < ACID_REVERB_KNOBS; e++) {
        CHECK(acid_synth_knob(&b, ACID_PAGE_REVERB, e + 1) == val[e]);
    }
    blob[3] = 5;
    acid_synth_init(&b, SR);
    CHECK(acid_synth_import(&b, blob, ACID_SYNTH_BLOB_V5));
    static const int def[ACID_REVERB_KNOBS] = {64, 64, 64, 64, 50, 127};
    for (int e = 0; e < ACID_REVERB_KNOBS; e++) {
        CHECK(acid_synth_knob(&b, ACID_PAGE_REVERB, e + 1) == def[e]);
    }
    CHECK(acid_synth_export(&b, blob, sizeof blob) == ACID_SYNTH_BLOB);
    CHECK(blob[3] == 10);
    // Версия 7: пять ручек, ширины нет - полная.
    acid_synth_init(&a, SR);
    for (int e = 0; e < ACID_REVERB_KNOBS; e++) {
        acid_synth_set_knob(&a, ACID_PAGE_REVERB, e + 1, val[e]);
    }
    CHECK(acid_synth_export(&a, blob, sizeof blob) == ACID_SYNTH_BLOB);
    blob[3] = 7;
    acid_synth_init(&b, SR);
    CHECK(acid_synth_import(&b, blob, ACID_SYNTH_BLOB_V7));
    for (int e = 0; e < 5; e++) {
        CHECK(acid_synth_knob(&b, ACID_PAGE_REVERB, e + 1) == val[e]);
    }
    CHECK(acid_synth_knob(&b, ACID_PAGE_REVERB, 6) == 127);
    // Версия 6 (сборка одного дня): wet лежал на месте LYR HALF (FX 15). Переезжает на FX 16, LYR HALF выключен.
    acid_synth_init(&a, SR);
    acid_synth_set_knob(&a, 8, 14, 0);
    acid_synth_set_knob(&a, 8, 15, 0);
    CHECK(acid_synth_export(&a, blob, sizeof blob) == ACID_SYNTH_BLOB);
    blob[3] = 6;
    blob[ACID_SYNTH_BLOB_V4 + 6 * ACID_KNOBS + 14] = 90; // эффекты - седьмая из сохраняемых страниц
    acid_synth_init(&b, SR);
    CHECK(acid_synth_import(&b, blob, ACID_SYNTH_BLOB_V7));
    CHECK(acid_synth_knob(&b, 8, 15) == 90);
    CHECK(acid_synth_knob(&b, 8, 14) == 0 && !b.layer_half);
}

// Ширина: wet = разница с сухим синтом. На 0 оба канала wet одинаковы (моно), на 127 - как раньше, разные.
static float wet_side(int width)
{
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    acid_synth_init(&a, SR);
    acid_synth_init(&b, SR);
    acid_synth_set_knob(&b, 8, 15, 127);
    acid_synth_set_knob(&b, ACID_PAGE_REVERB, 6, width);
    note(&a, 4, 48);
    note(&b, 4, 48);
    float dl[CH], dr[CH], wl[CH], wr[CH];
    double side = 0.0, mid = 0.0;
    for (int k = 0; k < 200; k++) {
        acid_synth_render_lr(&a, dl, dr, CH);
        acid_synth_render_lr(&b, wl, wr, CH);
        for (int i = 0; i < CH; i++) {
            double l = wl[i] - dl[i], r = wr[i] - dr[i];
            side += (l - r) * (l - r);
            mid += (l + r) * (l + r);
        }
    }
    CHECK(mid > 1e-6);
    return (float)(side / mid);
}

TEST(test_reverb_width)
{
    float full = wet_side(127), half = wet_side(64), mono = wet_side(0);
    CHECK(full > 0.1f);
    CHECK(mono < 1e-9f);
    CHECK(half > 0.15f * full && half < 0.4f * full); // сторона вдвое тише: энергия вчетверо
}

TEST(test_reverb_load_duck_and_sleep)
{
    memset(&a, 0, sizeof a);
    acid_synth_init(&a, SR);
    acid_synth_set_knob(&a, 8, 15, 127);
    note(&a, 1, 42);
    float l[CH], r[CH];
    for (int k = 0; k < 8; k++) {
        acid_synth_render_lr(&a, l, r, CH);
    }
    CHECK(acid_reverb_active(&a.reverb));
    a.tight = 3;
    for (int k = 0; k < 8; k++) {
        acid_synth_render_lr(&a, l, r, CH);
    }
    CHECK(a.rv_duck == 0.0f);
    acid_reverb_t frozen = a.reverb;
    for (int k = 0; k < 4; k++) {
        acid_synth_render_lr(&a, l, r, CH);
    }
    CHECK(memcmp(&frozen, &a.reverb, sizeof frozen) == 0);
    a.tight = 0;
    acid_synth_render_lr(&a, l, r, CH);
    CHECK(a.rv_duck > 0.0f && a.rv_duck < 1.0f);
    CHECK(acid_reverb_active(&a.reverb));
}

int main(void)
{
    RUN(test_reverb_defaults_and_off_bit_exact);
    RUN(test_reverb_kick_not_sent);
    RUN(test_reverb_each_send);
    RUN(test_reverb_blob_import_and_roundtrip);
    RUN(test_reverb_width);
    RUN(test_reverb_load_duck_and_sleep);
    TEST_MAIN_END();
}
