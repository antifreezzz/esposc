// Модуляторы на панели: подписи страницы, списки в сетке, цели и действующее значение (то же, что у синта).
#include <string.h>

#include "acid_mod.h"
#include "acid_synth.h"
#include "pl_labels.h"
#include "pl_mod.h"
#include "pl_model.h"
#include "pl_pick.h"
#include "test.h"

static pl_model_t m;
static acid_synth_t syn;

// Модель панели из состояния синта: сырые значения страниц 0..13.
static void model_from_synth(void)
{
    pl_model_init(&m);
    m.have = 1;
    for (int p = 0; p < PL_PAGES; p++) {
        for (int e = 1; e < PL_KNOBS; e++) {
            m.raw[p][e] = (uint8_t)acid_synth_knob(&syn, p, e);
        }
    }
}

TEST(test_page_names)
{
    CHECK(!strcmp(pl_page_name(PL_PAGE_MOD1), "MOD 1") && !strcmp(pl_page_name(PL_PAGE_MOD1 + 3), "MOD 4"));
    CHECK(pl_page_name(PL_PAGE_MOD1 + 5) == NULL && pl_page_name(PL_PAGE_MIX_DRUMS) == NULL);
}

TEST(test_labels)
{
    static const char *const L[16] = {NULL,   "SHAPE",   "RATE",    "SYNC",    "TRIG",    "PROB",    "ATTACK", "DECAY",
                                      "AMOUNT", "DEPTH 1", "DEPTH 2", "DEPTH 3", "DEPTH 4", NULL, NULL, NULL};
    uint8_t raw[16] = {0};
    for (int k = 0; k < PL_MOD_PAGES; k++) {
        for (int e = 0; e < 16; e++) {
            const char *s = pl_knob_label(PL_PAGE_MOD1 + k, e, raw);
            CHECK(L[e] ? (s && !strcmp(s, L[e])) : s == NULL);
            CHECK(!s || strlen(s) <= 10); // не длиннее самых длинных подписей соседей (CRASH TAIL)
        }
    }
    // Номера ACID_MOD_* и места подписей совпадают: энкодеры страницы модулятора.
    CHECK(ACID_MOD_SHAPE == 1 && ACID_MOD_AMOUNT == 8 && ACID_MOD_DEPTH1 == 9);
    CHECK(!pl_labels_depend(PL_PAGE_MOD1));
}

TEST(test_value_lists_match_synth)
{
    static const char *const SHAPE[] = {"SIN", "TRI", "SAW", "RAMP", "SQR", "S&H", "DRIFT", "ENV"};
    static const char *const SYNC[] = {"FREE", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
    static const char *const TRIG[] = {"FREE", "S1 NOTE", "S2 NOTE", "S1 ACC", "S2 ACC", "KICK", "SNARE",
                                       "HAT",  "CLAP",    "PERC",    "BEAT",   "BAR",    "4 BARS", "STEP"};
    uint8_t raw[16] = {0};
    char buf[12];
    CHECK(ACID_SHAPES == 8 && ACID_MOD_SYNCS == 9 && ACID_MOD_TRIGS == 14 && ACID_SHAPE_ENV == PL_MOD_SHAPE_ENV);
    for (int v = 0; v < ACID_SHAPES; v++) {
        CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1, ACID_MOD_SHAPE, v, raw, buf), SHAPE[v]));
    }
    for (int v = 0; v < ACID_MOD_SYNCS; v++) {
        CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1 + 1, ACID_MOD_SYNC, v, raw, buf), SYNC[v]));
    }
    for (int v = 0; v < ACID_MOD_TRIGS; v++) {
        CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1 + 2, ACID_MOD_TRIG, v, raw, buf), TRIG[v]));
    }
    int lo, hi;
    pl_knob_range(PL_PAGE_MOD1, ACID_MOD_SHAPE, raw, &lo, &hi);
    CHECK(lo == 0 && hi == ACID_SHAPES - 1 && pl_knob_list(PL_PAGE_MOD1, ACID_MOD_SHAPE, raw));
    pl_knob_range(PL_PAGE_MOD1, ACID_MOD_SYNC, raw, &lo, &hi);
    CHECK(lo == 0 && hi == ACID_MOD_SYNCS - 1);
    pl_knob_range(PL_PAGE_MOD1, ACID_MOD_TRIG, raw, &lo, &hi);
    CHECK(lo == 0 && hi == ACID_MOD_TRIGS - 1);
    for (int e = 1; e < 14; e++) { // остальные ручки не списки: 0..127
        if (e == ACID_MOD_SHAPE || e == ACID_MOD_SYNC || e == ACID_MOD_TRIG) {
            continue;
        }
        pl_knob_range(PL_PAGE_MOD1, e, raw, &lo, &hi);
        CHECK(lo == 0 && hi == 127 && !pl_knob_list(PL_PAGE_MOD1, e, raw));
    }
}

