// Тип синта PETRI (25): слот звучит от таблиц чашки, слой его не берёт, память ручек переходит с версии 3.
#include "acid.h"
#include "test.h"

#include <math.h>
#include <string.h>

#define SR 48000.0f
#define BASS 4
#define PETRI 25

static acid_synth_t s, b;
static float buf[48000];

static void send(acid_synth_t *x, uint8_t type, uint8_t ch, uint8_t d1, uint8_t d2)
{
    acid_midi_event_t e = {.type = type, .ch = ch, .d1 = d1, .d2 = d2};
    acid_synth_midi(x, &e);
}

static float rms(int n)
{
    acid_synth_render(&s, buf, (size_t)n);
    double sq = 0.0;
    for (int i = 0; i < n; i++) {
        sq += (double)buf[i] * buf[i];
    }
    return (float)sqrt(sq / n);
}

static void live(void)
{
    acid_synth_init(&s, SR);
    acid_synth_notes_off(&s);
}

TEST(petri_type_sounds)
{
    live();
    acid_synth_set_knob(&s, 6, 1, PETRI);
    rms(960);
    CHECK(s.slot[0].type == PETRI);
    CHECK(rms(4800) < 1e-4f);
    send(&s, ACID_MIDI_NOTE_ON, BASS, 45, 100);
    float r = rms(9600);
    printf("  petri rms %.3f\n", r);
    CHECK(r > 0.02f);
    CHECK(acid_petri_active(&s.petri[0]));
    send(&s, ACID_MIDI_NOTE_OFF, BASS, 45, 0);
    for (int i = 0; i < 7; i++) {
        rms(48000); // релиз 0.6 с - постоянная времени: до 1e-4 ~5.5 с
    }
    CHECK(!acid_petri_active(&s.petri[0]));
}

TEST(petri_chord_voices)
{
    live();
    acid_synth_set_knob(&s, 6, 1, PETRI);
    rms(960);
    send(&s, ACID_MIDI_NOTE_ON, BASS, 45, 100);
    send(&s, ACID_MIDI_NOTE_ON, BASS, 52, 100);
    send(&s, ACID_MIDI_NOTE_ON, BASS, 57, 100);
    rms(480);
    int on = 0;
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        on += s.petri[0].v[k].gate;
    }
    CHECK(on == 3);
    send(&s, ACID_MIDI_NOTE_OFF, BASS, 52, 0);
    rms(2400); // дальше окна дребезга
    on = 0;
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        on += s.petri[0].v[k].gate;
    }
    CHECK(on == 2);
}

static int gate_of(int note)
{
    for (int k = 0; k < ACID_PETRI_VOICES; k++) {
        if (s.petri[0].v[k].gate && s.petri[0].v[k].note == note) {
            return 1;
        }
    }
    return 0;
}

// Две короткие ноты аккорда подряд: вторая отпускается через MERGE_S от своего нажатия, а не от соседней.
TEST(short_chord_release_uses_own_press)
{
    live();
    acid_synth_set_knob(&s, 6, 1, PETRI);
    rms(960);
    send(&s, ACID_MIDI_NOTE_ON, BASS, 45, 100);
    send(&s, ACID_MIDI_NOTE_ON, BASS, 52, 100);
    rms(1000);
    send(&s, ACID_MIDI_NOTE_ON, BASS, 57, 100);
    send(&s, ACID_MIDI_NOTE_OFF, BASS, 45, 0); // короткая: отложена
    send(&s, ACID_MIDI_NOTE_OFF, BASS, 52, 0); // короткая: 45 отпущена, 52 отложена до нажатия + 50 мс
    rms(2000);                                 // 52 нажата 3000 сэмплов назад, окно 2400
    CHECK(!gate_of(45));
    CHECK(!gate_of(52));
    CHECK(gate_of(57));
}

TEST(type_switch_kills_petri_voices)
{
    live();
    acid_synth_set_knob(&s, 6, 1, PETRI);
    rms(960);
    send(&s, ACID_MIDI_NOTE_ON, BASS, 45, 100);
    rms(960);
    acid_synth_set_knob(&s, 6, 1, 0);
    rms(960);
    CHECK(s.slot[0].type == 0);
    send(&s, ACID_MIDI_NOTE_OFF, BASS, 45, 0);
    acid_synth_set_knob(&s, 6, 1, PETRI);
    rms(960);
    CHECK(!acid_petri_active(&s.petri[0])); // старые голоса не всплыли
}

TEST(layer_refuses_petri)
{
    live();
    acid_synth_set_knob(&s, ACID_PAGE_LAYER1, 1, PETRI);
    rms(960);
    CHECK(s.layer[0].sel_type == ACID_LAYER_OFF);
    CHECK(s.layer[0].pet == NULL);
    acid_synth_set_knob(&s, ACID_PAGE_LAYER1, 1, 3);
    rms(960);
    CHECK(s.layer[0].type == 3);
}

