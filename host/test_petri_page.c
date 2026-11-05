// Страница «Чашка» (внутренняя 13, на BSP 18) и снимок для панели в два кадра.
#include "acid.h"
#include "acid_scene.h"
#include "pl_labels.h"
#include "pl_model.h"
#include "pl_proto.h"
#include "test.h"

#include <string.h>

#define SR 48000.0f
static acid_synth_t s;
static acid_scene_t sc;
static pl_model_t m;
static float buf[4800];

TEST(snapshot_fits)
{
    CHECK(PL_PAGES == 14 && PL_PAGE_PETRI == 13 && PL_USER_FIRST == 14);
    CHECK(ACID_PAGES == 14 && ACID_PAGE_PETRI == 13);
    CHECK(sizeof(pl_snapshot_t) <= PL_PAYLOAD_MAX);
    CHECK(sizeof(pl_snapshot2_t) <= PL_PAYLOAD_MAX);
    uint8_t f[PL_FRAME_MAX];
    static pl_snapshot2_t s2;
    CHECK(pl_encode(PL_SNAPSHOT2, &s2, sizeof s2, f) == sizeof s2 + 4);
}

TEST(bsp_18_is_petri)
{
    CHECK(acid_page_from_bsp(18) == ACID_PAGE_PETRI);
    CHECK(acid_page_to_bsp(ACID_PAGE_PETRI) == 18);
    CHECK(acid_page_from_bsp(19) == -1);
    CHECK(pl_page_bsp(PL_PAGE_PETRI) == 18);
    CHECK(acid_page_from_bsp(10) == ACID_USER_FIRST); // свои страницы следом
}

TEST(petri_page_defaults_and_lists)
{
    acid_synth_init(&s, SR);
    CHECK(acid_synth_knob(&s, ACID_PAGE_PETRI, 4) == 4);  // шагов на кадр
    CHECK(acid_synth_knob(&s, ACID_PAGE_PETRI, 5) == 3);  // пожиратели
    CHECK(acid_synth_knob(&s, ACID_PAGE_PETRI, 11) == 26); // пожиратели -> срез -0.6
    acid_synth_set_knob(&s, ACID_PAGE_PETRI, 1, 5);
    acid_synth_set_knob(&s, ACID_PAGE_PETRI, 1, 6); // пресетов 6: вне списка игнор
    CHECK(acid_synth_knob(&s, ACID_PAGE_PETRI, 1) == 5);
    acid_synth_set_knob(&s, ACID_PAGE_PETRI, 5, 17);
    CHECK(acid_synth_knob(&s, ACID_PAGE_PETRI, 5) == 3);
    acid_synth_set_knob(&s, ACID_PAGE_PETRI, 11, 127);
    CHECK_NEAR(s.petri_depth[0], 1.0, 1e-6);
}

TEST(petri_freeze_knob)
{
    acid_synth_init(&s, SR);
    acid_synth_set_knob(&s, ACID_PAGE_PETRI, 14, 100);
    CHECK(s.petri[0].mt == 1.0f && s.petri[1].mt == 1.0f);
    acid_synth_set_knob(&s, ACID_PAGE_PETRI, 14, 10);
    CHECK(s.petri[0].mt == 0.0f);
}

TEST(bsp_cc_reaches_petri_page)
{
    static const uint8_t ENC[16] = {10, 74, 71, 76, 77, 93, 73, 75, 114, 18, 19, 16, 17, 91, 79, 72};
    acid_synth_init(&s, SR);
    acid_midi_event_t e = {.type = ACID_MIDI_CC, .ch = 1, .d1 = ENC[0], .d2 = 18};
    acid_synth_midi(&s, &e);
    CHECK(s.page == ACID_PAGE_PETRI);
    e.d1 = ENC[4];
    e.d2 = 6;
    acid_synth_midi(&s, &e);
    CHECK(acid_synth_knob(&s, ACID_PAGE_PETRI, 4) == 6);
    acid_synth_render(&s, buf, 4800);
}

