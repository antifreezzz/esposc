// Подписи ручек панели: покрытие всех страниц, типов синта и моделей перкуссии, списки значений.
#include "pl_labels.h"
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
    case 8: return enc >= 12;
    default: return 0;
    }
}

TEST(test_labels_cover_synth)
{
    uint8_t raw[16] = {0};
    for (int page = 0; page < 10; page++) {
        CHECK(pl_page_name(page) != NULL);
        int variants = (page == 6 || page == 7) ? 25 : (page >= 3 && page <= 5) ? 5 : 1;
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
    CHECK(pl_page_name(10) == NULL);
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
    range(6, 1, 0, 0, 24);  // тип синта
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
        int variants = (p == 6 || p == 7) ? 25 : (p >= 3 && p <= 5) ? 5 : 1;
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
    CHECK(pl_page_color(10) == 0);
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

int main(void)
{
    RUN(test_labels_cover_synth);
    RUN(test_ranges);
    RUN(test_value_text);
    RUN(test_tonal_step_in_scale);
    RUN(test_label_follows_model);
    RUN(test_labels_depend);
    RUN(test_colors);
    RUN(test_bipolar);
    RUN(test_page_summary);
    RUN(test_slot_label);
    TEST_MAIN_END();
}