TEST(test_value_text_numbers)
{
    uint8_t raw[16] = {0};
    char buf[12];
    CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1, ACID_MOD_PROB, 127, raw, buf), "100%"));
    CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1, ACID_MOD_PROB, 0, raw, buf), "0%"));
    CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1, ACID_MOD_AMOUNT, 127, raw, buf), "100%"));
    CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1, ACID_MOD_DEPTH1, 64, raw, buf), "0"));
    CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1, ACID_MOD_DEPTH1 + 3, 100, raw, buf), "+36"));
    CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1, ACID_MOD_DEPTH1 + 1, 1, raw, buf), "-63"));
    CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1, ACID_MOD_RATE, 0, raw, buf), "0.05 Hz"));
    CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1, ACID_MOD_RATE, 127, raw, buf), "20 Hz"));
    CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1, ACID_MOD_ATTACK, 0, raw, buf), "1 ms"));
    CHECK(!strcmp(pl_value_text(PL_PAGE_MOD1, ACID_MOD_DECAY, 127, raw, buf), "10 s"));
    for (int v = 0; v < 128; v++) { // везде влезает в 12 знаков
        for (int e = ACID_MOD_RATE; e <= ACID_MOD_DEPTH1 + 3; e++) {
            CHECK(strlen(pl_value_text(PL_PAGE_MOD1, e, v, raw, buf)) <= 11);
        }
    }
    CHECK(pl_knob_bipolar(PL_PAGE_MOD1, ACID_MOD_DEPTH1, raw) && pl_knob_bipolar(PL_PAGE_MOD1, ACID_MOD_DEPTH1 + 3, raw));
    CHECK(!pl_knob_bipolar(PL_PAGE_MOD1, ACID_MOD_AMOUNT, raw) && !pl_knob_bipolar(PL_PAGE_MOD1, ACID_MOD_SHAPE, raw));
    CHECK(pl_knob_color(PL_PAGE_MOD1, ACID_MOD_SHAPE, raw) != 0 && pl_knob_color(PL_PAGE_MOD1, 14, raw) == 0);
    CHECK(pl_page_color(PL_PAGE_MOD1) != 0);
}

// Списки страницы модулятора выбираются сеткой: pl_pick_view отдаёт GRID, число клеток и значения - как у синта.
TEST(test_lists_pick_by_grid)
{
    pl_model_init(&m);
    m.have = 1;
    static const int N[3] = {ACID_SHAPES, ACID_MOD_SYNCS, ACID_MOD_TRIGS};
    static const int E[3] = {ACID_MOD_SHAPE, ACID_MOD_SYNC, ACID_MOD_TRIG};
    for (int k = 0; k < PL_MOD_PAGES; k++) {
        int page = PL_PAGE_MOD1 + k;
        for (int i = 0; i < 3; i++) {
            int tp, te;
            CHECK(pl_pick_view(&m, page, E[i], &tp, &te) == PL_PICK_GRID && tp == page && te == E[i]);
            CHECK(pl_pick_count(page, E[i], m.mod_knob[k]) == N[i]);
            for (int s = 0; s < N[i]; s++) {
                CHECK(pl_pick_value(page, E[i], m.mod_knob[k], s) == s && pl_pick_slot(page, E[i], m.mod_knob[k], s) == s);
            }
            CHECK(pl_pick_value(page, E[i], m.mod_knob[k], N[i]) == -1);
            CHECK(pl_pick_grab(page, E[i], m.mod_knob[k], 1) == 0); // удержание - тап по списку
        }
        for (int e = 1; e < 14; e++) { // плавные ручки сеткой не выбираются
            if (e != ACID_MOD_SHAPE && e != ACID_MOD_SYNC && e != ACID_MOD_TRIG) {
                int tp, te;
                CHECK(pl_pick_view(&m, page, e, &tp, &te) == PL_PICK_NONE);
            }
        }
    }
}

TEST(test_can_target_matches_synth)
{
    acid_synth_init(&syn, 48000.0f);
    for (int page = 0; page < ACID_PAGES; page++) {
        int is_syn = page == 6 || page == 7 || page == 10 || page == 11;
        int max_t = is_syn ? (page >= 10 ? ACID_LAYER_OFF : ACID_TYPES - 1) : 0;
        for (int t = 0; t <= max_t; t++) {
            if (page >= 10 && t == ACID_TYPE_PETRI) {
                continue;
            }
            if (is_syn) {
                acid_synth_set_knob(&syn, page, 1, t);
            }
            model_from_synth();
            for (int e = 0; e < ACID_KNOBS; e++) {
                CHECK(pl_mod_can_target(&m, page, e) == acid_synth_knob_smooth(&syn, page, e));
            }
        }
    }
}

