// Выбор значения на панели: сетка для списков, тап для переключателей и триггеров, переход к чашке Петри.
#include <string.h>

#include "pl_pick.h"
#include "pl_view.h"
#include "test.h"

static uint8_t raw[16];

static int kind(int page, int enc)
{
    return pl_pick_kind(page, enc, raw);
}

static void synth_type(int t)
{
    memset(raw, 0, sizeof raw);
    raw[1] = (uint8_t)t;
}

TEST(test_kind_synth)
{
    synth_type(0); // 303
    CHECK(kind(PL_PAGE_SYNTH1, 1) == PL_PICK_GRID);   // TYPE
    CHECK(kind(PL_PAGE_SYNTH1, 2) == PL_PICK_NONE);   // CUTOFF
    CHECK(kind(PL_PAGE_SYNTH1, 7) == PL_PICK_TOGGLE); // WAVE
    CHECK(kind(PL_PAGE_SYNTH1, 9) == PL_PICK_GRID);   // TRANS
    synth_type(1); // Plaits
    CHECK(kind(PL_PAGE_SYNTH1 + 1, 7) == PL_PICK_NONE); // GLIDE
    CHECK(kind(PL_PAGE_SYNTH1 + 1, 8) == PL_PICK_GRID); // TRANS
    synth_type(PL_TYPE_PETRI);
    CHECK(kind(PL_PAGE_SYNTH1, 8) == PL_PICK_GRID);  // RELIEF
    CHECK(kind(PL_PAGE_SYNTH1, 10) == PL_PICK_GRID); // ORBIT
    CHECK(kind(PL_PAGE_SYNTH1, 12) == PL_PICK_NONE); // CENTER
    synth_type(PL_LAYER_OFF);
    CHECK(kind(PL_PAGE_LAYER1, 1) == PL_PICK_GRID);
    CHECK(kind(PL_PAGE_LAYER1, 2) == PL_PICK_NONE); // выключенный слой пуст
}

