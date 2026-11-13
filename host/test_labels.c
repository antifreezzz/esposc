// Подписи ручек панели: покрытие всех страниц, типов синта и моделей перкуссии, списки значений.
#include "pl_labels.h"
#include "pl_grids.h"
#include "test.h"

#include <string.h>

// Пустые слоты (индекс энкодера с 0); у синтов - 303, у перкуссии - рим и ковбелл.
static int empty(int page, int enc, int type)
{
    switch (page) {
    case 0: return enc == 15;
    case 2: return enc >= 11;
    case 4:
    case 5: return enc >= 14;
    case 6:
    case 7: return type == 0 && enc >= 10 && enc <= 12;
    case 10:
    case 11: return type == 26 ? enc >= 2 : type == 0 && enc >= 10 && enc <= 12; // слой: как синт, OFF (26) - пусто
    case 8: return 0; // FX: все 15, последняя - REVERB
    case 12: return 0; // Grids: 13 PATTERN, 14 SWING, 15 ROLL
    default: return 0;
    }
}

TEST(test_labels_cover_synth)
{
    uint8_t raw[16] = {0};
    for (int page = 0; page < PL_PAGES; page++) {
        CHECK(pl_page_name(page) != NULL);
        int variants = (page == 6 || page == 7) ? 26 : (page == 10 || page == 11) ? 27 : (page >= 3 && page <= 5) ? 5 : 1;
        for (int t = 0; t < variants; t++) {
            raw[1] = (uint8_t)t;
            CHECK(pl_knob_label(page, 0, raw) == NULL); // энкодер 1 - страница, на круге его нет
            for (int enc = 1; enc < 16; enc++) {
                const char *s = pl_knob_label(page, enc, raw);
                int want_empty = empty(page, enc, t);
                if ((s == NULL) != want_empty || (s && strlen(s) > 10)) {
                    printf("  page %d type %d enc %d: %s\n", page, t, enc + 1, s ? s : "(null)");
                }
                CHECK((s == NULL) == want_empty);
                CHECK(s == NULL || strlen(s) <= 10);
            }
        }
    }
    CHECK(pl_page_name(PL_PAGES) == NULL);
}

static void range(int page, int enc, int type, int lo, int hi)
{
    uint8_t raw[16] = {0};
    raw[1] = (uint8_t)type;
    int a = -1, b = -1;
    pl_knob_range(page, enc, raw, &a, &b);
    if (a != lo || b != hi) {
        printf("  range page %d enc %d: %d..%d, want %d..%d\n", page, enc + 1, a, b, lo, hi);
    }
    CHECK(a == lo && b == hi);
}

TEST(test_ranges)
{
    range(8, 6, 0, 0, 4);   // время дилэя
    range(8, 9, 0, 0, 4);   // статтер
    range(8, 10, 0, 0, 2);  // движение дилэя
    range(9, 1, 0, 0, 16);  // длина шага
    range(9, 2, 0, 1, 8);   // шагов в круге
    range(9, 6, 0, 0, 7);   // лад
    range(9, 7, 0, 0, 11);  // тоника
    range(9, 8, 0, 52, 76); // шаг 1
    range(6, 1, 0, 0, 25);  // тип синта (25 - PETRI)
    range(6, 7, 0, 0, 1);   // wave 303
    range(6, 9, 0, 52, 76); // transpose 303
    range(7, 8, 11, 52, 76); // transpose Plaits
    range(7, 7, 11, 0, 127); // glide Plaits
    range(3, 1, 0, 0, 4);   // модель перкуссии
    range(0, 1, 0, 0, 127); // мастер
}

static void text(int page, int enc, int v, int type, const char *want)
{
    uint8_t raw[16] = {0};
    raw[1] = (uint8_t)type;
    char buf[12];
    const char *s = pl_value_text(page, enc, v, raw, buf);
    if (strcmp(s, want) != 0) {
        printf("  text page %d enc %d v %d: '%s', want '%s'\n", page, enc + 1, v, s, want);
    }
    CHECK(strcmp(s, want) == 0);
}