TEST(layer_off_is_26)
{
    CHECK(ACID_LAYER_OFF == 26);
    CHECK(ACID_TYPE_PETRI == 25);
    live();
    CHECK(acid_synth_knob(&s, ACID_PAGE_LAYER1, 1) == 26);
}

TEST(petri_knob_lists)
{
    live();
    acid_synth_set_knob(&s, 6, 1, PETRI);
    rms(960);
    // Индексы энкодеров с 0: 8 - режим рельефа (энкодер 9), 10 - форма орбиты (энкодер 11).
    acid_synth_set_knob(&s, 6, 8, 2);
    CHECK(acid_synth_knob(&s, 6, 8) == 2);
    acid_synth_set_knob(&s, 6, 8, 7); // режимов 4: вне списка игнор
    CHECK(acid_synth_knob(&s, 6, 8) == 2);
    acid_synth_set_knob(&s, 6, 10, 4);
    acid_synth_set_knob(&s, 6, 10, 5);
    CHECK(acid_synth_knob(&s, 6, 10) == 4);
    CHECK(acid_synth_knob(&s, 6, 2) == 57); // срез по умолчанию ~540 Гц
}

// Модуляция пожирателей с отрицательной глубиной закрывает фильтр.
TEST(petri_route_moves_cutoff)
{
    live();
    acid_synth_set_knob(&s, 6, 1, PETRI);
    rms(960);
    acid_petri_frame_t f;
    memset(&f, 0, sizeof f);
    for (int i = 0; i < ACID_PETRI_TABLE; i++) {
        f.tab[i] = (int16_t)(16000 * sinf(6.2831853f * i / ACID_PETRI_TABLE));
    }
    acid_synth_petri(&s, &f);
    send(&s, ACID_MIDI_NOTE_ON, BASS, 45, 100);
    rms(9600);
    float g0 = s.slot[0].g;
    f.mods[0] = 127; // все пожиратели
    acid_synth_petri(&s, &f);
    rms(9600);
    CHECK(s.slot[0].g < g0 * 0.8f);
}

static void old_blob(uint8_t *blob, size_t *len, int ver)
{
    // Раскладка версий 2-3: 25 типов на слот, у слоя «выкл» = 25.
    const int OLD_TYPES = 25, slots = ver == 2 ? ACID_SLOTS : 2 * ACID_SLOTS;
    uint8_t *p = blob;
    memcpy(p, "AS5", 3);
    p[3] = (uint8_t)ver;
    p += 4;
    for (int k = 0; k < slots; k++) {
        *p++ = k < ACID_SLOTS ? (uint8_t)(k == 0 ? 11 : 3) : 25;
        memset(p, 0xFF, (size_t)OLD_TYPES * ACID_KNOBS);
        p[11 * ACID_KNOBS + 2] = 99; // тип 11, энкодер 3
        p += OLD_TYPES * ACID_KNOBS;
    }
    memset(p, 0xFF, ACID_TONAL_KNOBS);
    p += ACID_TONAL_KNOBS;
    *len = (size_t)(p - blob);
}

TEST(blob_v3_imports)
{
    static uint8_t blob[4096];
    size_t len;
    for (int ver = 2; ver <= 3; ver++) {
        old_blob(blob, &len, ver);
        acid_synth_init(&b, SR);
        CHECK(acid_synth_import(&b, blob, len) == 1);
        CHECK(acid_synth_knob(&b, 6, 1) == 11);
        CHECK(acid_synth_knob(&b, 6, 2) == 99);
        CHECK(acid_synth_knob(&b, 7, 1) == 3);
        CHECK(acid_synth_knob(&b, ACID_PAGE_LAYER1, 1) == ACID_LAYER_OFF);
    }
}

TEST(blob_v4_roundtrip_petri)
{
    live();
    acid_synth_set_knob(&s, 7, 1, PETRI);
    rms(960);
    acid_synth_set_knob(&s, 7, 3, 33);
    static uint8_t blob[ACID_SYNTH_BLOB];
    CHECK(acid_synth_export(&s, blob, sizeof blob) == ACID_SYNTH_BLOB);
    acid_synth_init(&b, SR);
    CHECK(acid_synth_import(&b, blob, sizeof blob) == 1);
    CHECK(acid_synth_knob(&b, 7, 1) == PETRI && acid_synth_knob(&b, 7, 3) == 33);
}

int main(void)
{
    RUN(petri_type_sounds);
    RUN(petri_chord_voices);
    RUN(short_chord_release_uses_own_press);
    RUN(type_switch_kills_petri_voices);
    RUN(layer_refuses_petri);
    RUN(layer_off_is_26);
    RUN(petri_knob_lists);
    RUN(petri_route_moves_cutoff);
    RUN(blob_v3_imports);
    RUN(blob_v4_roundtrip_petri);
    TEST_MAIN_END();
}
