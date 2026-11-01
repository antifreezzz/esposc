// Редактор своих страниц на панели: шаги выбора цели, ход lo/hi, удаление, назад.
#include <string.h>

#include "pl_edit.h"
#include "test.h"

static pl_model_t m;
static pl_edit_t e;
static pl_custom_edit_t out;

static void setup(void)
{
    pl_model_init(&m);
    m.have = 1;
    m.user_have = 0x3F;
}

TEST(test_add_target_path)
{
    setup();
    pl_edit_start(&e, 2);
    CHECK(e.state == PL_EDIT_SLOTS && e.up == 2);
    pl_edit_slot(&e, 5);
    CHECK(e.state == PL_EDIT_TARGETS && e.enc == 5);
    pl_edit_add(&e, &m);
    CHECK(e.state == PL_EDIT_PICK_PAGE && e.idx == 0);
    pl_edit_page(&e, 6);
    CHECK(e.state == PL_EDIT_PICK_KNOB && e.page == 6);
    CHECK(pl_edit_knob(&e, &m, 2, &out) == 1);
    CHECK(e.state == PL_EDIT_RANGE && e.which == 0);
    CHECK(out.up == 2 && out.enc == 5 && out.idx == 0 && out.pe == (6 << 4 | 2) && out.lo == 0 && out.hi == 127);
}

TEST(test_add_appends_after_existing)
{
    setup();
    m.user_t[0][3][0] = (pl_target_t){0 << 4 | 1, 0, 127};
    m.user_t[0][3][1] = (pl_target_t){0 << 4 | 2, 0, 127};
    pl_edit_start(&e, 0);
    pl_edit_slot(&e, 3);
    pl_edit_add(&e, &m);
    CHECK(e.idx == 2);
}

TEST(test_list_knob_default_range)
{
    setup();
    pl_edit_start(&e, 0);
    pl_edit_slot(&e, 1);
    pl_edit_add(&e, &m);
    pl_edit_page(&e, 8);
    CHECK(pl_edit_knob(&e, &m, 6, &out) == 1); // время дилэя: 0..4
    CHECK(out.lo == 0 && out.hi == 4);
}

TEST(test_type_and_empty_not_pickable)
{
    setup();
    pl_edit_start(&e, 0);
    pl_edit_slot(&e, 1);
    pl_edit_add(&e, &m);
    pl_edit_page(&e, 6);
    CHECK(pl_edit_knob(&e, &m, 1, &out) == 0); // тип синта
    CHECK(pl_edit_knob(&e, &m, 10, &out) == 0); // пустой слот 303
    CHECK(pl_edit_knob(&e, &m, 0, &out) == 0);
    CHECK(e.state == PL_EDIT_PICK_KNOB);
    pl_edit_back(&e);
    pl_edit_page(&e, PL_USER_FIRST); // своя страница целью не бывает
    CHECK(e.state == PL_EDIT_PICK_PAGE);
}

TEST(test_full_slot_no_add)
{
    setup();
    for (int i = 0; i < PL_TARGETS; i++) {
        m.user_t[1][4][i] = (pl_target_t){0 << 4 | (uint8_t)(i + 1), 0, 127};
    }
    pl_edit_start(&e, 1);
    pl_edit_slot(&e, 4);
    pl_edit_add(&e, &m);
    CHECK(e.state == PL_EDIT_TARGETS);
}

TEST(test_range_changes_which)
{
    setup();
    pl_edit_start(&e, 0);
    pl_edit_slot(&e, 1);
    pl_edit_add(&e, &m);
    pl_edit_page(&e, 0);
    pl_edit_knob(&e, &m, 1, &out);
    CHECK(pl_edit_range(&e, &m, 100, &out) == 1);
    CHECK(out.lo == 100 && out.hi == 127 && out.pe == (0 << 4 | 1) && out.idx == 0);
    e.which = 1;
    CHECK(pl_edit_range(&e, &m, 20, &out) == 1);
    CHECK(out.lo == 100 && out.hi == 20);
    // Список: ход зажат в границы списка.
    pl_edit_back(&e); // к целям слота
    pl_edit_add(&e, &m);
    pl_edit_page(&e, 8);
    pl_edit_knob(&e, &m, 6, &out);
    e.which = 1;
    pl_edit_range(&e, &m, 90, &out);
    CHECK(out.hi == 4);
}

TEST(test_open_existing_target)
{
    setup();
    m.user_t[3][2][1] = (pl_target_t){8 << 4 | 3, 10, 50};
    m.user_t[3][2][0] = (pl_target_t){0 << 4 | 1, 0, 127};
    pl_edit_start(&e, 3);
    pl_edit_slot(&e, 2);
    CHECK(pl_edit_open(&e, &m, 1) == 1);
    CHECK(e.state == PL_EDIT_RANGE && e.idx == 1);
    pl_edit_range(&e, &m, 60, &out);
    CHECK(out.pe == (8 << 4 | 3) && out.lo == 60 && out.hi == 50 && out.idx == 1);
    pl_edit_back(&e);
    CHECK(pl_edit_open(&e, &m, 2) == 0); // нет такой цели
}

TEST(test_remove)
{
    setup();
    pl_edit_start(&e, 0);
    pl_edit_slot(&e, 7);
    CHECK(pl_edit_remove(&e, 1, &out) == 1);
    CHECK(out.up == 0 && out.enc == 7 && out.idx == 1 && out.pe == PL_NONE);
    CHECK(e.state == PL_EDIT_TARGETS);
}

TEST(test_back_steps)
{
    setup();
    pl_edit_start(&e, 0);
    pl_edit_slot(&e, 1);
    pl_edit_add(&e, &m);
    pl_edit_page(&e, 0);
    pl_edit_knob(&e, &m, 1, &out);
    int seq[] = {PL_EDIT_TARGETS, PL_EDIT_SLOTS, PL_EDIT_OFF, PL_EDIT_OFF};
    for (int i = 0; i < 4; i++) {
        pl_edit_back(&e);
        CHECK(e.state == seq[i]);
    }
    pl_edit_start(&e, 0);
    pl_edit_slot(&e, 1);
    pl_edit_add(&e, &m);
    pl_edit_page(&e, 0);
    pl_edit_back(&e);
    CHECK(e.state == PL_EDIT_PICK_PAGE);
    pl_edit_back(&e);
    CHECK(e.state == PL_EDIT_TARGETS);
}

TEST(test_layer_type_not_pickable)
{
    setup();
    pl_edit_start(&e, 0);
    pl_edit_slot(&e, 1);
    pl_edit_add(&e, &m);
    pl_edit_page(&e, 10);
    CHECK(pl_edit_knob(&e, &m, 1, &out) == 0);
    CHECK(e.state == PL_EDIT_PICK_KNOB);
}

int main(void)
{
    RUN(test_layer_type_not_pickable);
    RUN(test_add_target_path);
    RUN(test_add_appends_after_existing);
    RUN(test_list_knob_default_range);
    RUN(test_type_and_empty_not_pickable);
    RUN(test_full_slot_no_add);
    RUN(test_range_changes_which);
    RUN(test_open_existing_target);
    RUN(test_remove);
    RUN(test_back_steps);
    TEST_MAIN_END();
}
