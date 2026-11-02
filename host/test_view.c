// Вид ячейки сетки для любой страницы 0..15: штатные - как подписи, свои - псевдоним и макро.
#include <string.h>

#include "pl_view.h"
#include "test.h"

static pl_model_t m;
static pl_cell_t c;

static void setup(void)
{
    pl_model_init(&m);
    m.have = 1;
    m.user_have = 0x3F;
}

TEST(test_standard_cell)
{
    setup();
    m.raw[8][6] = 3;
    m.held[8][6] = 1;
    pl_view_cell(&m, 8, 6, &c);
    CHECK(strcmp(c.label, "DLY TIME") == 0);
    CHECK(c.v == 3 && c.lo == 0 && c.hi == 4 && strcmp(c.text, "1/4") == 0);
    CHECK(c.held == 1 && c.color == pl_knob_color(8, 6, m.raw[8]));
    CHECK(c.src == NULL); // штатная страница: источник - она сама
    pl_view_cell(&m, 0, 15, &c);
    CHECK(c.label == NULL);
}

TEST(test_alias_cell_shows_target)
{
    setup();
    m.raw[6][2] = 88;
    m.user_t[0][4][0] = (pl_target_t){6 << 4 | 2, 0, 127};
    m.user_val[0][4] = 88;
    m.user_held[0][4] = 1;
    pl_view_cell(&m, PL_USER_FIRST, 4, &c);
    CHECK(strcmp(c.label, "CUTOFF") == 0);
    CHECK(c.v == 88 && c.lo == 0 && c.hi == 127 && strcmp(c.text, "88") == 0);
    CHECK(c.held == 1 && c.color == pl_knob_color(6, 2, m.raw[6]));
    m.raw[6][2] = 20; // цель двинули на родной странице: псевдоним за ней
    pl_view_cell(&m, PL_USER_FIRST, 4, &c);
    CHECK(c.v == 20);
}

TEST(test_macro_cell_own_value)
{
    setup();
    m.raw[6][2] = 88;
    m.user_t[1][2][0] = (pl_target_t){6 << 4 | 2, 0, 127};
    m.user_t[1][2][1] = (pl_target_t){8 << 4 | 3, 0, 60};
    m.user_val[1][2] = 40;
    pl_view_cell(&m, PL_USER_FIRST + 1, 2, &c);
    CHECK(strcmp(c.label, "CUTOFF +1") == 0);
    CHECK(c.v == 40 && c.lo == 0 && c.hi == 127 && strcmp(c.text, "40") == 0 && c.bipolar == 0);
    pl_view_cell(&m, PL_USER_FIRST + 1, 3, &c);
    CHECK(c.label == NULL);
}

TEST(test_user_cell_without_frame_empty)
{
    setup();
    m.user_have = 0;
    m.user_t[0][1][0] = (pl_target_t){0 << 4 | 1, 0, 127};
    pl_view_cell(&m, PL_USER_FIRST, 1, &c);
    CHECK(c.label == NULL);
}

TEST(test_user_cell_source)
{
    setup();
    m.user_t[0][4][0] = (pl_target_t){6 << 4 | 2, 0, 127};
    pl_view_cell(&m, PL_USER_FIRST, 4, &c);
    CHECK(c.src && strcmp(c.src, "SYNTH 1") == 0 && c.src_color == pl_page_color(6));
    m.user_t[0][4][1] = (pl_target_t){6 << 4 | 3, 0, 60}; // макро на одной странице
    pl_view_cell(&m, PL_USER_FIRST, 4, &c);
    CHECK(c.src && strcmp(c.src, "SYNTH 1") == 0 && c.src_color == pl_page_color(6));
    m.user_t[0][4][2] = (pl_target_t){8 << 4 | 3, 0, 60};
    pl_view_cell(&m, PL_USER_FIRST, 4, &c);
    CHECK(c.src && strcmp(c.src, "2 PAGES") == 0);
}

TEST(test_target_lookup)
{
    setup();
    m.user_t[1][2][0] = (pl_target_t){6 << 4 | 2, 0, 127};
    m.user_t[1][2][1] = (pl_target_t){8 << 4 | 3, 0, 60};
    int e = -1;
    CHECK(pl_view_target(&m, PL_USER_FIRST + 1, 2, 0, &e) == 6 && e == 2);
    CHECK(pl_view_target(&m, PL_USER_FIRST + 1, 2, 1, &e) == 8 && e == 3);
    CHECK(pl_view_target(&m, PL_USER_FIRST + 1, 2, 2, &e) == -1);
    CHECK(pl_view_target(&m, PL_USER_FIRST + 1, 2, 4, &e) == -1);
    CHECK(pl_view_target(&m, 6, 2, 0, &e) == -1); // штатная страница целей не имеет
    m.user_have = 0;
    CHECK(pl_view_target(&m, PL_USER_FIRST + 1, 2, 0, &e) == -1);
}