TEST(test_can_target_examples)
{
    acid_synth_init(&syn, 48000.0f);
    model_from_synth();
    CHECK(pl_mod_can_target(&m, 0, 2) && pl_mod_can_target(&m, 1, 1) && pl_mod_can_target(&m, 12, 2));
    CHECK(!pl_mod_can_target(&m, 0, 0) && !pl_mod_can_target(&m, 0, 16) && !pl_mod_can_target(&m, 0, 15)); // пустой слот
    CHECK(!pl_mod_can_target(&m, 6, 1) && !pl_mod_can_target(&m, 8, 14) && pl_mod_can_target(&m, 8, 15) && !pl_mod_can_target(&m, 12, 12));
    CHECK(!pl_mod_can_target(&m, 9, 2) && !pl_mod_can_target(&m, PL_PAGE_PETRI, 2)); // TONAL, чашка
    CHECK(!pl_mod_can_target(&m, 14, 2) && !pl_mod_can_target(&m, PL_PAGE_MIX_DRUMS, 2) && !pl_mod_can_target(&m, PL_PAGE_MOD1, 9));
    CHECK(!pl_mod_can_target(&m, -1, 2) && !pl_mod_can_target(&m, 99, 2));
    // Тип синта меняет, какие ручки плавные: у 303 enc 10 пуст, у Plaits (M ENV) - нет, enc 8 (GLIDE) - не у Plaits.
    m.raw[6][1] = 0;
    CHECK(pl_mod_can_target(&m, 6, 2) && !pl_mod_can_target(&m, 6, 10) && !pl_mod_can_target(&m, 6, 7)); // WAVE - список
    m.raw[6][1] = 1;
    CHECK(pl_mod_can_target(&m, 6, 10) && pl_mod_can_target(&m, 6, 7) && !pl_mod_can_target(&m, 6, 8)); // TRANS - список
    m.raw[10][1] = PL_LAYER_OFF;
    CHECK(!pl_mod_can_target(&m, 10, 2));
}

static void set_target(int k, int idx, int page, int enc)
{
    m.mod_t[k][idx].page = (uint8_t)page;
    m.mod_t[k][idx].enc = (uint8_t)enc;
}

// Одно действующее значение на панели и в синте: те же числа на наборе out, AMOUNT, глубин и баз.
TEST(test_effective_matches_synth)
{
    static acid_mod_t am;
    acid_synth_init(&syn, 48000.0f);
    model_from_synth();
    acid_mod_init(&am, 1.0f / 1500.0f);
    static const int OUT[] = {-127, -100, -64, -33, -1, 0, 1, 7, 30, 63, 64, 99, 126, 127};
    static const int AMT[] = {0, 1, 40, 64, 127};
    static const int DEP[] = {0, 1, 33, 63, 64, 65, 80, 100, 127};
    static const int BASE[] = {0, 3, 64, 100, 124, 127};
    static const int TGT[][2] = {{0, 2}, {0, 3}, {8, 1}, {1, 1}};
    int checked = 0;
    for (unsigned a = 0; a < sizeof AMT / sizeof AMT[0]; a++) {
        for (unsigned d = 0; d < sizeof DEP / sizeof DEP[0]; d++) {
            for (unsigned o = 0; o < sizeof OUT / sizeof OUT[0]; o++) {
                for (unsigned b = 0; b < sizeof BASE / sizeof BASE[0]; b++) {
                    pl_model_init(&m);
                    model_from_synth();
                    acid_mod_init(&am, 1.0f / 1500.0f);
                    // Три модулятора на одну ручку (page 0, enc 2) и четвёртый на другую: складываются.
                    for (int k = 0; k < 4; k++) {
                        int out = k % 2 ? -OUT[o] : OUT[o];
                        int depth = k == 2 ? 127 - DEP[d] : DEP[d];
                        int amount = k == 3 ? 127 : AMT[a];
                        am.m[k].out = (float)out / 127.0f;
                        am.m[k].knob[ACID_MOD_AMOUNT] = (uint8_t)amount;
                        am.m[k].knob[ACID_MOD_DEPTH1] = (uint8_t)depth;
                        am.m[k].knob[ACID_MOD_DEPTH1 + 1] = 90;
                        am.m[k].t[0] = (acid_mod_target_t){0, k == 3 ? 3 : 2};
                        am.m[k].t[1] = (acid_mod_target_t){k == 3 ? 8 : 0xFF, 1};
                        m.mod_out[k] = (int8_t)out;
                        m.mod_knob[k][ACID_MOD_AMOUNT] = (uint8_t)amount;
                        m.mod_knob[k][ACID_MOD_DEPTH1] = (uint8_t)depth;
                        m.mod_knob[k][ACID_MOD_DEPTH1 + 1] = 90;
                        set_target(k, 0, 0, k == 3 ? 3 : 2);
                        set_target(k, 1, k == 3 ? 8 : PL_NONE, 1);
                        m.mod_have |= 1 << k;
                    }
                    for (unsigned t = 0; t < sizeof TGT / sizeof TGT[0]; t++) {
                        int page = TGT[t][0], enc = TGT[t][1], n_syn = 0, n_pl = 0;
                        m.raw[page][enc] = (uint8_t)BASE[b];
                        int want = BASE[b] + acid_mod_sum(&am, page, enc, &n_syn);
                        want = want < 0 ? 0 : want > 127 ? 127 : want;
                        CHECK(pl_mod_sum(&m, page, enc, &n_pl) == acid_mod_sum(&am, page, enc, NULL));
                        CHECK(n_pl == n_syn);
                        CHECK(pl_mod_effective(&m, page, enc) == want);
                        checked++;
                    }
                }
            }
        }
    }
    CHECK(checked > 5000);
}