TEST(test_value_text)
{
    text(8, 6, 1, 0, "1/8");
    text(8, 9, 0, 0, "OFF");
    text(8, 10, 1, 0, "PING-PONG");
    text(6, 7, 1, 0, "SQR");
    text(6, 1, 0, 0, "303");
    text(6, 1, 11, 0, "FM 2OP");
    text(7, 1, 24, 0, "HI-HAT");
    text(3, 1, 3, 0, "MODAL");
    text(9, 1, 0, 0, "OFF");
    text(9, 1, 5, 0, "5");
    text(9, 7, 11, 0, "B");
    text(9, 8, 64, 0, "0");
    text(9, 8, 67, 0, "+3");
    text(6, 9, 52, 0, "-12");
    text(0, 1, 100, 0, "100");
}

TEST(test_tonal_step_in_scale)
{
    uint8_t raw[16] = {0};
    char buf[12];
    raw[6] = 0; // полутоны: весь ход
    CHECK(strcmp(pl_value_text(9, 8, 76, raw, buf), "+12") == 0);
    raw[6] = 2; // лад: синт зажимает до 7 ступеней
    CHECK(strcmp(pl_value_text(9, 8, 76, raw, buf), "+7") == 0);
    CHECK(strcmp(pl_value_text(9, 15, 52, raw, buf), "-7") == 0);
    CHECK(strcmp(pl_value_text(9, 8, 67, raw, buf), "+3") == 0);
    CHECK(strcmp(pl_value_text(6, 9, 76, raw, buf), "+12") == 0); // транспозиция синта лад не трогает
}

TEST(test_label_follows_model)
{
    uint8_t raw[16] = {0};
    raw[1] = 0;
    CHECK(strcmp(pl_knob_label(6, 2, raw), "CUTOFF") == 0);
    raw[1] = 11;
    CHECK(strcmp(pl_knob_label(6, 2, raw), "HARMONICS") == 0);
    raw[1] = 2; // перкуссия FM
    CHECK(strcmp(pl_knob_label(3, 6, raw), "INDEX") == 0);
    raw[1] = 4; // шум
    CHECK(strcmp(pl_knob_label(4, 7, raw), "COLOR") == 0);
    CHECK(strcmp(pl_page_name(6), "SYNTH 1") == 0);
}

TEST(test_labels_depend)
{
    // Подписи зависят от энкодера 2 только у перкуссии и синтов: на MIX это MASTER, перестраивать круг не надо.
    int want[10] = {0, 0, 0, 1, 1, 1, 1, 1, 0, 0};
    for (int p = 0; p < 10; p++) {
        CHECK(pl_labels_depend(p) == want[p]);
    }
}

TEST(test_colors)
{
    uint8_t raw[16] = {0};
    for (int p = 0; p < 10; p++) {
        CHECK(pl_page_color(p) != 0);
        int variants = (p == 6 || p == 7) ? 26 : (p >= 3 && p <= 5) ? 5 : 1;
        for (int t = 0; t < variants; t++) {
            raw[1] = (uint8_t)t;
            CHECK(pl_knob_color(p, 0, raw) == 0);
            for (int enc = 1; enc < 16; enc++) {
                uint32_t c = pl_knob_color(p, enc, raw);
                CHECK((c == 0) == empty(p, enc, t));
            }
        }
    }
    CHECK(pl_page_color(-1) == 0);
    CHECK(pl_page_color(PL_PAGES) == 0);
    // На странице MIX мастер, синты, ударка, фильтр и панорама различаются по цвету групп.
    CHECK(pl_knob_color(0, 1, raw) != pl_knob_color(0, 3, raw));
    CHECK(pl_knob_color(0, 3, raw) != pl_knob_color(0, 5, raw));
    CHECK(pl_knob_color(0, 5, raw) != pl_knob_color(0, 11, raw));
}

