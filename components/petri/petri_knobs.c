#include "petri_knobs.h"

#include <string.h>

#define SYNTH1 6 // страницы синтов
#define TYPE_PETRI 25

static float lin(int v, float a, float b)
{
    return a + (b - a) * (float)v / 127.0f;
}

static float bip(int v)
{
    float x = (float)(v - 64) / 63.0f;
    return x < -1.0f ? -1.0f : x;
}

static float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

void petri_knobs_init(petri_knobs_t *k)
{
    memset(k, 0, sizeof *k);
    k->steps = 4;
}

int petri_knobs_apply(petri_t *p, petri_knobs_t *k, const uint8_t raw[PL_PAGES][PL_KNOBS])
{
    int which = raw[SYNTH1][1] == TYPE_PETRI ? 0 : raw[SYNTH1 + 1][1] == TYPE_PETRI ? 1 : -1;
    if (which < 0) {
        return -1;
    }
    const uint8_t *sy = raw[SYNTH1 + which], *pg = raw[PL_PAGE_PETRI];
    p->mode = sy[8] < PETRI_MODES ? sy[8] : 0;
    p->contrast = lin(sy[9], 0, 1);
    p->shape = sy[10] < PETRI_SHAPES ? sy[10] : 0;
    p->radius = lin(sy[11], 2, 48);
    p->follow = sy[12] == 127;
    p->drift = sy[12] >= 127 ? p->drift : 15.0f * (float)sy[12] / 126.0f;

    float f, kk;
    petri_preset(pg[1], &f, &kk);
    // Тонкие ручки сдвигают пресет: F на ±0.01, k на ±0.003, в пределах ручек прототипа.
    petri_set_fk(p, clampf(f + 0.01f * bip(pg[2]), 0.01f, 0.09f), clampf(kk + 0.003f * bip(pg[3]), 0.045f, 0.07f));
    k->steps = pg[4] <= 6 ? pg[4] : 6;
    p->appetite = lin(pg[9], 0, 0.5f);
    p->mutation = lin(pg[10], 0, 3);
    for (int kind = 0; kind < PETRI_KINDS; kind++) {
        if (!k->have || k->last[5 + kind] != pg[5 + kind]) {
            petri_set_count(p, kind, pg[5 + kind]);
        }
    }
    if (k->have && k->last[15] == 0 && pg[15] == 1) {
        petri_reseed(p); // переход 0 -> 1: пересев (жители остаются)
    }
    memcpy(k->last, pg, sizeof k->last);
    k->have = 1;
    return which;
}