TEST(test_page_names_and_colors)
{
    CHECK(strcmp(pl_view_page_name(9), "TONAL") == 0);
    CHECK(strcmp(pl_view_page_name(10), "LAYER 1") == 0);
    CHECK(strcmp(pl_view_page_name(11), "LAYER 2") == 0);
    CHECK(pl_view_page_color(10) == pl_page_color(6) && pl_view_page_color(11) == pl_page_color(7));
    CHECK(strcmp(pl_view_page_name(PL_USER_FIRST), "USER 1") == 0);
    CHECK(strcmp(pl_view_page_name(PL_USER_FIRST + 5), "USER 6") == 0);
    CHECK(pl_view_page_name(PL_USER_FIRST + 6) == NULL);
    CHECK(pl_view_page_color(3) == pl_page_color(3));
    CHECK(pl_view_page_color(12) != 0);
}

TEST(test_page_step_stays_in_group)
{
    CHECK(pl_view_page_step(9, 1) == 10); // за штатными - слои
    CHECK(pl_view_page_step(11, 1) == 0);
    CHECK(pl_view_page_step(0, -1) == 11);
    CHECK(pl_view_page_step(PL_USER_FIRST + 5, 1) == PL_USER_FIRST);
    CHECK(pl_view_page_step(PL_USER_FIRST, -1) == PL_USER_FIRST + 5);
    CHECK(pl_view_page_step(PL_USER_FIRST + 2, 1) == PL_USER_FIRST + 3);
}

TEST(test_page_bsp_numbers)
{
    CHECK(pl_page_bsp(0) == 0 && pl_page_bsp(9) == 9);
    CHECK(pl_page_bsp(10) == 16 && pl_page_bsp(11) == 17);
    CHECK(pl_page_bsp(PL_USER_FIRST) == 10 && pl_page_bsp(PL_USER_FIRST + 5) == 15);
}

// Статус в шапке: без связи, live, в демо - имя песни.
TEST(test_status_text)
{
    static pl_model_t sm;
    pl_model_init(&sm);
    CHECK(strcmp(pl_view_status(&sm, 0), "NO LINK") == 0);
    sm.mode = 1;
    CHECK(strcmp(pl_view_status(&sm, 1), "LIVE") == 0);
    sm.mode = 0;
    sm.demo = PL_DEMO_METAL;
    CHECK(strcmp(pl_view_status(&sm, 1), "METAL") == 0);
    sm.demo = PL_DEMO_ACID;
    CHECK(strcmp(pl_view_status(&sm, 1), "ACID") == 0);
    sm.demo = 9; // чужой номер от синта новее панели
    CHECK(strcmp(pl_view_status(&sm, 1), "DEMO") == 0);
}

// Тап по статусу (шапка правее EDIT с запасом) меняет демо, только в демо и со связью.
TEST(test_demo_tap)
{
    static pl_model_t sm;
    pl_model_init(&sm);
    sm.have = 1;
    sm.mode = 0;
    CHECK(pl_view_demo_tap(&sm, 1, 450, 20, 452, 22));
    CHECK(!pl_view_demo_tap(&sm, 1, 420, 20, 420, 20)); // EDIT и его запас
    CHECK(!pl_view_demo_tap(&sm, 1, 450, 60, 450, 60)); // ниже шапки
    CHECK(!pl_view_demo_tap(&sm, 1, 450, 20, 450, 70)); // увёл палец вниз
    CHECK(!pl_view_demo_tap(&sm, 0, 450, 20, 450, 20)); // нет связи
    sm.mode = 1;
    CHECK(!pl_view_demo_tap(&sm, 1, 450, 20, 450, 20)); // live
}

int main(void)
{
    RUN(test_page_bsp_numbers);
    RUN(test_standard_cell);
    RUN(test_alias_cell_shows_target);
    RUN(test_macro_cell_own_value);
    RUN(test_user_cell_without_frame_empty);
    RUN(test_user_cell_source);
    RUN(test_target_lookup);
    RUN(test_page_names_and_colors);
    RUN(test_page_step_stays_in_group);
    RUN(test_status_text);
    RUN(test_demo_tap);
    TEST_MAIN_END();
}