TEST(test_bipolar)
{
    uint8_t raw[16] = {0};
    CHECK(pl_knob_bipolar(0, 1, raw) == 0);  // MASTER
    CHECK(pl_knob_bipolar(0, 11, raw) == 1); // DJ FILTER
    CHECK(pl_knob_bipolar(0, 13, raw) == 1); // PAN S1
    CHECK(pl_knob_bipolar(1, 13, raw) == 1); // PAN HAT
    CHECK(pl_knob_bipolar(2, 3, raw) == 1);  // CRASH TUNE
    CHECK(pl_knob_bipolar(2, 4, raw) == 1);  // CRASH PAN
    CHECK(pl_knob_bipolar(3, 12, raw) == 1); // TOMS PAN
    raw[1] = 0;                              // 303
    CHECK(pl_knob_bipolar(6, 2, raw) == 0);  // CUTOFF
    CHECK(pl_knob_bipolar(6, 9, raw) == 1);  // TRANS 303
    CHECK(pl_knob_bipolar(6, 14, raw) == 1); // TONE
    raw[1] = 11;                             // Plaits
    CHECK(pl_knob_bipolar(6, 8, raw) == 1);  // TRANS Plaits
    CHECK(pl_knob_bipolar(6, 9, raw) == 1);  // T ENV
    CHECK(pl_knob_bipolar(6, 10, raw) == 1); // M ENV
    CHECK(pl_knob_bipolar(6, 11, raw) == 1); // P ENV
    CHECK(pl_knob_bipolar(9, 7, raw) == 0);  // ROOT
    CHECK(pl_knob_bipolar(9, 8, raw) == 1);  // STEP 1
    CHECK(pl_knob_bipolar(9, 15, raw) == 1); // STEP 8
}

TEST(test_page_summary)
{
    uint8_t raw[16] = {0};
    char buf[24];
    raw[1] = 0;
    CHECK(strcmp(pl_page_summary(6, raw, buf), "303") == 0);
    raw[1] = 11;
    CHECK(strcmp(pl_page_summary(7, raw, buf), "FM 2OP") == 0);
    raw[1] = 3;
    CHECK(strcmp(pl_page_summary(3, raw, buf), "MODAL") == 0);
    // TONAL: выключен или лад и тоника.
    raw[1] = 0;
    CHECK(strcmp(pl_page_summary(9, raw, buf), "OFF") == 0);
    raw[1] = 4;
    raw[6] = 2; // MINOR
    raw[7] = 9; // A
    CHECK(strcmp(pl_page_summary(9, raw, buf), "MINOR A") == 0);
    // FX: время дилэя.
    raw[6] = 1; // 1/8
    raw[9] = 0; // stutter off
    CHECK(strcmp(pl_page_summary(8, raw, buf), "DLY 1/8") == 0);
    // MIX: DJ-фильтр выключен ровно на 64, 0 - ФНЧ до упора.
    raw[11] = 64;
    CHECK(strcmp(pl_page_summary(0, raw, buf), "LEVELS & DJF") == 0);
    raw[11] = 0;
    CHECK(strcmp(pl_page_summary(0, raw, buf), "DJF LP -64") == 0);
    raw[11] = 70;
    CHECK(strcmp(pl_page_summary(0, raw, buf), "DJF HP +6") == 0);
}

TEST(test_slot_label)
{
    static uint8_t raw[PL_PAGES][PL_KNOBS];
    memset(raw, 0, sizeof raw); // синт 1 - 303
    pl_target_t t[PL_TARGETS];
    memset(t, PL_NONE, sizeof t);
    char buf[20];
    CHECK(pl_slot_label(t, raw, buf) == NULL);
    t[0] = (pl_target_t){6 << 4 | 2, 0, 127};
    CHECK(strcmp(pl_slot_label(t, raw, buf), "CUTOFF") == 0);
    t[1] = (pl_target_t){6 << 4 | 3, 0, 127};
    t[2] = (pl_target_t){8 << 4 | 3, 0, 127};
    CHECK(strcmp(pl_slot_label(t, raw, buf), "CUTOFF +2") == 0);
    raw[6][1] = 11; // Plaits: подпись по типу
    CHECK(strcmp(pl_slot_label(t, raw, buf), "HARMONICS +2") == 0);
    CHECK(strcmp(pl_user_name(0), "USER 1") == 0);
    CHECK(strcmp(pl_user_name(5), "USER 6") == 0);
    CHECK(pl_user_name(6) == NULL && pl_user_name(-1) == NULL);
}

