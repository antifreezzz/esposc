// Вкладка MIX инструмента: слоты встроенной страницы ведут на ручки штатных страниц.
#include "pl_mix.h"
#include "pl_proto.h"
#include "test.h"

static int hit(int page, int enc, int type, int ltype, int tp, int te)
{
    int p = -1, e = -1;
    return pl_mix_target(page, enc, type, ltype, &p, &e) && p == tp && e == te;
}

static int none(int page, int enc, int type, int ltype)
{
    int p, e;
    return !pl_mix_target(page, enc, type, ltype, &p, &e);
}

TEST(test_synth_mix)
{
    int s1 = PL_PAGE_MIX_SYN1, s2 = PL_PAGE_MIX_SYN1 + 1;
    CHECK(pl_mix_is(s1) && pl_mix_is(s2) && pl_mix_is(PL_PAGE_MIX_DRUMS) && !pl_mix_is(0) && !pl_mix_is(PL_USER_FIRST));
    CHECK(!pl_mix_is(PL_PAGE_MIX_DRUMS + PL_MIX_PAGES));
    CHECK(hit(s1, 1, 0, 0, 0, 3) && hit(s2, 1, 0, 0, 0, 4));   // громкость: MIX E4 / E5
    CHECK(hit(s1, 2, 0, 0, 0, 13) && hit(s2, 2, 0, 0, 0, 14)); // панорама: MIX E14 / E15
    CHECK(hit(s1, 3, 0, 0, 8, 3) && hit(s2, 3, 0, 0, 8, 4));   // посыл в дилэй: FX E4 / E5
    CHECK(hit(s1, 4, 0, 0, 8, 12) && hit(s2, 4, 0, 0, 8, 13)); // громкость слоя: FX E13 / E14
    // TRANS по типу: 303 - E10, Plaits - E9, у чашки нет
    CHECK(hit(s1, 5, 0, 0, PL_PAGE_SYNTH1, 9) && hit(s1, 5, 7, 0, PL_PAGE_SYNTH1, 8) && none(s1, 5, PL_TYPE_PETRI, 0));
    CHECK(hit(s2, 5, 24, 0, PL_PAGE_SYNTH1 + 1, 8));
    // TRANS слоя: по типу слоя, у выключенного нет
    CHECK(hit(s1, 6, 0, 0, PL_PAGE_LAYER1, 9) && hit(s2, 6, 0, 3, PL_PAGE_LAYER1 + 1, 8) && none(s1, 6, 0, PL_LAYER_OFF));
    // перегруз, тон, вой: E14-E16 у любого типа
    for (int k = 0; k < 3; k++) {
        CHECK(hit(s1, 7 + k, 0, 0, PL_PAGE_SYNTH1, 13 + k) && hit(s2, 7 + k, PL_TYPE_PETRI, 0, PL_PAGE_SYNTH1 + 1, 13 + k));
    }
    for (int e = 10; e < PL_KNOBS; e++) {
        CHECK(none(s1, e, 0, 0));
    }
    CHECK(none(s1, 0, 0, 0) && none(s1, 16, 0, 0) && none(0, 1, 0, 0));
}

TEST(test_drums_mix)
{
    int d = PL_PAGE_MIX_DRUMS;
    static const uint8_t T[][3] = {{1, 0, 5},  {2, 0, 6},  {3, 0, 7},   {4, 0, 8},   {5, 2, 1},  {6, 2, 5},  {7, 2, 9},
                                   {8, 3, 11}, {9, 4, 11}, {10, 5, 11}, {11, 1, 13}, {12, 1, 14}, {13, 1, 15}, {14, 8, 5}};
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        CHECK(hit(d, T[i][0], 0, 0, T[i][1], T[i][2]));
    }
    CHECK(none(d, 15, 0, 0));
}

// Цель - настоящая ручка штатной страницы, не тип синта.
TEST(test_targets_valid)
{
    static const int TYPES[] = {0, 1, 24, PL_TYPE_PETRI};
    for (int page = PL_PAGE_MIX_DRUMS; page < PL_PAGE_MIX_DRUMS + PL_MIX_PAGES; page++) {
        for (int ti = 0; ti < 4; ti++) {
            for (int enc = 1; enc < PL_KNOBS; enc++) {
                int p, e;
                if (pl_mix_target(page, enc, TYPES[ti], TYPES[ti] == PL_TYPE_PETRI ? PL_LAYER_OFF : TYPES[ti], &p, &e)) {
                    CHECK(p >= 0 && p < PL_PAGES && e >= 1 && e < PL_KNOBS);
                    int synth = p == PL_PAGE_SYNTH1 || p == PL_PAGE_SYNTH1 + 1 || p == PL_PAGE_LAYER1 || p == PL_PAGE_LAYER1 + 1;
                    CHECK(!(synth && e == 1)); // тип синта псевдонимом не крутим
                }
            }
        }
    }
}

int main(void)
{
    RUN(test_synth_mix);
    RUN(test_drums_mix);
    RUN(test_targets_valid);
    TEST_MAIN_END();
}
