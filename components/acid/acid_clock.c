#include "acid_clock.h"

#include <math.h>

#define BPM_MIN 60.0f
#define BPM_MAX 200.0f
#define HYST 0.015f  // скачок темпа больше 1.5% - сразу
#define DRIFT 0.002f // смещение больше 0.2% одним знаком целое окно - тоже
#define GAP 4u      // интервал больше 4 средних - разрыв (клок пропадал)
#define LONG 1.5f   // интервал больше 1.5 средних - может быть потерянный тик

void acid_clock_init(acid_clock_t *c, float sr)
{
    c->sr = sr;
    c->have_last = 0;
    c->n = c->pos = 0;
    c->gap = 0;
    c->drift = 0;
    c->pend = 0;
    c->spb = sr * 60.0f / 120.0f;
}

static void push(acid_clock_t *c, uint32_t d)
{
    c->iv[c->pos] = d;
    c->pos = (c->pos + 1) % ACID_CLOCK_WIN;
    c->n += c->n < ACID_CLOCK_WIN;
}

static uint64_t window_sum(const acid_clock_t *c)
{
    uint64_t sum = 0;
    for (int i = 0; i < c->n; i++) {
        sum += c->iv[i];
    }
    return sum;
}

static void update(acid_clock_t *c)
{
    if (c->n < ACID_CLOCK_PPQN) {
        return;
    }
    float spb = (float)window_sum(c) / (float)c->n * ACID_CLOCK_PPQN;
    float lo = c->sr * 60.0f / BPM_MAX, hi = c->sr * 60.0f / BPM_MIN;
    spb = spb < lo ? lo : (spb > hi ? hi : spb);
    float e = spb - c->spb;
    if (fabsf(e) > HYST * c->spb) {
        c->spb = spb;
        c->drift = 0;
    } else if (fabsf(e) > DRIFT * c->spb) {
        // Знак смещения в знаке счётчика: смена стороны начинает счёт заново.
        c->drift = e > 0.0f ? (c->drift > 0 ? c->drift + 1 : 1) : (c->drift < 0 ? c->drift - 1 : -1);
        if (c->drift >= ACID_CLOCK_WIN || c->drift <= -ACID_CLOCK_WIN) {
            c->spb = spb;
            c->drift = 0;
        }
    } else {
        c->drift = 0;
    }
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
    uint64_t sum = window_sum(c);
    // Тики доходят кусками рендера: бывают нулевые интервалы, их сумма с соседними верна.
    if (c->n >= ACID_CLOCK_PPQN && (uint64_t)d * (uint64_t)c->n > GAP * sum) {
        c->pend = 0;
        if (!c->gap) {
            c->gap = 1; // одиночный разрыв: клок пропадал, темп прежний
            return;
        }
        c->n = c->pos = 0; // разрывы подряд: темп и правда сильно упал, окно заново
    }
    c->gap = 0;
    float avg = c->n ? (float)sum / (float)c->n : 0.0f;
    if (c->pend) {
        uint32_t p = c->pend;
        c->pend = 0;
        if ((float)d > LONG * avg) {
            push(c, p); // второй длинный подряд: темп и правда упал
            push(c, d);
        } else {
            // Сумма двух интервалов - k средних: k - 1 тиков потеряно (k = 2 - просто джиттер).
            uint32_t total = p + d;
            uint32_t k = (uint32_t)((float)total / avg + 0.5f);
            uint32_t q = total / k;
            for (uint32_t i = 1; i < k; i++) {
                push(c, q);
            }
            push(c, total - q * (k - 1));
        }
    } else if (c->n >= ACID_CLOCK_PPQN && (float)d > LONG * avg) {
        c->pend = d; // решим на следующем тике
        return;
    } else {
        push(c, d);
    }
    update(c);
}

float acid_clock_spb(const acid_clock_t *c)
{
    return c->spb;
}