TEST(test_layer_labels)
{
    uint8_t raw[16] = {0};
    raw[1] = 0;
    CHECK(strcmp(pl_knob_label(10, 2, raw), "CUTOFF") == 0);
    raw[1] = 21;
    CHECK(strcmp(pl_knob_label(11, 2, raw), "HARMONICS") == 0);
    raw[1] = 26;
    CHECK(strcmp(pl_knob_label(10, 1, raw), "TYPE") == 0);
    CHECK(pl_knob_label(10, 2, raw) == NULL);
    char buf[12];
    CHECK(strcmp(pl_value_text(10, 1, 26, raw, buf), "OFF") == 0);
    CHECK(strcmp(pl_value_text(6, 1, 26, raw, buf), "OFF") != 0); // у синта OFF нет
    int lo, hi;
    pl_knob_range(10, 1, raw, &lo, &hi);
    CHECK(lo == 0 && hi == 26);
    pl_knob_range(6, 1, raw, &lo, &hi);
    CHECK(lo == 0 && hi == 25);
    CHECK(pl_knob_skip(10, 1, raw) == 25 && pl_knob_skip(11, 1, raw) == 25); // слой не бывает PETRI
    CHECK(pl_knob_skip(6, 1, raw) == -1 && pl_knob_skip(10, 2, raw) == -1);
    CHECK(pl_labels_depend(10) && pl_labels_depend(11));
    raw[1] = 0;
    CHECK(pl_knob_color(10, 3, raw) == pl_knob_color(6, 3, raw) && pl_knob_color(10, 3, raw) != 0);
}

TEST(test_fx_reverb_labels)
{
    uint8_t raw[16] = {0};
    CHECK(strcmp(pl_knob_label(8, 12, raw), "LYR1 VOL") == 0);
    CHECK(strcmp(pl_knob_label(8, 13, raw), "LYR2 VOL") == 0);
    CHECK(strcmp(pl_knob_label(8, 14, raw), "LYR HALF") == 0);
    CHECK(strcmp(pl_knob_label(8, 15, raw), "REVERB") == 0);
    char buf[12];
    CHECK(strcmp(pl_value_text(8, 14, 63, raw, buf), "OFF") == 0); // переключатель: порог 64
    CHECK(strcmp(pl_value_text(8, 14, 64, raw, buf), "ON") == 0);
    CHECK(strcmp(pl_value_text(8, 15, 0, raw, buf), "0") == 0);
    CHECK(strcmp(pl_value_text(8, 15, 64, raw, buf), "64") == 0);
    CHECK(strcmp(pl_value_text(8, 15, 127, raw, buf), "127") == 0);
    static const char *const name[6] = {"RVB S1", "RVB S2", "RVB DRUMS", "DECAY", "DAMP", "WIDTH"};
    CHECK(strcmp(pl_page_name(PL_PAGE_REVERB), "REVERB") == 0);
    CHECK(pl_page_color(PL_PAGE_REVERB) == pl_page_color(8));
    for (int e = 1; e < PL_KNOBS; e++) {
        const char *label = pl_knob_label(PL_PAGE_REVERB, e, raw);
        CHECK(label == (e <= 6 ? name[e - 1] : NULL));
        if (label) {
            CHECK(strcmp(label, name[e - 1]) == 0);
            CHECK(strcmp(pl_value_text(PL_PAGE_REVERB, e, 64, raw, buf), "64") == 0);
            int lo, hi;
            pl_knob_range(PL_PAGE_REVERB, e, raw, &lo, &hi);
            CHECK(lo == 0 && hi == 127);
        }
    }
}