TEST(test_effective_edges)
{
    acid_synth_init(&syn, 48000.0f);
    model_from_synth();
    // Без целей и до кадра PL_MOD - база как есть.
    m.raw[0][2] = 77;
    CHECK(pl_mod_effective(&m, 0, 2) == 77 && pl_mod_sum(&m, 0, 2, NULL) == 0);
    // Полный ход вниз и вверх прижимается к 0 и 127.
    m.mod_knob[0][ACID_MOD_AMOUNT] = 127;
    m.mod_knob[0][ACID_MOD_DEPTH1] = 127;
    set_target(0, 0, 0, 2);
    m.mod_out[0] = 127;
    CHECK(pl_mod_effective(&m, 0, 2) == 127);
    m.mod_out[0] = -127;
    m.raw[0][2] = 10;
    CHECK(pl_mod_effective(&m, 0, 2) == 0);
    CHECK(pl_mod_offset(127, 127, 127) == 126 && pl_mod_offset(-127, 127, 127) == -126 && pl_mod_offset(127, 127, 64) == 0);
    CHECK(pl_mod_offset(127, 0, 127) == 0);
    // Цель, которую у нового типа синта уже нельзя (не плавная), не двигает значение.
    m.raw[6][2] = 50;
    m.raw[6][1] = 1;
    set_target(0, 1, 6, 8); // TRANS у Plaits - список
    m.mod_knob[0][ACID_MOD_DEPTH1 + 1] = 127;
    m.mod_out[0] = 127;
    CHECK(pl_mod_effective(&m, 6, 8) == m.raw[6][8]);
    // Вне страниц и энкодеров: база без падения.
    CHECK(pl_mod_effective(&m, 99, 2) == 0 && pl_mod_effective(&m, 0, 16) == 0 && pl_mod_effective(&m, -1, 1) == 0);
}

// Синт перестал слать выход (демо: модуляторы стоят, PL_MOD_POS нет) - последний выход устаревает, ручки на базе.
TEST(test_stale_output_expires)
{
    acid_synth_init(&syn, 48000.0f);
    model_from_synth();
    m.raw[0][2] = 60;
    m.mod_knob[0][ACID_MOD_AMOUNT] = 127;
    m.mod_knob[0][ACID_MOD_DEPTH1] = 100;
    set_target(0, 0, 0, 2);
    int8_t pos[PL_MOD_POS_LEN] = {100, -50, 0, 0, 0, 0, 0, 0};
    pl_model_apply(&m, PL_MOD_POS, (const uint8_t *)pos, sizeof pos, 1000);
    CHECK(pl_mod_effective(&m, 0, 2) > 60);
    pl_model_mod_expire(&m, 1000 + PL_MOD_STALE_MS); // ровно на пороге ещё свежий
    CHECK(m.mod_out[0] == 100 && m.mod_out[1] == -50 && pl_mod_effective(&m, 0, 2) > 60);
    pl_model_mod_expire(&m, 1000 + PL_MOD_STALE_MS + 1);
    CHECK(m.mod_out[0] == 0 && m.mod_out[1] == 0 && pl_mod_effective(&m, 0, 2) == 60);
    // Время отрисовки раньше кадра (отстаёт на перерисовку) - не устаревание.
    pl_model_apply(&m, PL_MOD_POS, (const uint8_t *)pos, sizeof pos, 5000);
    pl_model_mod_expire(&m, 4990);
    CHECK(m.mod_out[0] == 100);
}

