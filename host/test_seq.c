// Встроенный секвенсер: ход по песне, ноты, гейт, акцент, слайд.
#include <string.h>

#include "acid_seq.h"
#include "test.h"

static acid_seq_t q;
static acid_tonal_t tonal;

// События одного тика (не больше 4) и общий журнал.
static acid_seq_ev_t ev[4];
static int nev;

static void setup(void)
{
    acid_seq_init(&q);
    acid_tonal_init(&tonal);
}

static int tick(int beat_step)
{
    nev = acid_seq_clock(&q, beat_step, &tonal, ev);
    return nev;
}

// Такт pat: шаг 0 - ступень deg, остальные - пауза.
static void one_note(int synth, int pat, int deg)
{
    acid_seq_set_step(&q, synth, pat, 0, (uint8_t)deg);
}

static void run(int synth)
{
    acid_seq_ev_t off[2];
    acid_seq_run(&q, synth, 1, 0, off);
}

static void row(int synth, int r, const char *cells, int rep)
{
    uint8_t c[ACID_SEQ_CELLS];
    memset(c, ACID_SEQ_EMPTY, sizeof c);
    for (int i = 0; cells[i]; i++) {
        c[i] = (uint8_t)(cells[i] - 'A');
    }
    acid_seq_set_row(&q, synth, r, c, rep);
}

// Играющий такт каждого из bars тактов (по note on на шаге 0).
static void bars_played(int bars, int *out)
{
    for (int b = 0; b < bars; b++) {
        out[b] = -1;
        for (int t = 0; t < 6 * 16; t++) {
            tick(-1);
            for (int i = 0; i < nev; i++) {
                if (ev[i].on && t == 0) {
                    out[b] = ev[i].note - 36;
                }
            }
        }
    }
}

TEST(test_song_walk)
{
    setup();
    one_note(0, 0, 0);
    one_note(0, 1, 1);
    one_note(0, 2, 2);
    row(0, 0, "AAAB", 3);
    row(0, 1, "AAAC", 1);
    run(0);
    int got[17];
    bars_played(17, got);
    static const int want[17] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 2, 0};
    for (int i = 0; i < 17; i++) {
        CHECK(got[i] == want[i]);
    }
}

TEST(test_empty_song_plays_edit)
{
    setup();
    one_note(0, 3, 5);
    acid_seq_set_edit(&q, 0, 3);
    run(0);
    CHECK(tick(-1) == 1);
    CHECK(ev[0].on && ev[0].note == 41);
}

TEST(test_empty_row_skipped)
{
    setup();
    one_note(0, 0, 0);
    one_note(0, 2, 2);
    row(0, 0, "A", 1);
    row(0, 2, "C", 1);
    run(0);
    int got[3];
    bars_played(3, got);
    CHECK(got[0] == 0 && got[1] == 2 && got[2] == 0);
}

TEST(test_gate_and_rest)
{
    setup();
    one_note(0, 0, 0);
    run(0);
    CHECK(tick(-1) == 1);
    CHECK(ev[0].on && ev[0].note == 36 && ev[0].vel == 90 && ev[0].synth == 0);
    for (int t = 1; t < 5; t++) {
        CHECK(tick(-1) == 0);
    }
    CHECK(tick(-1) == 1); // тик 5: гейт, нота звучит почти весь шаг
    CHECK(!ev[0].on && ev[0].note == 36);
    for (int t = 6; t < 12; t++) {
        CHECK(tick(-1) == 0); // шаг 1 - пауза
    }
}

TEST(test_accent)
{
    setup();
    acid_seq_set_step(&q, 0, 0, 0, 0 | ACID_SEQ_ACC);
    run(0);
    tick(-1);
    CHECK(nev == 1 && ev[0].vel == 110);
}

TEST(test_slide)
{
    setup();
    acid_seq_set_step(&q, 0, 0, 0, 0 | ACID_SEQ_SLIDE);
    acid_seq_set_step(&q, 0, 0, 1, 4);
    run(0);
    tick(-1);
    for (int t = 1; t < 6; t++) {
        CHECK(tick(-1) == 0); // со слайдом гейта нет
    }
    CHECK(tick(-1) == 2);
    CHECK(ev[0].on && ev[0].note == 40); // новая раньше: глайд
    CHECK(!ev[1].on && ev[1].note == 36);
}

TEST(test_off_matches_on_after_tonal_change)
{
    setup();
    one_note(0, 0, 0);
    run(0);
    tick(-1);
    acid_tonal_set(&tonal, 7, 5); // тоника F
    for (int t = 1; t <= 5; t++) {
        tick(-1);
    }
    CHECK(nev == 1 && !ev[0].on && ev[0].note == 36);
}