TEST(test_kind_other_pages)
{
    memset(raw, 0, sizeof raw);
    CHECK(kind(0, 1) == PL_PICK_NONE);  // MASTER
    CHECK(kind(3, 1) == PL_PICK_GRID);  // MODEL перкуссии
    CHECK(kind(5, 2) == PL_PICK_NONE);  // TUNE
    CHECK(kind(8, 6) == PL_PICK_GRID);  // DLY TIME
    CHECK(kind(8, 9) == PL_PICK_GRID);  // STUTTER
    CHECK(kind(8, 10) == PL_PICK_GRID); // MOTION
    CHECK(kind(8, 14) == PL_PICK_TOGGLE); // LYR HALF
    CHECK(pl_pick_next(8, 14, raw, 0) == 127); // порог 64
    CHECK(pl_pick_next(8, 14, raw, 63) == 127);
    CHECK(pl_pick_next(8, 14, raw, 64) == 0);
    // REVERB: крутилка, но тап открывает подэкран. Захват только сдвигом пальца, как у списков:
    // удержание без сдвига остаётся тапом, иначе дрожание пальца крутит ручку вместо входа.
    CHECK(kind(8, 15) == PL_PICK_NONE);
    CHECK(pl_pick_sub(8, 15) == PL_PAGE_REVERB);
    CHECK(pl_pick_grab(8, 15, raw, 0) == 1);
    CHECK(pl_pick_grab(8, 15, raw, 1) == 0);
    CHECK(pl_pick_sub(8, 14) == -1 && pl_pick_sub(0, 15) == -1 && pl_pick_sub(PL_PAGE_REVERB, 1) == -1);
    CHECK(pl_pick_grab(0, 5, raw, 1) == 1);
    CHECK(kind(8, 11) == PL_PICK_NONE); // WIDTH
    for (int e = 1; e <= 5; e++) {
        CHECK(kind(PL_PAGE_REVERB, e) == PL_PICK_NONE);
        CHECK(pl_pick_grab(PL_PAGE_REVERB, e, raw, 0));
        CHECK(pl_pick_grab(PL_PAGE_REVERB, e, raw, 1));
    }
    CHECK(kind(PL_PAGE_REVERB, 6) == PL_PICK_NONE);
    for (int e = 1; e <= 15; e++) {
        CHECK(kind(9, e) == PL_PICK_GRID); // TONAL: всё списки и шаги
    }
    CHECK(kind(PL_PAGE_PETRI, 1) == PL_PICK_GRID); // PRESET
    CHECK(kind(PL_PAGE_PETRI, 2) == PL_PICK_NONE); // F FINE
    for (int e = 4; e <= 8; e++) {
        CHECK(kind(PL_PAGE_PETRI, e) == PL_PICK_GRID); // STEPS .. VORTICES
    }
    CHECK(kind(PL_PAGE_PETRI, 14) == PL_PICK_TOGGLE);  // FREEZE
    CHECK(kind(PL_PAGE_PETRI, 15) == PL_PICK_TRIGGER); // RESEED
    CHECK(kind(PL_PAGE_GRIDS, 1) == PL_PICK_TOGGLE);   // ON
    CHECK(kind(PL_PAGE_GRIDS, 2) == PL_PICK_NONE);     // X
    CHECK(kind(PL_PAGE_GRIDS, 8) == PL_PICK_GRID);     // HAT MODE
    CHECK(kind(PL_PAGE_GRIDS, 9) == PL_PICK_TOGGLE);   // TIME HAND
    CHECK(kind(PL_PAGE_GRIDS, 12) == PL_PICK_TRIGGER); // FILL
    CHECK(kind(PL_PAGE_GRIDS, 13) == PL_PICK_GRID);    // PATTERN: карта, банк, свои
    CHECK(kind(PL_PAGE_GRIDS, 14) == PL_PICK_GRID);    // SWING
    CHECK(kind(PL_PAGE_GRIDS, 15) == PL_PICK_NONE);    // пусто
    CHECK(kind(PL_USER_FIRST, 1) == PL_PICK_NONE);     // своя страница: по цели, см. pl_pick_view
}

TEST(test_synth_types_in_order)
{
    synth_type(0);
    CHECK(pl_pick_count(PL_PAGE_SYNTH1, 1, raw) == 26);
    for (int s = 0; s < 26; s++) {
        CHECK(pl_pick_value(PL_PAGE_SYNTH1, 1, raw, s) == s);
        CHECK(pl_pick_slot(PL_PAGE_SYNTH1, 1, raw, s) == s);
    }
    CHECK(pl_pick_value(PL_PAGE_SYNTH1, 1, raw, 26) == -1);
    CHECK(pl_pick_value(PL_PAGE_SYNTH1, 1, raw, -1) == -1);
    CHECK(pl_pick_slot(PL_PAGE_SYNTH1, 1, raw, PL_LAYER_OFF) == -1); // у синта нет «выкл»
}

TEST(test_layer_types_skip_petri)
{
    synth_type(0);
    CHECK(pl_pick_count(PL_PAGE_LAYER1, 1, raw) == 26);
    CHECK(pl_pick_value(PL_PAGE_LAYER1, 1, raw, 24) == 24);
    CHECK(pl_pick_value(PL_PAGE_LAYER1, 1, raw, 25) == PL_LAYER_OFF);
    CHECK(pl_pick_slot(PL_PAGE_LAYER1, 1, raw, PL_TYPE_PETRI) == -1);
    CHECK(pl_pick_slot(PL_PAGE_LAYER1, 1, raw, PL_LAYER_OFF) == 25);
    for (int s = 0; s < 26; s++) {
        CHECK(pl_pick_value(PL_PAGE_LAYER1, 1, raw, s) != PL_TYPE_PETRI);
    }
}

