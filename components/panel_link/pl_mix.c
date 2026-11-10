#include "pl_mix.h"

#include <stdint.h>

#include "pl_proto.h"

#define PAGE_MIX 0
#define PAGE_KIT 1
#define PAGE_CYM 2
#define PAGE_TOMS 3
#define PAGE_FX 8
#define DRIVE_ENC 13 // перегруз, тон, вой: энкодеры 14-16 у любого типа

int pl_mix_is(int page)
{
    return page >= PL_PAGE_MIX_DRUMS && page < PL_PAGE_MIX_DRUMS + PL_MIX_PAGES;
}

// Энкодер TRANS по типу: 303 - E10, Plaits - E9, у чашки и выключенного слоя нет (0).
static int trans_enc(int type)
{
    return type == 0 ? 9 : type >= 1 && type < PL_TYPE_PETRI ? 8 : 0;
}

static const uint8_t DRUMS[PL_KNOBS][2] = {
    [1] = {PAGE_MIX, 5},       [2] = {PAGE_MIX, 6},       [3] = {PAGE_MIX, 7},   [4] = {PAGE_MIX, 8}, // бочка, хэты, клэп, малый
    [5] = {PAGE_CYM, 1},       [6] = {PAGE_CYM, 5},       [7] = {PAGE_CYM, 9},                         // крэш, райд, купол
    [8] = {PAGE_TOMS, 11},     [9] = {PAGE_TOMS + 1, 11}, [10] = {PAGE_TOMS + 2, 11},                  // томы, рим, ковбелл
    [11] = {PAGE_KIT, 13},     [12] = {PAGE_KIT, 14},     [13] = {PAGE_KIT, 15},                       // панорамы
    [14] = {PAGE_FX, 5},                                                                               // посыл ударки
};

int pl_mix_target(int page, int enc, int type, int ltype, int *tpage, int *tenc)
{
    if (!pl_mix_is(page) || enc < 1 || enc >= PL_KNOBS) {
        return 0;
    }
    int p = -1, e = 0;
    if (page == PL_PAGE_MIX_DRUMS) {
        p = DRUMS[enc][0], e = DRUMS[enc][1];
    } else {
        int k = page - PL_PAGE_MIX_SYN1; // синт 0 или 1
        switch (enc) {
        case 1: p = PAGE_MIX, e = 3 + k; break;
        case 2: p = PAGE_MIX, e = 13 + k; break;
        case 3: p = PAGE_FX, e = 3 + k; break;
        case 4: p = PAGE_FX, e = 12 + k; break;
        case 5: p = PL_PAGE_SYNTH1 + k, e = trans_enc(type); break;
        case 6: p = PL_PAGE_LAYER1 + k, e = trans_enc(ltype); break;
        case 7:
        case 8:
        case 9: p = PL_PAGE_SYNTH1 + k, e = DRIVE_ENC + enc - 7; break;
        default: break;
        }
    }
    if (e == 0) {
        return 0;
    }
    *tpage = p, *tenc = e;
    return 1;
}