TEST(test_waits_bar)
{
    setup();
    one_note(0, 0, 0);
    acid_seq_ev_t off[2];
    acid_seq_run(&q, 0, 1, 1, off);
    CHECK(tick(5) == 0);
    CHECK(tick(-1) == 0);
    CHECK(tick(0) == 1 && ev[0].on);
}

TEST(test_stop_releases)
{
    setup();
    one_note(0, 0, 0);
    run(0);
    tick(-1);
    acid_seq_ev_t off[2];
    CHECK(acid_seq_run(&q, 0, 0, 0, off) == 1);
    CHECK(!off[0].on && off[0].note == 36);
    CHECK(acid_seq_run(&q, 0, 0, 0, off) == 0);
    CHECK(tick(-1) == 0);
}

TEST(test_edit_playing_row)
{
    setup();
    one_note(0, 0, 0);
    one_note(0, 1, 1);
    row(0, 0, "AAABBBBB", 1);
    run(0);
    int got[3];
    bars_played(3, got);
    row(0, 0, "B", 1); // сократили посреди хода: ячейка 3 пропала
    bars_played(3, got);
    CHECK(got[0] == 1 && got[1] == 1 && got[2] == 1);
    CHECK(q.t[0].c < ACID_SEQ_CELLS && q.t[0].row < ACID_SEQ_ROWS);
}

TEST(test_oct_and_synth2)
{
    setup();
    one_note(0, 0, 0);
    acid_seq_set_oct(&q, 0, 0, 1);
    one_note(1, 0, 0);
    run(0);
    run(1);
    CHECK(tick(-1) == 2);
    CHECK(ev[0].synth == 0 && ev[0].note == 48);
    CHECK(ev[1].synth == 1 && ev[1].note == 48);
    acid_seq_set_oct(&q, 0, 0, 3); // вне диапазона - игнор
    CHECK(q.t[0].oct[0] == 1);
}

TEST(test_tie_same_note)
{
    setup();
    acid_seq_set_step(&q, 0, 0, 0, 0 | ACID_SEQ_SLIDE);
    acid_seq_set_step(&q, 0, 0, 1, 0);
    run(0);
    tick(-1);
    for (int t = 1; t < 6; t++) {
        tick(-1);
    }
    CHECK(tick(-1) == 0); // та же нота со слайдом - тянется
    for (int t = 1; t < 5; t++) {
        tick(-1);
    }
    CHECK(tick(-1) == 1 && !ev[0].on);
}

TEST(test_rewind_no_slide_artifact)
{
    setup();
    acid_seq_set_step(&q, 0, 0, 0, 0 | ACID_SEQ_SLIDE);
    acid_seq_set_step(&q, 0, 0, 15, 2 | ACID_SEQ_SLIDE);
    run(0);
    tick(-1);
    acid_seq_rewind(&q);
    CHECK(tick(-1) == 2); // звучащая гасится раньше новой: без глайда
    CHECK(!ev[0].on && ev[0].note == 36);
    CHECK(ev[1].on && ev[1].note == 36);
}

TEST(test_blob_roundtrip)
{
    setup();
    acid_seq_set_step(&q, 1, 7, 15, 3 | ACID_SEQ_ACC);
    acid_seq_set_oct(&q, 0, 2, -2);
    acid_seq_set_scale(&q, 1, 6, 8);
    row(1, 4, "HAB", 16);
    acid_seq_set_edit(&q, 1, 5);
    acid_seq_set_bpm(&q, 140);
    run(1);
    static uint8_t blob[ACID_SEQ_BLOB];
    CHECK(acid_seq_export(&q, blob, sizeof blob) == ACID_SEQ_BLOB);
    acid_seq_t q2;
    acid_seq_init(&q2);
    CHECK(acid_seq_import(&q2, blob, ACID_SEQ_BLOB) == 1);
    CHECK(q2.bpm == 140);
    for (int s = 0; s < ACID_SEQ_SYNTHS; s++) {
        CHECK(memcmp(q2.t[s].pat, q.t[s].pat, sizeof q.t[s].pat) == 0);
        CHECK(memcmp(q2.t[s].oct, q.t[s].oct, sizeof q.t[s].oct) == 0);
        CHECK(memcmp(q2.t[s].scale, q.t[s].scale, sizeof q.t[s].scale) == 0);
        CHECK(memcmp(q2.t[s].cell, q.t[s].cell, sizeof q.t[s].cell) == 0);
        CHECK(memcmp(q2.t[s].rep, q.t[s].rep, sizeof q.t[s].rep) == 0);
        CHECK(q2.t[s].run == q.t[s].run && q2.t[s].edit == q.t[s].edit);
    }
    CHECK(q2.t[1].sounding == -1);
}

TEST(test_step_octave_up)
{
    setup();
    acid_seq_set_step(&q, 0, 0, 0, 2 | ACID_SEQ_UP);
    CHECK(q.t[0].pat[0][0] == (2 | ACID_SEQ_UP));
    run(0);
    CHECK(tick(-1) == 1 && ev[0].note == 38 + 12);
}