TEST(test_grids_labels)
{
    static const uint8_t raw[16];
    static const char *const L[] = {NULL, "ON", "X", "Y", "KICK", "SNARE", "HAT", "CHAOS", "HAT MODE", "TIME",
                                    "EVERY", "FILL LEN", "FILL"};
    CHECK(strcmp(pl_page_name(PL_PAGE_GRIDS), "GRIDS") == 0);
    for (int e = 1; e <= 12; e++) {
        CHECK(pl_knob_label(PL_PAGE_GRIDS, e, raw) && strcmp(pl_knob_label(PL_PAGE_GRIDS, e, raw), L[e]) == 0);
    }
    CHECK(strcmp(pl_knob_label(PL_PAGE_GRIDS, 13, raw), "PATTERN") == 0);
    CHECK(strcmp(pl_knob_label(PL_PAGE_GRIDS, 14, raw), "SWING") == 0);
    CHECK(strcmp(pl_knob_label(PL_PAGE_GRIDS, 15, raw), "ROLL") == 0);
    int lo, hi;
    pl_knob_range(PL_PAGE_GRIDS, 2, raw, &lo, &hi);
    CHECK(lo == 0 && hi == 127);
    static const int HI[] = {[1] = 1, [8] = 2, [9] = 1, [10] = 3, [11] = 2, [12] = 1};
    for (int e = 1; e <= 12; e++) {
        if (HI[e]) {
            pl_knob_range(PL_PAGE_GRIDS, e, raw, &lo, &hi);
            CHECK(lo == 0 && hi == HI[e]);
        }
    }
    char buf[12];
    CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 1, 1, raw, buf), "ON") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 8, 2, raw, buf), "MIX") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 9, 1, raw, buf), "RIDE") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 10, 0, raw, buf), "OFF") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 10, 3, raw, buf), "16") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 11, 2, raw, buf), "4") == 0);
    CHECK(pl_page_color(PL_PAGE_GRIDS) != 0);
    // Источник рисунка: карта, банк по именам, свои; свинг в процентах.
    pl_knob_range(PL_PAGE_GRIDS, 13, raw, &lo, &hi);
    CHECK(lo == 0 && hi == PL_GRIDS_SRCS - 1);
    CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 13, 0, raw, buf), "MAP") == 0);
    for (int b = 0; b < PL_GRIDS_BANK; b++) {
        CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 13, 1 + b, raw, buf), pl_grids_bank[b].name) == 0);
    }
    CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 13, PL_GRIDS_SRC_USER, raw, buf), "U1") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 13, PL_GRIDS_SRCS - 1, raw, buf), "U4") == 0);
    pl_knob_range(PL_PAGE_GRIDS, 14, raw, &lo, &hi);
    CHECK(lo == 0 && hi == 3);
    CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 14, 0, raw, buf), "50%") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_GRIDS, 14, 2, raw, buf), "67%") == 0);
    // В шапке - рисунок вместо точки карты.
    uint8_t on[16] = {0};
    char sum[24];
    on[1] = 1, on[2] = 5, on[3] = 6;
    CHECK(strcmp(pl_page_summary(PL_PAGE_GRIDS, on, sum), "ON X5 Y6") == 0);
    on[13] = 7;
    CHECK(strcmp(pl_page_summary(PL_PAGE_GRIDS, on, sum), "ON AMEN") == 0);
}

