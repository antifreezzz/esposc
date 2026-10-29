#include "acid_clock.h"

#include <math.h>

#define BPM_MIN 60.0f
#define BPM_MAX 200.0f
#define HYST 0.015f // темп меняется, только если ушёл больше чем на 1.5%
#define GAP 4u      // интервал больше 4 средних - разрыв (клок пропадал)

void acid_clock_init(acid_clock_t *c, float sr)
{
    c->sr = sr;
    c->have_last = 0;
    c->n = c->pos = 0;
    c->gap = 0;
    c->spb = sr * 60.0f / 120.0f;
}

static void push(acid_clock_t *c, uint32_t d)
{
    c->iv[c->pos] = d;
    c->pos = (c->pos + 1) % ACID_CLOCK_WIN;
    c->n += c->n < ACID_CLOCK_WIN;
}

void acid_clock_tick(acid_clock_t *c, uint32_t now)
{
    uint32_t d = now - c->last;
    int first = !c->have_last;
    c->last = now;
    c->have_last = 1;
    if (first) {
        return;
    }
    uint64_t sum = 0;
    for (int i = 0; i < c->n; i++) {
        sum += c->iv[i];
    }
    // Тики доходят кусками рендера: бывают нулевые интервалы, их сумма с соседними верна.
    if (c->n >= ACID_CLOCK_PPQN && (uint64_t)d * (uint64_t)c->n > GAP * sum) {
        if (!c->gap) {
            c->gap = 1; // одиночный разрыв: клок пропадал, темп прежний
            return;
        }
        c->n = c->pos = 0; // разрывы подряд: темп и правда сильно упал, окно заново
    }
    c->gap = 0;
    push(c, d);
    if (c->n < ACID_CLOCK_PPQN) {
        return;
    }
    sum = 0;
    for (int i = 0; i < c->n; i++) {
        sum += c->iv[i];
    }
    float spb = (float)sum / (float)c->n * ACID_CLOCK_PPQN;
    float lo = c->sr * 60.0f / BPM_MAX, hi = c->sr * 60.0f / BPM_MIN;
    spb = spb < lo ? lo : (spb > hi ? hi : spb);
    if (fabsf(spb - c->spb) > HYST * c->spb) {
        c->spb = spb;
    }
}

float acid_clock_spb(const acid_clock_t *c)
{
    return c->spb;
}