TEST(test_pattern_scale)
{
    setup();
    acid_seq_set_step(&q, 0, 0, 0, 2);
    acid_seq_set_step(&q, 0, 1, 0, 2);
    acid_seq_set_scale(&q, 0, 0, 1 + 2); // такт A - минор
    CHECK(q.t[0].scale[0] == 3);
    acid_seq_set_scale(&q, 0, 0, ACID_SEQ_SCALES); // вне - игнор
    CHECK(q.t[0].scale[0] == 3);
    acid_tonal_set(&tonal, 6, 1); // страница 9: мажор
    row(0, 0, "AB", 1);
    run(0);
    int got[2];
    bars_played(2, got);
    CHECK(got[0] == 3); // A: свой минор
    CHECK(got[1] == 4); // B: лад страницы 9 (TONAL - умолчание)
}

TEST(test_blob_v1_imports)
{
    // Blob первой версии (без ладов): такты на месте, лад - TONAL.
    static uint8_t v1[4 + 2 * (128 + 8 + 64 + 8 + 2) + 1];
    memset(v1, 0, sizeof v1);
    memcpy(v1, (const uint8_t[]){'S', 'Q', '1', 1}, 4);
    for (int s = 0; s < 2; s++) {
        uint8_t *t = v1 + 4 + s * (128 + 8 + 64 + 8 + 2);
        memset(t, ACID_SEQ_REST, 128);
        memset(t + 136, ACID_SEQ_EMPTY, 64);
        memset(t + 200, 1, 8);
    }
    v1[4 + 5] = 7; // синт 1, такт A, шаг 5
    v1[sizeof v1 - 1] = 133;
    acid_seq_t q2;
    acid_seq_init(&q2);
    q2.t[0].scale[0] = 4;
    CHECK(acid_seq_import(&q2, v1, sizeof v1) == 1);
    CHECK(q2.t[0].pat[0][5] == 7 && q2.bpm == 133);
    CHECK(q2.t[0].scale[0] == 0);
}

TEST(test_blob_rejects_foreign)
{
    setup();
    static uint8_t blob[ACID_SEQ_BLOB];
    acid_seq_export(&q, blob, sizeof blob);
    blob[0] ^= 1;
    acid_seq_t q2;
    acid_seq_init(&q2);
    q2.bpm = 77;
    CHECK(acid_seq_import(&q2, blob, ACID_SEQ_BLOB) == 0);
    CHECK(q2.bpm == 77);
    blob[0] ^= 1;
    CHECK(acid_seq_import(&q2, blob, ACID_SEQ_BLOB - 1) == 0);
    CHECK(acid_seq_export(&q, blob, ACID_SEQ_BLOB - 1) == 0);
}

TEST(test_clk_rate)
{
    acid_seq_clk_t c;
    acid_seq_clk_init(&c);
    int n = 0;
    for (int i = 0; i < 48000 / 32; i++) {
        n += acid_seq_clk_run(&c, 120, 32, 48000);
    }
    CHECK(n == 48); // 120 BPM: 2 доли в секунду по 24 тика
    acid_seq_clk_init(&c);
    n = 0;
    for (int i = 0; i < 48000 * 10 / 32; i++) {
        n += acid_seq_clk_run(&c, 137, 32, 48000);
    }
    CHECK(n == 548); // остаток копится, без дрейфа
}

TEST(test_clk_ext_mutes)
{
    acid_seq_clk_t c;
    acid_seq_clk_init(&c);
    acid_seq_clk_ext(&c, 48000);
    int n = 0;
    for (int i = 0; i < 24000 / 32; i++) {
        n += acid_seq_clk_run(&c, 120, 32, 48000);
    }
    CHECK(n == 0);
    for (int i = 0; i < 24000 / 32; i++) {
        n += acid_seq_clk_run(&c, 120, 32, 48000);
    }
    CHECK(n == 24); // 0.5 с при 120 BPM
}

int main(void)
{
    RUN(test_song_walk);
    RUN(test_empty_song_plays_edit);
    RUN(test_empty_row_skipped);
    RUN(test_gate_and_rest);
    RUN(test_accent);
    RUN(test_slide);
    RUN(test_off_matches_on_after_tonal_change);
    RUN(test_waits_bar);
    RUN(test_stop_releases);
    RUN(test_edit_playing_row);
    RUN(test_oct_and_synth2);
    RUN(test_tie_same_note);
    RUN(test_rewind_no_slide_artifact);
    RUN(test_blob_roundtrip);
    RUN(test_step_octave_up);
    RUN(test_pattern_scale);
    RUN(test_blob_v1_imports);
    RUN(test_blob_rejects_foreign);
    RUN(test_clk_rate);
    RUN(test_clk_ext_mutes);
    TEST_MAIN_END();
}