// Тип синта PETRI (25): свои подписи и списки, у слоя «выкл» теперь 26.
TEST(petri_labels)
{
    uint8_t raw[16] = {0};
    char buf[12];
    raw[1] = 25;
    CHECK(strcmp(pl_value_text(6, 1, 25, raw, buf), "PETRI") == 0);
    CHECK(strcmp(pl_knob_label(6, 2, raw), "CUTOFF") == 0);
    CHECK(strcmp(pl_knob_label(6, 8, raw), "RELIEF") == 0);
    CHECK(strcmp(pl_knob_label(6, 10, raw), "ORBIT") == 0);
    CHECK(strcmp(pl_knob_label(6, 13, raw), "DRIVE") == 0);
    CHECK(strcmp(pl_value_text(6, 8, 3, raw, buf), "VOID") == 0);
    CHECK(strcmp(pl_value_text(6, 10, 3, raw, buf), "ROSE") == 0);
    CHECK(strcmp(pl_value_text(6, 12, 127, raw, buf), "FOLLOW") == 0);
    int lo, hi;
    pl_knob_range(6, 8, raw, &lo, &hi);
    CHECK(lo == 0 && hi == 3);
    pl_knob_range(6, 1, raw, &lo, &hi);
    CHECK(hi == 25);
    pl_knob_range(10, 1, raw, &lo, &hi);
    CHECK(hi == 26);
    raw[1] = 26;
    CHECK(strcmp(pl_value_text(10, 1, 26, raw, buf), "OFF") == 0);
    CHECK(pl_knob_label(10, 2, raw) == NULL);
}

TEST(test_pitch_labels)
{
    static const uint8_t raw[16];
    char buf[12];
    CHECK(strcmp(pl_page_name(PL_PAGE_PITCH), "PITCH") == 0);
    CHECK(pl_page_color(PL_PAGE_PITCH) == pl_page_color(6));
    CHECK(strcmp(pl_knob_label(PL_PAGE_PITCH, 1, raw), "S1 GLIDE") == 0);
    CHECK(strcmp(pl_knob_label(PL_PAGE_PITCH, 2, raw), "S2 GLIDE") == 0);
    CHECK(strcmp(pl_knob_label(PL_PAGE_PITCH, 3, raw), "TUNING") == 0);
    CHECK(strcmp(pl_knob_label(PL_PAGE_PITCH, 4, raw), "S1 GATE") == 0);
    CHECK(strcmp(pl_knob_label(PL_PAGE_PITCH, 5, raw), "S2 GATE") == 0);
    CHECK(pl_knob_label(PL_PAGE_PITCH, 6, raw) == NULL && pl_knob_label(PL_PAGE_PITCH, 0, raw) == NULL);
    int lo, hi;
    pl_knob_range(PL_PAGE_PITCH, 1, raw, &lo, &hi);
    CHECK(lo == 0 && hi == 6 && pl_knob_list(PL_PAGE_PITCH, 2, raw));
    CHECK(strcmp(pl_value_text(PL_PAGE_PITCH, 1, 0, raw, buf), "SMOOTH") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_PITCH, 2, 1, raw, buf), "SEMI") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_PITCH, 2, 2, raw, buf), "QUARTER") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_PITCH, 1, 3, raw, buf), "SEMI GL") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_PITCH, 1, 4, raw, buf), "QRTR GL") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_PITCH, 1, 5, raw, buf), "SEMI ART") == 0);
    CHECK(strcmp(pl_value_text(PL_PAGE_PITCH, 1, 6, raw, buf), "QRTR ART") == 0);
    pl_knob_range(PL_PAGE_PITCH, 3, raw, &lo, &hi);
    CHECK(lo == 0 && hi == 5);
    static const char *const T[6] = {"EQUAL", "JUST", "PYTHAG", "MEANTONE", "RAST", "BAYATI"};
    for (int i = 0; i < 6; i++) {
        CHECK(strcmp(pl_value_text(PL_PAGE_PITCH, 3, i, raw, buf), T[i]) == 0);
        CHECK(strlen(T[i]) <= 10);
    }
}

int main(void)
{
    RUN(petri_labels);
    RUN(test_layer_labels);
    RUN(test_fx_reverb_labels);
    RUN(test_labels_cover_synth);
    RUN(test_grids_labels);
    RUN(test_ranges);
    RUN(test_value_text);
    RUN(test_tonal_step_in_scale);
    RUN(test_label_follows_model);
    RUN(test_labels_depend);
    RUN(test_colors);
    RUN(test_bipolar);
    RUN(test_page_summary);
    RUN(test_slot_label);
    RUN(test_pitch_labels);
    TEST_MAIN_END();
}