TEST(test_list_values)
{
    synth_type(0);
    CHECK(pl_pick_count(PL_PAGE_SYNTH1, 9, raw) == 25); // TRANS -12..+12
    CHECK(pl_pick_value(PL_PAGE_SYNTH1, 9, raw, 0) == 52);
    CHECK(pl_pick_value(PL_PAGE_SYNTH1, 9, raw, 12) == 64);
    CHECK(pl_pick_slot(PL_PAGE_SYNTH1, 9, raw, 76) == 24);
    memset(raw, 0, sizeof raw);
    CHECK(pl_pick_count(9, 2, raw) == 8); // STEPS 1..8
    CHECK(pl_pick_value(9, 2, raw, 0) == 1);
    CHECK(pl_pick_count(9, 7, raw) == 12); // ROOT
    CHECK(pl_pick_count(3, 1, raw) == 5);  // MODEL
    CHECK(pl_pick_count(0, 1, raw) == 0);  // не список
}

TEST(test_tonal_steps_in_scale)
{
    memset(raw, 0, sizeof raw);
    CHECK(pl_pick_count(9, 8, raw) == 25); // хроматика: ±12
    raw[6] = 1;                            // MAJOR: синт зажимает до ±7
    CHECK(pl_pick_count(9, 8, raw) == 15);
    CHECK(pl_pick_value(9, 8, raw, 0) == 57);
    CHECK(pl_pick_value(9, 8, raw, 14) == 71);
    CHECK(pl_pick_slot(9, 8, raw, 52) == -1);
}

TEST(test_toggle_next)
{
    synth_type(0);
    CHECK(pl_pick_next(PL_PAGE_SYNTH1, 7, raw, 0) == 1); // WAVE
    CHECK(pl_pick_next(PL_PAGE_SYNTH1, 7, raw, 1) == 0);
    memset(raw, 0, sizeof raw);
    CHECK(pl_pick_next(PL_PAGE_PETRI, 14, raw, 127) == 0);
    CHECK(pl_pick_next(PL_PAGE_GRIDS, 1, raw, 0) == 1);
}

TEST(test_grab)
{
    synth_type(0);
    CHECK(pl_pick_grab(PL_PAGE_SYNTH1, 2, raw, 1)); // обычная ручка: и по удержанию,
    CHECK(pl_pick_grab(PL_PAGE_SYNTH1, 2, raw, 0)); // и по сдвигу
    CHECK(!pl_pick_grab(PL_PAGE_SYNTH1, 9, raw, 1)); // список: удержание - это тап
    CHECK(pl_pick_grab(PL_PAGE_SYNTH1, 9, raw, 0));
    CHECK(!pl_pick_grab(PL_PAGE_SYNTH1, 1, raw, 0)); // TYPE - только сеткой
    CHECK(!pl_pick_grab(PL_PAGE_SYNTH1, 7, raw, 0)); // переключатель
    memset(raw, 0, sizeof raw);
    CHECK(!pl_pick_grab(PL_PAGE_PETRI, 15, raw, 0)); // триггер
}

TEST(test_cells_hit_and_fit)
{
    static const int NS[] = {2, 3, 4, 5, 8, 12, 15, 17, 25, 26};
    for (unsigned k = 0; k < sizeof NS / sizeof NS[0]; k++) {
        int n = NS[k];
        for (int s = 0; s < n; s++) {
            int x, y, w, h;
            pl_pick_rect(n, s, &x, &y, &w, &h);
            CHECK(x >= 0 && y >= 40 && x + w <= 480 && y + h <= 480 && w >= 80 && h >= 50); // под шапкой, под палец
            CHECK(pl_pick_at(n, x + w / 2, y + h / 2) == s);
            CHECK(pl_pick_at(n, x, y) == s && pl_pick_at(n, x + w - 1, y + h - 1) == s);
            CHECK(pl_pick_at(n, x + w, y) == -1); // промежуток между клетками - мимо
        }
        CHECK(pl_pick_at(n, 240, 10) == -1); // шапка
    }
    int x, y, w, h;
    pl_pick_rect(26, 0, &x, &y, &w, &h); // типы: прежняя сетка 4x7
    CHECK(x == 6 && y == 46 && w == 111 && h == 56);
    pl_pick_rect(26, 25, &x, &y, &w, &h);
    CHECK(x == 6 + 117 && y == 46 + 6 * 62);
    CHECK(pl_pick_at(26, 6 + 3 * 117 + 50, 46 + 6 * 62 + 20) == -1); // клетка 27 пустая
}