// Сцены версии 3 (13 страниц) после прошивки: страницы на месте, «Чашка» не задана.
TEST(scene_imports_13_pages)
{
    const size_t user = sizeof sc.user;
    static uint8_t blob[4 + 1 + ACID_SCENES * 13 * ACID_KNOBS + sizeof sc.user];
    memset(blob, 0xFF, sizeof blob);
    memcpy(blob, "SCN\x03", 4);
    blob[4] = 1;
    blob[5 + 6 * ACID_KNOBS + 2] = 77; // сцена 0, страница 6, энкодер 3
    blob[5 + 10 * ACID_KNOBS + 1] = 25; // слой 1 «выкл» по-старому
    blob[5 + 11 * ACID_KNOBS + 1] = 7;  // слой 2 - модель 7
    memset(blob + 5 + ACID_SCENES * 13 * ACID_KNOBS, 0, user); // значения своих страниц в старом blob - 0..127
    acid_scene_init(&sc);
    CHECK(acid_scene_import(&sc, blob, sizeof blob) == 1);
    CHECK(sc.used == 1);
    CHECK(sc.knob[0][6][2] == 77);
    CHECK(sc.knob[0][ACID_PAGE_PETRI][2] == 0xFF);
    CHECK(sc.knob[0][ACID_PAGE_LAYER1][1] == ACID_LAYER_OFF); // 25 был «выкл», теперь это PETRI
    CHECK(sc.knob[0][ACID_PAGE_LAYER1 + 1][1] == 7);
}

// Короткий аккорд (короче окна дребезга): все голоса чашки отпускаются.
TEST(short_chord_releases_all)
{
    acid_synth_init(&s, SR);
    acid_synth_notes_off(&s);
    acid_synth_set_knob(&s, 6, 1, 25);
    acid_synth_render(&s, buf, 960);
    int notes[3] = {45, 49, 52};
    for (int i = 0; i < 3; i++) {
        acid_midi_event_t e = {.type = ACID_MIDI_NOTE_ON, .ch = 4, .d1 = (uint8_t)notes[i], .d2 = 100};
        acid_synth_midi(&s, &e);
    }
    acid_synth_render(&s, buf, 480); // 10 мс
    for (int i = 0; i < 3; i++) {
        acid_midi_event_t e = {.type = ACID_MIDI_NOTE_OFF, .ch = 4, .d1 = (uint8_t)notes[i], .d2 = 0};
        acid_synth_midi(&s, &e);
    }
    acid_synth_render(&s, buf, 4800);
    int gates = 0;
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        gates += s.petri[0].v[k].gate;
    }
    CHECK(gates == 0);
    CHECK(s.slot[0].nheld == 0);
}

TEST(model_takes_snapshot2)
{
    pl_model_init(&m);
    static pl_snapshot_t s1;
    static pl_snapshot2_t s2;
    s1.mode = 1;
    s1.raw[PL_PAGE_PETRI][5] = 9;
    pl_model_apply(&m, PL_SNAPSHOT, (const uint8_t *)&s1, sizeof s1, 10);
    CHECK(m.have && m.raw[PL_PAGE_PETRI][5] == 9);
    int i = PL_PAGE_PETRI * PL_KNOBS + 5;
    s2.held[i / 8] |= (uint8_t)(1 << (i % 8));
    s2.bsp_pos[5] = 42;
    pl_model_apply(&m, PL_SNAPSHOT2, (const uint8_t *)&s2, sizeof s2, 11);
    CHECK(m.held[PL_PAGE_PETRI][5] == 1);
    CHECK(m.bsp_pos[5] == 42);
}

TEST(petri_page_labels)
{
    uint8_t raw[16] = {0};
    char b[12];
    CHECK(strcmp(pl_page_name(PL_PAGE_PETRI), "PETRI") == 0);
    CHECK(strcmp(pl_knob_label(PL_PAGE_PETRI, 1, raw), "PRESET") == 0);
    CHECK(strcmp(pl_knob_label(PL_PAGE_PETRI, 5, raw), "GRAZERS") == 0);
    CHECK(strcmp(pl_knob_label(PL_PAGE_PETRI, 15, raw), "RESEED") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_PETRI, 1, 3, raw, b), "WORMS") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_PETRI, 14, 100, raw, b), "ON") == 0);
    int lo, hi;
    pl_knob_range(PL_PAGE_PETRI, 5, raw, &lo, &hi);
    CHECK(lo == 0 && hi == 16);
    CHECK(pl_knob_bipolar(PL_PAGE_PETRI, 11, raw));
    CHECK(pl_page_color(PL_PAGE_PETRI) != 0);
}

int main(void)
{
    RUN(snapshot_fits);
    RUN(bsp_18_is_petri);
    RUN(petri_page_defaults_and_lists);
    RUN(petri_freeze_knob);
    RUN(bsp_cc_reaches_petri_page);
    RUN(scene_imports_13_pages);
    RUN(model_takes_snapshot2);
    RUN(short_chord_releases_all);
    RUN(petri_page_labels);
    TEST_MAIN_END();
}
