#include "pl_load.h"

static uint8_t pct(uint32_t us, uint32_t budget)
{
    uint32_t p = (us * 100u + budget / 2) / budget;
    return (uint8_t)(p > 255 ? 255 : p);
}

void pl_load_pack(pl_load_t *o, const pl_load_acc_t *a, int guard)
{
    o->c1_mean = a->blocks ? pct(a->c1_sum_us / a->blocks, PL_LOAD_BLOCK_US) : 0;
    o->c1_worst = a->blocks ? pct(a->c1_worst_us, PL_LOAD_BLOCK_US) : 0;
    o->c0_mean = a->chunks ? pct(a->c0_sum_us / a->chunks, PL_LOAD_CHUNK_US) : 0;
    o->c0_worst = a->chunks ? pct(a->c0_worst_us, PL_LOAD_CHUNK_US) : 0;
    o->underruns = (uint8_t)(a->underruns > 255 ? 255 : a->underruns);
    o->guard = (uint8_t)guard;
}