TEST(test_view_alias)
{
    static pl_model_t m;
    pl_model_init(&m);
    m.have = 1;
    int tp, te;
    CHECK(pl_pick_view(&m, 9, 6, &tp, &te) == PL_PICK_GRID && tp == 9 && te == 6); // штатная страница
    CHECK(pl_pick_view(&m, PL_USER_FIRST, 1, &tp, &te) == PL_PICK_NONE);          // нет своей страницы
    m.user_have = 1;
    m.user_t[0][1][0] = (pl_target_t){(uint8_t)(9 << 4 | 6), 0, 127}; // псевдоним SCALE
    CHECK(pl_pick_view(&m, PL_USER_FIRST, 1, &tp, &te) == PL_PICK_GRID && tp == 9 && te == 6);
    m.user_t[0][2][0] = (pl_target_t){(uint8_t)(9 << 4 | 6), 0, 64}; // свой ход - макро
    CHECK(pl_pick_view(&m, PL_USER_FIRST, 2, &tp, &te) == PL_PICK_NONE);
}

TEST(test_dish_link)
{
    static pl_model_t m;
    pl_model_init(&m);
    CHECK(!pl_view_dish(&m, PL_PAGE_PETRI)); // до снимка не знаем
    m.have = 1;
    CHECK(pl_view_dish(&m, PL_PAGE_PETRI));
    CHECK(!pl_view_dish(&m, PL_PAGE_SYNTH1));
    m.raw[PL_PAGE_SYNTH1 + 1][1] = PL_TYPE_PETRI;
    CHECK(pl_view_dish(&m, PL_PAGE_SYNTH1 + 1));
    CHECK(!pl_view_dish(&m, PL_PAGE_SYNTH1));
    CHECK(!pl_view_dish(&m, PL_PAGE_LAYER1 + 1)); // слой не PETRI, даже если синт PETRI
    CHECK(!pl_view_dish(&m, 8));
    CHECK(!pl_view_dish(&m, PL_USER_FIRST));
}

// Вкладка MIX: класс ручки - как у цели (TRANS - сетка значений), значения берутся с её страницы.
TEST(test_view_mix)
{
    static pl_model_t m;
    pl_model_init(&m);
    m.have = 1;
    int tp, te;
    CHECK(pl_pick_view(&m, PL_PAGE_MIX_SYN1, 1, &tp, &te) == PL_PICK_NONE && tp == 0 && te == 3);
    CHECK(pl_pick_view(&m, PL_PAGE_MIX_SYN1, 5, &tp, &te) == PL_PICK_GRID && tp == PL_PAGE_SYNTH1 && te == 9);
    CHECK(pl_pick_view(&m, PL_PAGE_MIX_SYN1, 12, &tp, &te) == PL_PICK_NONE); // пустой слот
}

int main(void)
{
    RUN(test_kind_synth);
    RUN(test_kind_other_pages);
    RUN(test_synth_types_in_order);
    RUN(test_layer_types_skip_petri);
    RUN(test_list_values);
    RUN(test_tonal_steps_in_scale);
    RUN(test_toggle_next);
    RUN(test_grab);
    RUN(test_cells_hit_and_fit);
    RUN(test_view_alias);
    RUN(test_dish_link);
    RUN(test_view_mix);
    TEST_MAIN_END();
}