// Точка на форме волны: по горизонтали 0..255. У LFO это фаза с синта, у огибающей место считается по стадии
// и выходу - обратно той же кривой, которой панель рисует огибающую.
TEST(test_dot_position)
{
    for (int shape = ACID_SHAPE_SIN; shape < ACID_SHAPE_ENV; shape++) {
        CHECK(pl_mod_dot(shape, 50, 0, 64, 64) == 0 && pl_mod_dot(shape, -90, 200, 0, 127) == 200);
    }
    for (int a = 0; a <= 127; a += 127) {
        for (int d = 0; d <= 127; d += 127) {
            float split = pl_mod_env_split(a, d);
            int top = (int)(split * 255.0f + 0.5f);
            CHECK(split >= 0.08f && split <= 0.4f);
            CHECK(pl_mod_dot(ACID_SHAPE_ENV, 99, 0, a, d) == 0); // молчит: в начале
            CHECK(pl_mod_dot(ACID_SHAPE_ENV, 0, 1, a, d) == 0 && pl_mod_dot(ACID_SHAPE_ENV, 127, 1, a, d) == top);
            CHECK(pl_mod_dot(ACID_SHAPE_ENV, 127, 2, a, d) == top);
            int last = top;
            for (int out = 126; out >= 0; out--) { // спад: чем тише, тем правее, до края
                int x = pl_mod_dot(ACID_SHAPE_ENV, out, 2, a, d);
                CHECK(x >= last && x <= 255);
                last = x;
            }
            CHECK(last == 255);
            // Точка лежит на нарисованной кривой: на спаде высота кривой в её месте - выход.
            int x = pl_mod_dot(ACID_SHAPE_ENV, 64, 2, a, d);
            float p = (float)x / 255.0f;
            CHECK_NEAR(exp2f(-6.644f * (p - split) / (1.0f - split)), 64.0f / 127.0f, 0.03f);
        }
    }
}

TEST(test_edit_target)
{
    acid_synth_init(&syn, 48000.0f);
    model_from_synth();
    pl_mod_edit_t e;
    CHECK(!pl_mod_edit(&m, 1, 0, 0, 2, &e)); // кадра модулятора ещё не было
    m.mod_have = 0xF;
    CHECK(pl_mod_edit(&m, 1, 2, 0, 2, &e));
    CHECK(e.mod == 1 && e.idx == 2 && e.page == 0 && e.enc == 2);
    CHECK(m.mod_t[1][2].page == 0 && m.mod_t[1][2].enc == 2); // показываем сразу, эхо синта подтвердит
    CHECK(!pl_mod_edit(&m, 1, 3, 9, 2, &e));                  // TONAL нельзя
    CHECK(!pl_mod_edit(&m, 1, 3, 6, 1, &e));                  // TYPE нельзя
    CHECK(m.mod_t[1][3].page == PL_NONE);
    CHECK(!pl_mod_edit(&m, 4, 0, 0, 2, &e) && !pl_mod_edit(&m, -1, 0, 0, 2, &e));
    CHECK(!pl_mod_edit(&m, 0, 4, 0, 2, &e) && !pl_mod_edit(&m, 0, -1, 0, 2, &e));
    CHECK(pl_mod_edit(&m, 1, 2, PL_NONE, 0, &e)); // убрать цель
    CHECK(e.mod == 1 && e.idx == 2 && e.page == PL_NONE && e.enc == 0);
    CHECK(m.mod_t[1][2].page == PL_NONE);
    CHECK(pl_mod_count(&m, 1) == 0);
    pl_mod_edit(&m, 1, 0, 0, 2, &e);
    pl_mod_edit(&m, 1, 1, 12, 3, &e);
    CHECK(pl_mod_count(&m, 1) == 2 && pl_mod_count(&m, 0) == 0 && pl_mod_count(&m, 7) == 0);
}

int main(void)
{
    RUN(test_page_names);
    RUN(test_labels);
    RUN(test_value_lists_match_synth);
    RUN(test_value_text_numbers);
    RUN(test_lists_pick_by_grid);
    RUN(test_can_target_matches_synth);
    RUN(test_can_target_examples);
    RUN(test_effective_matches_synth);
    RUN(test_effective_edges);
    RUN(test_stale_output_expires);
    RUN(test_dot_position);
    RUN(test_edit_target);
    TEST_MAIN_END();
}
