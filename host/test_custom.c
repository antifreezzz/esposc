// Свои страницы: цели слотов, псевдоним и макро, маппинг хода, blob.
#include <string.h>

#include "acid.h"
#include "acid_custom.h"
#include "test.h"

static acid_custom_t c;
static acid_synth_t s;

TEST(test_map_ends_and_inverse)
{
    acid_target_t t = {0, 1, 10, 90};
    CHECK(acid_custom_map(&t, 0) == 10);
    CHECK(acid_custom_map(&t, 127) == 90);
    CHECK(acid_custom_map(&t, 64) == 50);
    acid_target_t r = {0, 1, 100, 20};
    CHECK(acid_custom_map(&r, 0) == 100);
    CHECK(acid_custom_map(&r, 127) == 20);
    acid_target_t f = {0, 1, 0, 127};
    for (int v = 0; v <= 127; v++) {
        CHECK(acid_custom_map(&f, v) == v);
    }
}

TEST(test_edit_rejects_bad_targets)
{
    acid_custom_init(&c);
    CHECK(acid_custom_edit(&c, 0, 1, 0, ACID_USER_FIRST, 2, 0, 127) == 0);   // своя страница целью
    CHECK(acid_custom_edit(&c, 0, 1, 0, 6, 1, 0, 127) == 0);    // тип синта 1
    CHECK(acid_custom_edit(&c, 0, 1, 0, 7, 1, 0, 127) == 0);    // тип синта 2
    CHECK(acid_custom_edit(&c, 0, 1, 0, 0, 0, 0, 127) == 0);    // энкодер страницы
    CHECK(acid_custom_edit(&c, 0, 1, 0, 0, 16, 0, 127) == 0);
    CHECK(acid_custom_edit(&c, 6, 1, 0, 0, 1, 0, 127) == 0);    // нет такой своей страницы
    CHECK(acid_custom_edit(&c, 0, 0, 0, 0, 1, 0, 127) == 0);    // слот энкодера 1
    CHECK(acid_custom_edit(&c, 0, 1, 4, 0, 1, 0, 127) == 0);    // пятая цель
    CHECK(acid_custom_edit(&c, 0, 1, 0, 0, 1, 0, 128) == 0);
    CHECK(acid_custom_count(&c, 0, 1) == 0);
    CHECK(acid_custom_edit(&c, 0, 1, 0, 6, 2, 0, 127) == 1);
    CHECK(acid_custom_count(&c, 0, 1) == 1);
    CHECK(acid_custom_edit(&c, 0, 1, 2, 6, 3, 0, 127) == 0);    // дыра: целей одна, idx 2
}

TEST(test_remove_shifts)
{
    acid_custom_init(&c);
    acid_custom_edit(&c, 1, 5, 0, 0, 1, 0, 127);
    acid_custom_edit(&c, 1, 5, 1, 0, 2, 0, 127);
    acid_custom_edit(&c, 1, 5, 2, 0, 3, 0, 127);
    CHECK(acid_custom_count(&c, 1, 5) == 3);
    CHECK(acid_custom_edit(&c, 1, 5, 0, ACID_CUSTOM_NONE, 0, 0, 0) == 1);
    CHECK(acid_custom_count(&c, 1, 5) == 2);
    CHECK(c.t[1][5][0].enc == 2 && c.t[1][5][1].enc == 3);
    CHECK(c.t[1][5][2].page == ACID_CUSTOM_NONE);
}

TEST(test_alias)
{
    acid_custom_init(&c);
    acid_custom_edit(&c, 0, 1, 0, 6, 2, 0, 127);
    CHECK(acid_custom_is_alias(&c, 0, 1));
    acid_custom_edit(&c, 0, 2, 0, 6, 2, 0, 100);
    CHECK(!acid_custom_is_alias(&c, 0, 2));
    acid_custom_edit(&c, 0, 3, 0, 6, 2, 0, 127);
    acid_custom_edit(&c, 0, 3, 1, 6, 3, 0, 127);
    CHECK(!acid_custom_is_alias(&c, 0, 3));
    CHECK(!acid_custom_is_alias(&c, 0, 4)); // пустой слот
}

TEST(test_value_alias_follows_target)
{
    acid_synth_init(&s, 48000.0f);
    acid_custom_init(&c);
    acid_synth_set_knob(&s, 6, 2, 77);
    acid_custom_edit(&c, 0, 1, 0, 6, 2, 0, 127);
    CHECK(acid_custom_value(&c, &s, 0, 1) == 77);
    acid_custom_edit(&c, 0, 2, 0, 6, 2, 0, 50);
    c.val[0][2] = 33;
    CHECK(acid_custom_value(&c, &s, 0, 2) == 33);
}

TEST(test_blob_roundtrip_and_reject)
{
    static uint8_t blob[ACID_CUSTOM_BLOB];
    acid_custom_init(&c);
    acid_custom_edit(&c, 2, 7, 0, 8, 3, 5, 120);
    acid_custom_edit(&c, 2, 7, 1, 0, 11, 127, 0);
    c.val[2][7] = 99;
    CHECK(acid_custom_export(&c, blob, sizeof blob) == ACID_CUSTOM_BLOB);
    CHECK(acid_custom_export(&c, blob, sizeof blob - 1) == 0);

    static acid_custom_t d;
    acid_custom_init(&d);
    CHECK(acid_custom_import(&d, blob, sizeof blob) == 1);
    CHECK(memcmp(d.t, c.t, sizeof c.t) == 0 && memcmp(d.val, c.val, sizeof c.val) == 0);

    acid_custom_init(&d);
    CHECK(acid_custom_import(&d, blob, sizeof blob - 1) == 0);
    blob[0] ^= 1;
    CHECK(acid_custom_import(&d, blob, sizeof blob) == 0);
    blob[0] ^= 1;
    // Цель с невозможной страницей: blob чужой целиком, ничего не применяется.
    size_t at = 4 + ((2 * ACID_KNOBS + 7) * ACID_USER_TARGETS) * sizeof(acid_target_t);
    blob[at] = 12;
    CHECK(acid_custom_import(&d, blob, sizeof blob) == 0);
    CHECK(acid_custom_count(&d, 2, 7) == 0 && d.val[2][7] == 0);
}

TEST(test_edit_sets_save)
{
    acid_custom_init(&c);
    CHECK(c.save == 0);
    acid_custom_edit(&c, 0, 1, 0, ACID_USER_FIRST, 2, 0, 127);
    CHECK(c.save == 0);
    acid_custom_edit(&c, 0, 1, 0, 6, 2, 0, 127);
    CHECK(c.save == 1);
}

int main(void)
{
    RUN(test_map_ends_and_inverse);
    RUN(test_edit_rejects_bad_targets);
    RUN(test_remove_shifts);
    RUN(test_alias);
    RUN(test_value_alias_follows_target);
    RUN(test_blob_roundtrip_and_reject);
    RUN(test_edit_sets_save);
    TEST_MAIN_END();
}
